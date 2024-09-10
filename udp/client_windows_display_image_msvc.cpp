#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <WS2tcpip.h>
#include <tchar.h>
#include "queue.h"

#pragma comment(lib, "ws2_32.lib") // Link with ws2_32.lib

#define SERVER_PORT 8888
#define SERVER_IP "192.168.8.113"
#define BUFFER_SIZE (1024 * 1024)

#pragma pack(1)
typedef struct {
#define MAGIC 0X55AA
    unsigned short magic;
    unsigned int size;
    union {
        unsigned short type;
        struct {
            unsigned short type_sub : 8;
            unsigned short type : 7;
            unsigned short rw : 1;
        } type_div;
    };
    //   char data[0];
} frame_header;

typedef struct {
#define TAIL 0XAA55
    unsigned short crc16;
    unsigned short tail;
} frame_tail;

typedef struct {
    frame_header header;
    frame_tail tail;
} frame_t;
#pragma pack()

enum cmds {
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_Y8_MODE,
    CMD_Y8_AND_Y16_MODE,
    CMD_DATA,
    CMD_KEEP_LIVE,
    CMD_QUERY_MODE,
    CMD_Y8_DATA = 0X8104,
    CMD_Y16_DATA = 0X8105,
};

struct opt_args {
	char *width;
	char *height;
	char *deepth;
	char *frm_rate;
};

typedef char data_buf[700 * 1024];
class_type user; // struct 类型前缀 后缀为queue
CONSTRUCT_QUEUE_CONTEXT_BY_TYPE(data_buf, 16, 6, user);
GENERATE_QUEUE_OBJECT(data_buf, user);


SOCKET sockfd;
struct sockaddr_in server_addr;
int addr_len;
HANDLE keep_live_thread;
static int keep_live_run = 0;
static int main_run = 0;

static void DisplayRGB(HWND hwnd, unsigned char* rgb_frame, int width, int height);
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void send_cmd(enum cmds command)
{
    frame_t cmd;
    memset(&cmd, 0, sizeof(frame_t));
    cmd.header.magic = MAGIC;
    cmd.header.size = 0;
    cmd.header.type = command;
    cmd.tail.crc16 = 0;
    cmd.tail.tail = TAIL;
    const char* message = (char*)&cmd;

    int sent_bytes = sendto(sockfd, message, sizeof(frame_t), 0, (const struct sockaddr*)&server_addr, addr_len);
    if (sent_bytes == SOCKET_ERROR) {
        printf("send_cmd failed with error: %d\n", WSAGetLastError());
    }
    else {
        printf("Command sent successfully, bytes sent: %d\n", sent_bytes);
    }
}

BOOL WINAPI signal_handler(DWORD signal)
{
    if (signal == CTRL_C_EVENT) {
        printf("Signal received, disconnecting...\n");
        send_cmd(CMD_DISCONNECT);
        keep_live_run = 0;
        main_run = 0;
        WaitForSingleObject(keep_live_thread, INFINITE);
        closesocket(sockfd);
        WSACleanup();
        exit(1);
    }
    return TRUE;
}

unsigned __stdcall keep_live_cb(void* arg)
{
    keep_live_run = 1;
    while (keep_live_run) {
        Sleep(3000); // Sleep for 3 seconds
        printf("Sending keep alive command...\n");
        send_cmd(CMD_KEEP_LIVE);
    }
    return 0;
}

void usage(void)
{
	printf(	"unitcam usage:\n"
			"	-W set pixel width\n"
			"	-H set pixel height\n"
			"	-h help\n");
}

void parse_args(struct opt_args *args, int argc, char **argv)
{
	int ch;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-W") == 0 && i + 1 < argc) {
            args->width = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-H") == 0 && i + 1 < argc) {
            args->height = argv[i + 1];
            i++;
        }
    }

	if (argc < 3) {
		usage();
		printf("Error: too few params, -W -H -d -f are all needed\n");
		exit(EXIT_FAILURE);
	}
}

int y8torgb(char *y8, unsigned int y8_len, unsigned char *rgb) {
        if (y8 == NULL || rgb == NULL) {
                return -1;
        }

        for (unsigned int i = 0; i < y8_len; i++) {
                unsigned char y = (unsigned char)y8[i];

                rgb[3 * i] = y;
                rgb[3 * i + 1] = y;
                rgb[3 * i + 2] = y;
        }

        return 0;
}

