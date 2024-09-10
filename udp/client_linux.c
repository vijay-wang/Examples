#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

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
                        unsigned short type_sub:8;
                        unsigned short type:7;
                        unsigned short rw:1;
                } type_div;
        };
        char data[0];
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

int sockfd;
struct sockaddr_in server_addr;
socklen_t addr_len;
pthread_t keep_live_tid;
static int keep_live_run = 0;
static int main_run = 0;

void send_cmd(enum cmds command)
{
        frame_t cmd;
        memset(&cmd, 0, sizeof(frame_t));
        cmd.header.magic = MAGIC;
        cmd.header.size = 0;
        cmd.header.type = command;
        cmd.tail.crc16 = 0;
        cmd.tail.tail = TAIL;
        const char *message = (char *)&cmd;
        sendto(sockfd, message, sizeof(frame_t), 0, (const struct sockaddr *)&server_addr, addr_len);
}

void signal_handler(int signal)
{
        send_cmd(CMD_DISCONNECT);
        keep_live_run = 0;
        main_run = 0;
        pthread_join(keep_live_tid, NULL);
        exit(1);
}

void *keep_live_cb(void *arg)
{
        keep_live_run = 1;
        while (keep_live_run) {
                sleep(3);
                send_cmd(CMD_KEEP_LIVE);
        }
}

int main() {
        char buffer[BUFFER_SIZE];
        char data_buf[BUFFER_SIZE];
        char cmd_ret[32];
        addr_len = sizeof(server_addr);

        // Create UDP socket
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) <0) {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
        }

        // Fill server information
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

        // Send an initial message to the server to "register"this client
        send_cmd(CMD_CONNECT);
        send_cmd(CMD_QUERY_MODE);
        sleep(1);

        printf("Client started. Waiting for data...\n");

        signal(SIGINT, signal_handler);
        frame_header *hd = ( frame_header *)buffer;
        pthread_create(&keep_live_tid, NULL, keep_live_cb, NULL);
        // Main loop to receive data from the server
        main_run = 1;
        while (main_run) {
                static unsigned int offset = 0;
                int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&server_addr, &addr_len);
                switch (hd->type) {
                case CMD_Y8_DATA:
                        printf("offset: %d\n", offset);
                        offset = 0;
                        memcpy(data_buf, buffer, n);
                        offset += n;
                        break;
                case CMD_Y16_DATA:
                        printf("offset: %d\n", offset);
                        offset = 0;
                        memcpy(data_buf, buffer, n);
                        offset += n;
                        break;
                case CMD_QUERY_MODE:
                        printf("data mode: %x\n", *(unsigned int *)(buffer + 8));
                        break;
                default:
                        memcpy(data_buf + offset, buffer, n);
                        offset += n;
                }
        }

        close(sockfd);
        return 0;
}
