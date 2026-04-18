#ifndef __PAGE_CONFIG_H
#define __PAGE_CONFIG_H

#define _GNU_SOURCE
#include "lvgl.h"
#include <stdio.h>
#include <time.h>
#include "font_conf.h"
#include "image_conf.h"
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
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <cJSON/cJSON.h>
#include <curl/curl.h>

typedef struct sockaddr SockAddr;
typedef struct sockaddr_in SockAddrIn;

// QianWen:API
// sk-56ce6e81c43f44fc8538f52ffa5b8c02

// #define MONITOR_HOR_RES 1200
// #define MONITOR_VER_RES 700

// 定义服务端的端口
#define SERVER_PORT 8888
// 定义服务端的IP
#define SERVER_IP "192.168.88.135"
//服务器端套接字
extern int sockfd;
extern lv_style_t com_style;
extern lv_obj_t* friend_list_cont;//好友列表容器
extern lv_obj_t *cont_chat;//右侧聊天框底层容器
extern int current_chat_friend_id;//当前聊天好友id
extern lv_obj_t *current_msg_area;//消息区域
extern lv_obj_t* account_info;//本用户账号信息
//当前登录用户名
extern char my_username[32];
//当前时间信息
extern char time_buf[32];
extern int current_chat_type;              // 当前聊天类型(0:私聊，1：群聊)
extern int current_chat_id;               // 存储当前聊天对象ID（好友ID或群ID）

//用于传递请求消息
typedef struct{
    int from_id;
    char from_name[32];
}request_info_t;

typedef struct
{
    int id;
    char name[32];
}friend_info_t;

typedef struct
{
    int count;
    friend_info_t* friends;
}friend_list_t;

// 用于在天气和UI间传递数据
typedef struct
{
    lv_obj_t *weather_cont;
    char *city;
    char *weather;
    int temp;
} weather_result_t;

//天气缓存结构
typedef struct 
{
    char city[32];
    char weather[32];
    int temp;
    int valid;//1表示缓存有效
}weather_cache_t;

//定义HTTP响应数据结构
typedef struct 
{
    char* data;  //存储响应数据的缓冲区
    size_t size; //响应数据的实际大小（累计接收到的数据总大小）
}http_resp_data_t;

//AI结构体
typedef struct
{
    lv_obj_t* msg_area;//当前消息区域指针
    char* reply;//AI回复内容
}ai_result_t;

//群成员缓存
typedef struct
{
    int id;
    char name[32];
}group_info_t;

//历史消息
typedef struct
{
    char* sender;
    char* content;
    char* time;
    bool is_self;
}history_item_t;
//天气缓存变量
extern weather_cache_t g_weather_cache;
extern pthread_mutex_t g_weather_lock;
extern int weather_fetching;//标记是否正在获取天气
extern pthread_mutex_t weather_fetch_lock;
extern int cached_friends_count;//好友数量缓存
// 好友缓存，点击时获取好友名字
extern friend_info_t cached_friends[100];

void obj_font_set(lv_obj_t *obj, FONT_TYPE type, int weight);
void com_style_init(void);
//封装带长度头的发送函数
void send_with_len(int sockfd,const char *data);
//客户端收发线程回调函数
void* network_thread(void* arg);
//初始化登陆界面
void init_page_main(void);
//初始化好友及聊天界面
void init_page_users_chat(void);
//更新好友列表函数
void update_friend_list_ui(friend_list_t* list);
//构建一条消息
void add_message(lv_obj_t* parent,const char* sender,const char *content,bool is_self,const char* time_str);
//获取实时时间
void get_current_time_str(char* buf,size_t size,const char* format);
// A接收到B回传的消息弹窗（待优化）
void show_info_dialog(void *arg);
// base64编码函数
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);
//base64解码函数
char *base64_decode(const char *data, size_t input_length, size_t *output_length);
//处理file_start
void handle_file_start(const char* file_id,const char * filename,int total_chunks);
//处理file_data
void handle_file_data(const char* file_id,int chunk_index,const char* data_b64);
//处理file_end
void handle_file_end(const char* file_id,int success);
//CURL数据接收回调函数
size_t write_callback(void* data,size_t size,size_t nmemb,void* userp);
//获取天气函数
void* weather_thread_func(void*);
//AI线程回调函数
void *ai_thread_func(void *arg);
//更新天气缓存(再天气线程中调用)
void weather_cache_update(const char* city,const char* weather,int temp);
//读取天气缓存(返回0表示无缓存)
int weather_cache_get(char* city,size_t city_sz,char* weather,size_t weather_sz,int *temp);
//更新天气UI
void update_weather_ui(void *arg);
// 更新群聊列表UI
void update_group_list_ui(void *arg);

#endif