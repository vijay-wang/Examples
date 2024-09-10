#include "ir_udp_server.h"
#include "queue.h"
#include "log_file_dbg.h"
#include <signal.h>

typedef struct {
        char buf[700 * 1024];
        unsigned int buf_len;
} data_buf;

//typedef char data_buf[700 * 1024]; // 队列成员类型
class_type frame; // struct 类型前缀 后缀为queue
CONSTRUCT_QUEUE_CONTEXT_BY_TYPE(data_buf, 16, 6, frame);
GENERATE_QUEUE_OBJECT(data_qu, frame);
#define QUEUE_DEEPTH 5
#define MTU 20000

pthread_t udp_send_data_tid;
pthread_t udp_cmd_tid;

static struct ir_udp_server_t svr;
static int cmd_routine_run = 0;
static int udp_send_data_run = 0;
static struct timespec ts;

static ClientNode* addClient(server_t *server, int *num_clients);
static ClientNode* removeClient(ClientNode* head, struct sockaddr_in client_addr, int *num_clients);
static ClientNode* isClientExist(ClientNode* head, struct sockaddr_in client_addr);
static void sendToAllClients(ClientNode* head, int sockfd, const char* buffer, int buffer_len);
static void sendToClient(int sockfd, struct sockaddr_in client_addr, const char* message);
static void toggleMode(struct ir_udp_server_t *server, DATA_MODE mode);
void echo(int sockfd, struct sockaddr_in dest_addr, enum cmds command, char *data, unsigned int data_len);

static void set_live_timer(ClientNode* head)
{
        if (head == NULL) {
		LOG_FILE_ERR("NULL Pointer\n");
                return;
        }
	struct itimerspec timer_value;
	timer_value.it_value.tv_sec = 10;
	timer_value.it_value.tv_nsec = 0;
	timer_value.it_interval.tv_sec = 0;
	timer_value.it_interval.tv_nsec = 0;
	if (timer_settime(head->live_timer, 0, &timer_value, NULL) == -1) // reset timer
		LOG_FILE_ERR("set timer failed\n");
}

static void live_timer_handler(union sigval ptr)
{
        int num_clients;
        ClientNode *client = (ClientNode *)ptr.sival_ptr;
        server_t *svr = client->server;
        client->status = STREAM_OFF;
        usleep(1000 * 10);
        if (!isClientExist(svr->client_list, client->addr))
                return;
        LOG_FILE_INFO("Client removed: %s:%d, there are still %d clients online\n", inet_ntoa(client->addr.sin_addr), ntohs(client->addr.sin_port), num_clients);
        svr->client_list = removeClient(svr->client_list, client->addr, &num_clients);
}

static int live_timer_init(ClientNode* node)
{
	// struct sigaction sa;
	struct sigevent evp;
	struct itimerspec its;

	evp.sigev_value.sival_ptr = node;
	evp.sigev_notify = SIGEV_THREAD;
	evp.sigev_notify_function = live_timer_handler;

	// Create the timer
	if (timer_create(CLOCK_REALTIME, &evp, &node->live_timer) == -1) {
		LOG_FILE_ERR("ifr_timer create failed\n");
		return -1;
	}
	return 0;
}

