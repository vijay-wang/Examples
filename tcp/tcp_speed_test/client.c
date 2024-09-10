#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <getopt.h>

//#define SERVER_IP	"127.0.0.1"
//#define SERVER_PORT	9988
#define MSG_STR		"Hello,server"
char buf[1024 * 32];

void printf_usage(char *progname)
{
	printf("%s usage: \n",progname);
	printf("-i: sepcify server IP address \n");
	printf("-p: sepcify server port \n");
	printf("-h: print this help information \n");
}

int main(int argc, char **argv)
{
	int	connt_fd = 	-1;
	int	rv = 		-1;
	struct	sockaddr_in	serveraddr;
	char	buf[1024];
	char	*serverip;
	int	port;
	int	ch;
	int	index;
	struct	option		opts[] = {
		{"ipaddr", required_argument, NULL, 'i'},
		{"port", required_argument, NULL, 'p'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while((ch = getopt_long(argc, argv, "i:p:h", opts, NULL)) != -1)
	{
		switch(ch)
		{
			case 'i':
				serverip = optarg;
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 'h':
				printf_usage(argv[0]);
				return 0;
		}
	}

	while(1)
	{
		while(argc < 3)
		{
			printf("please input %s [serverip] [port]\n",argv[0]);
			return -1;
		}
		connt_fd = socket(AF_INET, SOCK_STREAM, 0);
		if(connt_fd < 0)
		{
			printf("create connt_fd failure: %s!\n", strerror(errno));
			close(connt_fd);
			return -2;
		}
		printf("create connt_fd[%d] successfully! \n", connt_fd);
		//serverip = argv[1];
		//port = atoi(argv[2]);
		memset(&serveraddr, 0 ,sizeof(serveraddr));
		serveraddr.sin_family =	AF_INET;
		serveraddr.sin_port =	htons(port);
		//inet_pton(AF_INET, serverip, &serveraddr.sin_addr);
		//printf("======%s=====\n", serverip);
		inet_aton(serverip, &serveraddr.sin_addr);	
		if(connect(connt_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
		{
			printf("connect to server[%s:%d] faiture : %s!\n",serverip, port, strerror(errno));
			close(connt_fd);
			return -3;
		}
		printf("connect to server[%s:%d] successfully!\n", serverip, port);
		while(1)
		{
			//if(write(connt_fd, MSG_STR, strlen(MSG_STR)) < 0)
			if(write(connt_fd, buf, sizeof(buf)) < 0)
			{
				printf("write the data to buf failture: %s", strerror(errno));
				goto cleanup;
			}	

			//memset(buf, 0, sizeof(buf));
			//rv = read(connt_fd, buf ,sizeof(buf));
			//if(rv < 0)
			//{
			//	printf("read the data from server failure: %s", strerror(errno));
			//	goto cleanup;
			//}

			//if(rv == 0)
			//{
			//	printf("disconnect the server....\n");
			//	goto cleanup;
			//}

			//printf("read %d bytes from server : '%s' \n", rv, buf);
			//sleep(1);
		}

cleanup:
		close(connt_fd);
	}
}

