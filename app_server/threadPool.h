#ifndef __THREADPOOL_H
#define __THREADPOOL_H

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
#include <errno.h>

//定义任务所需的头文件
typedef struct _task
{
    //任务函数的函数指针变量
    void* (*task)(void*);
    //任务函数执行的参数
    void* arg;
    //下一个任务节点的地址
    struct _task* next;
}task_t;

//线程池相关的结构体
typedef struct
{
    //线程池信息相关的成员
    int running;//线程池中线程的运行状态（1：正在运行，0：没有运行）
    int thread_num;//线程池中线程的数量--服务员的数量
    int queue_num;//任务队列中当前任务的数量---顾客数量
    int queue_max_num;//任务队列的最大容量

    //线程同步相关的成员
    pthread_mutex_t* mutex;//互斥锁
    pthread_cond_t* cond_queue_empty;//条件变量，用于检测任务队列是否为空
    pthread_cond_t* cond_queue_full;//条件变量，用于检测任务队列是否存满

    //线程相关的成员
    pthread_t* ids; //存放线程id的数组首元素地址
    task_t* head;   //队列头的指针
    task_t* tail;   //队列尾的指针
}threadpool_t;

//线程池相关的函数接口
//1.线程池的初始化：返回值：0表示正常，-1表示错误
int threadpool_init(threadpool_t* pool,int tnum,int max_size);

//2.给任务队列中添加任务
int threadpool_addtask(threadpool_t* pool,void*(*task)(void*),void* arg);

//3.线程池的销毁
int threadpool_destroy(threadpool_t* pool);

#endif