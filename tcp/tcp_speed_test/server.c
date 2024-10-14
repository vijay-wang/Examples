#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <getopt.h>

//#define PORT 9988
void printf_usage(char *progname)
{
	printf("%s usage: \n",progname);
	printf("-p: sepcify server port \n");
	printf("-h(--help): print this help information \n");
}

int main(int argc, char **argv)
{
	int			socket_fd = -1, client_fd = -1;
	int			rv = -1, rv1 = -1;
	struct sockaddr_in 	serveraddr, clientaddr;
	char			buf[1024 * 64];
	socklen_t		clientaddr_len = sizeof(struct sockaddr);
	int			port;
	int			ch;
	int			index;
	struct	option		opts[] = {
		{"port", required_argument, NULL, 'p'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while((ch = getopt_long(argc, argv, "p:h", opts, NULL)) != -1)
	{
		switch(ch)
		{
			case 'p':
				port = atoi(optarg);
				break;

			case 'h':
				printf_usage(argv[0]);
				return 0;
                        default:
				printf_usage(argv[0]);
		}
	}

	while(argc < 2)
	{
                printf_usage(argv[0]);
		return -1;
	}

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(socket_fd < 0)
	{
		printf("create socket failure : %s \n", strerror(errno));
		return -2;
	}

	//port = atoi(argv[1]);
	serveraddr.sin_family =	AF_INET;
	serveraddr.sin_port =	htons(port); //操作系统的字节序为小端，所以需要转换成网络大端字节序
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);	//ip地址为4个字节，用htonl；INADDR_ANY（实际上是0）可以自动选择服务器的ip，使用有线网卡就用有线ip，使用无线网卡就用无线ip。
	if(bind(socket_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	{
		printf("create server failure : %s \n", strerror(errno));
		return -3;
	}
	printf("socket [%d] bind on port[%d] for all IP address succesfully !", socket_fd, port);

	listen(socket_fd, 10); //监听socket，等待客户端连接，10为正在连接服务端的最大数，超过就不能连入服务器。

	while(1)
	{
		printf("\n Start waiting and accept new client connect...\n");
		client_fd = accept(socket_fd, (struct sockaddr *)&clientaddr, &clientaddr_len); //一般来说不关心addr的长度大小，这个数据不重要；地址为ipv4类型，需要保持一致；内核返回的值为新的socket描述符，用来服务某个接入的客户端。
		if(client_fd < 0)
		{
			printf("accept new socket failture : %s\n", strerror(errno));
			return -4;
		}
		printf("accept new client[%d] socket[%s:%d]\n",client_fd, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));//inet_ntoa函数为整型转字符型，ntohs函数为大端转小端。
		while(1)
		{
			memset(buf, 0, sizeof(buf));
			rv = read(client_fd, buf, sizeof(buf));
			if(rv < 0) {
				printf("read data from client [%d] failure : %s\n", client_fd, strerror(errno));
				close(client_fd);
				break;
			} else if(rv == 0) {
				printf("%d \n",rv);
				printf("client [%d] disconnected \n", client_fd);
				close(client_fd);
				break;	
			}
			printf("read %d bytes data from client[%d] and the data is :\n", rv , client_fd);
                        for (int i = 0; i < rv; i++)
                                printf("<0x%x>", buf[i]);
                        printf("\n");
		}
	}
	close(socket_fd);
}