int main(int argc, char *argv[]) {
        WSADATA wsaData;
        //char buffer[BUFFER_SIZE];
        //char data_buf[BUFFER_SIZE];
        char* buffer = (char *)malloc(BUFFER_SIZE);
        char* data_buf = (char*)malloc(BUFFER_SIZE);
        if (!buffer || !data_buf)
                return -1;
	struct opt_args args;

	parse_args(&args, argc, argv);
	unsigned int width = atoi(args.width);
	unsigned int height = atoi(args.height);
	unsigned char *rgb_frame = (unsigned char *)malloc(width * height * 3);
        addr_len = sizeof(server_addr);

    printf("Starting the program...\r\n");
    fflush(stdout);

        const wchar_t CLASS_NAME[] = L"RGB Display Class";

        WNDCLASS wc = {0};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = CLASS_NAME;

        RegisterClass(&wc);

        HWND hwnd = CreateWindowEx(
                        0,
                        CLASS_NAME,
                        L"Display",
                        WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, width + width * 0.05, height + height * 0.1,
                        NULL,
                        NULL,
                        GetModuleHandle(NULL),
                        NULL
                        );

        if (hwnd == NULL) {
                return 0;
        }

        ShowWindow(hwnd, SW_SHOWNORMAL);

        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                printf("WSAStartup failed with error: %d\n", WSAGetLastError());
                return 1;
        }

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET) {
        printf("Socket creation failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Fill server information
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    //server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    InetPton(AF_INET, _T(SERVER_IP), &server_addr.sin_addr.s_addr);
    //server_addr.sin_addr.s_addr = inet_pton(SERVER_IP));

    printf("Sending initial connect command...\n");
    send_cmd(CMD_CONNECT);
    send_cmd(CMD_QUERY_MODE);
    Sleep(1000);

    printf("Client started. Waiting for data...\n");

    SetConsoleCtrlHandler((PHANDLER_ROUTINE)signal_handler, TRUE);

    frame_header* hd = (frame_header*)buffer;
    keep_live_thread = (HANDLE)_beginthreadex(NULL, 0, keep_live_cb, NULL, 0, NULL);

    main_run = 1;
    while (main_run) {
        static unsigned int offset = 0;
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
        if (n == SOCKET_ERROR) {
            printf("recvfrom failed with error: %d\n", WSAGetLastError());
            break;
        }
        // printf("Received data, bytes: %d\n", n);
        switch (hd->type) {
        case CMD_Y8_DATA:
                                printf("Y8 data received, offset: %d\n", offset);
                                if (offset == 120020) {
                                    y8torgb(data_buf + 16, width * height, rgb_frame);
                                    DisplayRGB(hwnd, rgb_frame, width, height);
                                }
                                offset = 0;
                                memcpy(data_buf, buffer, n);
                                offset += n;
                                break;
                        case CMD_Y16_DATA:
                                printf("Y16 data received\n");
                                if (offset == 120020) {
                                    y8torgb(data_buf + 16, width * height, rgb_frame);
                                    DisplayRGB(hwnd, rgb_frame, width, height);
                                }
                                offset = 0;
                                memcpy(data_buf, buffer, n);
                                offset += n;
                                break;
                        case CMD_QUERY_MODE:
                                printf("Data mode: %x\n", *(unsigned int *)(buffer + 8));
                                break;
                        default:
                                memcpy(data_buf + offset, buffer, n);
                                offset += n;
                }
        }

        free(rgb_frame);
        free(buffer);
        free(data_buf);
        closesocket(sockfd);
        WSACleanup();
        return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
                case WM_PAINT: {
                                       PAINTSTRUCT ps;
                                       HDC hdc = BeginPaint(hwnd, &ps);
                                       EndPaint(hwnd, &ps);
                               }
                               return 0;

                case WM_DESTROY:
                               PostQuitMessage(0);
                               return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void DisplayRGB(HWND hwnd, unsigned char* rgb_frame, int width, int height) {
        HDC hdc = GetDC(hwnd);

        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(BITMAPINFO));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // Negative height to indicate top-down drawing
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24; // 24 bits for RGB
        bmi.bmiHeader.biCompression = BI_RGB;

        SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height, rgb_frame, &bmi, DIB_RGB_COLORS);

        ReleaseDC(hwnd, hdc);
}