void echo(int sockfd, struct sockaddr_in dest_addr, enum cmds command, char *data, unsigned int data_len)
{
        unsigned int len = data_len + sizeof(frame_header) + sizeof(frame_tail);
        char *buf = (char *)malloc(len);
        frame_header header;
        frame_tail tail;
        memset(buf, 0, len);
        header.magic = MAGIC;
        header.size = 0;
        header.type = command;
        tail.crc16 = 0;
        tail.tail = TAIL;
        memcpy(buf, &header, sizeof(header));
        memcpy(buf + sizeof(header), data, data_len);
        memcpy(buf + sizeof(header) + data_len, &tail, sizeof(tail));
        sendto(sockfd, buf, len, 0, (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
        free(buf);
}

void *udp_send_data(void *arg)
{
        struct ir_udp_server_t *svr = (struct ir_udp_server_t *)arg;
        udp_send_data_run = 1;
        while (udp_send_data_run) {
                if (!sem_timedwait_by_type(data_qu, 200, frame)) {
                        sendToAllClients(svr->client_list, svr->sockfd, data_qu.pdata[data_qu.front].buf, data_qu.pdata[data_qu.front].buf_len);
                        dequeue_by_type(data_qu, frame);
                }
        }
        return NULL;
}

int push_udp_ir_data(char *data, unsigned int len)
{
        int ret;
        if (data_qu.pdata == NULL || data == NULL) {
                return -1;
        }
        if (!is_full_by_type(data_qu, frame)) {
                memcpy(data_qu.pdata[data_qu.rear].buf, data, len);
                data_qu.pdata[data_qu.rear].buf_len = len;
                enqueue_by_type(data_qu, frame);
                ret = sem_post_by_type(data_qu, frame);
        } else {
                ret = -1;
                LOG_FILE_DBG("queue full\n");
        }
        return ret;
}

void *udp_cmd_routine(void *arg)
{
        socklen_t addr_len = sizeof(svr.client_addr);
        int num_clients = 0;
        svr.client_list = NULL;

        svr.data_mode = MODE_Y8_AND_Y16;
        // Create UDP socket
        if ((svr.sockfd = socket(AF_INET, SOCK_DGRAM, 0)) <0) {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
        }

        // Fill server information
        memset(&svr.server_addr, 0, sizeof(svr.server_addr));
        svr.server_addr.sin_family = AF_INET;
        svr.server_addr.sin_addr.s_addr = INADDR_ANY;
        svr.server_addr.sin_port = htons(PORT);

        // Bind the socket with the server address
        if (bind(svr.sockfd, (const struct sockaddr *)&svr.server_addr, sizeof(svr.server_addr)) <0) {
                perror("Bind failed");
                close(svr.sockfd);
                exit(EXIT_FAILURE);
        }

	LOG_FILE_DBG("Server started. Waiting for clients...\n");

        char recv_buf[BUFFER_SIZE] = { 0 };
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(svr.sockfd, &readfds);
        // Main loop to handle clients and send data
        cmd_routine_run = 1;

        while (cmd_routine_run) {
                // Receive data from a client
                int ret = select(svr.sockfd + 1, &readfds, NULL, NULL, NULL);
                if (ret < 0)
                        perror("select failed");
                int n = recvfrom(svr.sockfd, recv_buf, BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *)&svr.client_addr, &addr_len);
                frame_header *frm = (frame_header *)recv_buf;

                if (n > 0 && frm->magic == MAGIC) {
                        recv_buf[n] = '\0';
                        uint16_t cmd = frm->type;

                        switch (cmd) {
                        // Check for "disconnect"message
                        case CMD_DISCONNECT:
                                if (!isClientExist(svr.client_list, svr.client_addr))
                                        break;
                                svr.client_list = removeClient(svr.client_list, svr.client_addr, &num_clients);
                                LOG_FILE_INFO("Client disconnected: %s:%d\n", inet_ntoa(svr.client_addr.sin_addr), ntohs(svr.client_addr.sin_port));
                                break;
                        // Check if the client is already in the list
                        case CMD_CONNECT:
                                if (!isClientExist(svr.client_list, svr.client_addr)) {
                                        // Check if max clients is reached
                                        if (num_clients >= MAX_CLIENTS) {
                                                LOG_FILE_INFO("Maximum clients reached. Connection rejected: %s:%d\n", inet_ntoa(svr.client_addr.sin_addr), ntohs(svr.client_addr.sin_port));
                                                sendToClient(svr.sockfd, svr.client_addr, "Maximum client limit reached. Connection rejected.");
                                        } else{
                                                svr.client_list = addClient(&svr, &num_clients);
                                                LOG_FILE_INFO("New client connected: %s:%d\n", inet_ntoa(svr.client_addr.sin_addr), ntohs(svr.client_addr.sin_port));
                                        }
                                }
                                break;
                        case CMD_Y8_MODE:
                                toggleMode(&svr, MODE_Y16);
                                break;
                        case CMD_Y8_AND_Y16_MODE:
                                toggleMode(&svr, MODE_Y8_AND_Y16);
                                break;
                        case CMD_QUERY_MODE:
                                echo(svr.sockfd, svr.client_addr, CMD_QUERY_MODE, (char *)&svr.data_mode, sizeof(svr.data_mode));
                                break;
                        case CMD_KEEP_LIVE:
                                set_live_timer(isClientExist(svr.client_list, svr.client_addr));
                                break;
                        default:
                                ;
                        }
                }
        }
        return NULL;
}

#ifdef __cplusplus
extern "C" {
#endif
int ir_udp_server_deinit(void)
{
        int ret;
        cmd_routine_run = 0;
        udp_send_data_run = 0;
        pthread_join(udp_cmd_tid, NULL);
        pthread_join(udp_send_data_tid, NULL);
        ret |= close(svr.sockfd);
        queue_deinit_by_type(data_qu, frame);
        return ret;
}

int ir_udp_server_init(void)
{
        queue_init_by_type(data_qu, QUEUE_DEEPTH, frame);
        memset(&svr, 0, sizeof(struct ir_udp_server_t));
        pthread_create(&udp_send_data_tid, NULL, udp_send_data, &svr);
        pthread_create(&udp_cmd_tid, NULL, udp_cmd_routine, NULL);
        return 0;
}
#ifdef __cplusplus
}
#endif

static ClientNode* addClient(server_t *server, int *num_clients) {
        ClientNode* new_node = (ClientNode*)malloc(sizeof(ClientNode));
        live_timer_init(new_node);
        set_live_timer(new_node);
        new_node->addr = server->client_addr;
        new_node->next = server->client_list;
        new_node->server = server;
        new_node->status = STREAM_ON;
        (*num_clients)++;
        return new_node;
}

static ClientNode* removeClient(ClientNode* head, struct sockaddr_in client_addr, int *num_clients) {
        ClientNode *temp = head, *prev = NULL;

        // If the client to be deleted is the head node
        if (temp != NULL && temp->addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && temp->addr.sin_port == client_addr.sin_port) {
                head = temp->next;
                free(temp);
                (*num_clients)--;
                return head;
        }

        // Search for the client to be deleted, keep track of the previous node
        while (temp != NULL && !(temp->addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && temp->addr.sin_port == client_addr.sin_port)) {
                prev = temp;
                temp = temp->next;
        }

        // If client was not present in the list
        if (temp == NULL) return head;

        // Unlink the node from the linked list
        prev->next = temp->next;
        timer_delete(temp->live_timer); // Delete timer
        free(temp);
        (*num_clients)--;
        return head;
}

static void toggleMode(struct ir_udp_server_t *server, DATA_MODE mode)
{
        server->data_mode = (mode == MODE_Y16) ? MODE_Y16 : MODE_Y8_AND_Y16;
}

static ClientNode* isClientExist(ClientNode* head, struct sockaddr_in client_addr) {
        ClientNode* current = head;
        while (current != NULL) {
                if (current->addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && current->addr.sin_port == client_addr.sin_port) {
                        return current;
                }
                current = current->next;
        }
        return NULL;
}

static void sendToAllClients(ClientNode* head, int sockfd, const char* buffer, int buffer_len) {
        int ret = 0, transfer_num, last_trans_bytes;
        ClientNode* current = head;
        transfer_num = buffer_len / MTU;
        last_trans_bytes = buffer_len % MTU;
        while (current != NULL && current->status == STREAM_ON) {
                for (int i = 0; i < transfer_num; ++i) {
                        ret |= sendto(sockfd, buffer + i * MTU, MTU, 0, (const struct sockaddr*)&current->addr, sizeof(current->addr));
                }
                ret |= sendto(sockfd, buffer +  transfer_num * MTU, last_trans_bytes, 0, (const struct sockaddr*)&current->addr, sizeof(current->addr));
                if (ret < 0) {
                        perror("ir udp server error");
                }
                current = current->next;
        }
}

static void sendToClient(int sockfd, struct sockaddr_in client_addr, const char* message) {
        sendto(sockfd, message, strlen(message), 0, (const struct sockaddr*)&client_addr, sizeof(client_addr));
}
