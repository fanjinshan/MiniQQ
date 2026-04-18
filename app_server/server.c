/*
TCP网络通信的服务端
实现步骤：
    1.创建socket描述符----socket
    2.绑定自己的地址信息----bind
    3.监听客户端的连接----listen
    4.接收客户端的连接----accept
    5.接收客户端发送来的数据----recv
    6.给客户端响应数据----send
    7.关闭套接字
*/
#include "mysock.h"
#include "threadPool.h"


int main(int argc, const char *argv[])
{
    //signal(SIGPIPE, SIG_IGN);
    // 一、调用自定义函数，初始化服务端套接字
    int s_sockfd = mysock_init(SERVER_IP, SERVER_PORT);
    if (s_sockfd == -1)
    {
        return -1;
    }

    //1.定义线程池结构体变量
    threadpool_t pool;
    //给结构体成员初始化
    pthread_mutex_t mutex;
    pthread_cond_t cond_empty;
    pthread_cond_t cond_full;
    pool.mutex = &mutex;
    pool.cond_queue_empty = &cond_empty;
    pool.cond_queue_full = &cond_full;
    //2.初始化线程池
    if(threadpool_init(&pool,10,10) == -1)
    {
        puts("线程池初始化失败");
        close(s_sockfd);
        return -1;
    }

    
    int fds[NUM] = {0};
    // 二、循环接收客户端的连接
    for (int i = 0; i < NUM; i++)
    {
        // 二、接收客户端连接并返回客户端描述符
        fds[i] = mysock_build_connect(s_sockfd);
        if (fds[i] == -1)
        {
            return -1;
        }

        // client_info_t* info = (client_info_t*)malloc(sizeof(client_info_t));
        // info->sockfd = fds[i];
        // info->user_id = -1;
        //当客户端已经和服务端建立连接，就将线程要执行的任务函数和客户端的描述符添加到线程池中
        threadpool_addtask(&pool,doService,fds+i);
    }

    threadpool_destroy(&pool);

    return 0;
}