#ifndef __MYSOCK_H
#define __MYSOCK_H

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <net/if.h>
#include <pthread.h>
#include "cJSON.h"
#include <signal.h>
#include "db.h"

typedef struct sockaddr SockAddr;
typedef struct sockaddr_in SockAddrIn;

// 定义服务端的端口
#define SERVER_PORT 8888
// 定义服务端的IP
#define SERVER_IP "192.168.88.135"

#define NUM 100

//客户端信息结构体，传递给工作线程
typedef struct
{
    int sockfd; //客户端socket
    int user_id;//登录后用户的id，-1为未登录
}client_info_t;

//在线用户映射表(索引为用户id，值为socket fd)
extern int online_fd[10001];
extern pthread_mutex_t online_lock;

//定义函数接口
//1.根据给定的ip和port初始化服务端的套接字描述符
int mysock_init(const char* ip,uint16_t port);

//2.根据套接字描述符建立连接，并返回已经连接上的客户端套接字描述符
int mysock_build_connect(int sockfd);

//3.关闭套接字描述符
int mysock_close(int sockfd);

//4.线程执行的任务函数--提供读写服务
void * doService(void*arg);


#endif