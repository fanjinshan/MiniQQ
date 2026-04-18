#include "threadPool.h"

void* task_fun(void* arg)
{
    threadpool_t *pool = (threadpool_t*)arg;
    //循环执行任务
    while(1)
    {
        //添加互斥锁
        pthread_mutex_lock(pool->mutex);

        //循环判断线程池中队列是否为空
        while(pool->queue_num == 0 && pool->running == 1)
        {
            //队列为空，并且程序正在运行
            //为空的条件变量阻塞
            pthread_cond_wait(pool->cond_queue_empty,pool->mutex);
        }
        /*
        当程序执行到此处，说明：
            1.线程池处于停止状态(pool->running==0)
            2.线程池中的任务队列不为空（pool->queue_num != 0）
        */
       //1.线程池处于停止状态
       if(pool->running==0)
       {
            //解除互斥锁
            pthread_mutex_unlock(pool->mutex);
            return (void*)-1;
       }

       //2.线程池中的任务队列不为空
       //获取队列的头结点
       task_t *p = pool->head;
       //任务数量减1
       pool->queue_num--;
       //判断任务数量减去1后队列是否为空
       if(pool->queue_num==0)//为空
       {
            //队列为空，让队列头指针和尾指针指向NULL
            pool->head = pool->tail = NULL;
       }
       else//不为空
       {
            //移动头指针到下一个位置
            pool->head = pool->head->next;
       }
       //如果队列的任务数量不等于最大任务数量，表示没有存满，唤醒满的条件变量
       if(pool->queue_num != pool->queue_max_num)
       {
            //给满的条件变量发信号
            pthread_cond_signal(pool->cond_queue_full);
       }
       //解除互斥锁
       pthread_mutex_unlock(pool->mutex);

       //通过函数指针回调，执行函数
       *p->task(p->arg);
       free(p);
       p = NULL;
    }
    return NULL;
}

//1.线程池的初始化：返回值：0表示正常，-1表示错误
int threadpool_init(threadpool_t* pool,int tnum,int max_size)
{
    //初始化结构体成员
    pool->running = 1;
    pool->thread_num = tnum;
    pool->queue_num = 0;
    pool->queue_max_num = max_size;

    pool->head = NULL;
    pool->tail = NULL;

    //初始化互斥锁变量
    int err_num;
    if((err_num = pthread_mutex_init(pool->mutex,NULL)) != 0)//出错返回错误码
    {
        err(err_num,"pthread_mutex_init");
        return -1;
    }

    //初始化条件变量1
    if((err_num = pthread_cond_init(pool->cond_queue_empty,NULL)) != 0)
    {
        err(err_num,"pthread_cond_init_empty");
        pthread_mutex_destroy(pool->mutex);
        return -1;
    }

    //初始化条件变量2
    if((err_num = pthread_cond_init(pool->cond_queue_full,NULL)) != 0)
    {
        err(err_num,"pthread_cond_init_full");
        pthread_mutex_destroy(pool->mutex);
        pthread_cond_destroy(pool->cond_queue_empty);
        return -1;
    }

    //申请堆内存，用于存储线程id
    pool->ids = (pthread_t*)calloc(tnum,sizeof(pthread_t));
    if(pool->ids == NULL)
    {
        perror("calloc failed");
        pthread_mutex_destroy(pool->mutex);
        pthread_cond_destroy(pool->cond_queue_empty);
        pthread_cond_destroy(pool->cond_queue_full);
        return -1;
    }

    //创建线程
    for(int i = 0;i<tnum;i++)
    {
        pthread_create(pool->ids+i,NULL,task_fun,pool);
        //设置分离属性
        pthread_detach(pool->ids[i]);
    }

    return 0;
}


//2.给任务队列中添加任务
int threadpool_addtask(threadpool_t* pool,void*(*task)(void*),void* arg)
{
    //非空校验
    if(pool == NULL || task == NULL)
    {
        puts("参数不能为空");
        return -1;
    }

    //加互斥锁
    pthread_mutex_lock(pool->mutex);

    //判断线程池中任务队列中任务的数量
    if(pool->queue_num == pool->queue_max_num)
    {
        //队列已经存满
        puts("1.服务器满负荷运行");
        //让满的条件变量阻塞
        pthread_cond_wait(pool->cond_queue_full,pool->mutex);
    }

    puts("2.队列未满");
    //当代码执行到此处时，说明任务队列没满
    //给新的任务申请新的内存空间(新节点)
    task_t *pNew = (task_t*)malloc(sizeof(task_t));
    if(pNew == NULL)
    {
        perror("malloc failed");
        //解除互斥锁
        pthread_mutex_unlock(pool->mutex);
        return -1;
    }
    //初始化结构体成员
    pNew->task = task;
    pNew->arg = arg;
    pNew->next = NULL;
    puts("3.成功初始化新的任务节点");
    //判断任务队列是否为空
    if(pool->head == NULL || pool->tail == NULL)
    {
        //队列为空，没有节点
        puts("4.当前队列为空");
        //新节点作为唯一的节点,同时是头结点和尾节点
        pool->head = pool->tail = pNew;
        //通知条件变量是空的线程结束阻塞
        pthread_cond_signal(pool->cond_queue_empty);
    }
    else
    {
        //队列至少有一个节点
        puts("5.当前队列不为空");
        //将新节点链到原有节点的后面
        pool->tail->next = pNew;
        //新节点作为尾节点
        pool->tail = pNew;
    }
    //任务数量加一
    pool->queue_num++;
    puts("6.添加任务完毕");

    //解除互斥锁
    pthread_mutex_unlock(pool->mutex);

    usleep(10000);
    return 0;
}

//3.线程池的销毁
int threadpool_destroy(threadpool_t* pool)
{
    //先修改运行状态
    pool->running = 0;
    //将所有等待的线程全部唤醒
    pthread_cond_broadcast(pool->cond_queue_empty);
    pthread_cond_broadcast(pool->cond_queue_full);

    //销毁条件变量
    pthread_cond_destroy(pool->cond_queue_empty);
    pthread_cond_destroy(pool->cond_queue_full);
    //销毁互斥锁变量
    pthread_mutex_destroy(pool->mutex);
    //结束所有线程
    for(int i = 0;i < pool->thread_num;i++)
    {
        pthread_cancel(pool->ids[i]);
    }

    //回收线程id的堆内存
    free(pool->ids);
    //避免野指针
    pool->ids = NULL;

    //遍历队列中的节点，销毁所有队列节点
    task_t* cur = pool->head;
    task_t* follow = NULL;
    //循环遍历
    while(cur)
    {
        //尾随指针指向当前节点处
        follow = cur;
        //当前指针移动到下一个节点处
        cur = cur->next;
        //销毁尾随指针指向的节点
        free(follow);
        follow = NULL;
    }
    //将头指针为尾指针置为NULL
    pool->head = pool->tail = NULL;
    //将结构体中其他成员重置默认值
    pool->thread_num = 0;
    pool->queue_num = 0;
    pool->queue_max_num = 0;
    return 0;
}