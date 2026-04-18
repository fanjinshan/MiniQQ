/*
TCP网络通讯客户端
实现步骤：
    1.创建socket描述符----socket
    2.连接服务器----connect
    3.发送消息给服务器----send
    4.接收服务器响应----recv
    5.关闭套接字----close
*/

#include "./pages/page_config.h"

extern void lv_port_disp_init(bool is_disp_orientation);
extern void lv_port_indev_init(void);
int sockfd = -1;
char my_username[32] = "";//当前用户账号


int main(int argc, const char *argv[])
{
    //libcurl初始化
    curl_global_init(CURL_GLOBAL_DEFAULT);
    //注册清理函数
    atexit(curl_global_cleanup);
    // LVGL框架初始化
    lv_init();
    // 字库引擎初始化
    font_init();
    // LVGL显示屏幕初始化
    lv_port_disp_init(true);
    // LVGL输入设备初始化
    lv_port_indev_init();
    //登陆界面
    init_page_main();
    //聊天界面
    init_page_users_chat();
    
    // 1.创建socket描述符----socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket failed");
        return -1;
    }
    // 2.连接服务器----connect
    SockAddrIn addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    int r = connect(sockfd, (SockAddr *)&addr, sizeof(addr));
    if (r == -1)
    {
        perror("connect failed");
        close(sockfd);
        return -1;
    }

    // 连接成功，创造收发线程
    pthread_t tid;
    pthread_create(&tid, NULL, network_thread, NULL);
    pthread_detach(tid);

    // LVGL主循环
    while (1)
    {
        lv_task_handler();
        // 延时，保证cpu占有率不会过高
        usleep(5000);
    }

    // 5.关闭套接字----close
    close(sockfd);
    return 0;
}