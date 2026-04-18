#include "page_config.h"

// 添加（好友还是群聊）选项
lv_obj_t *msgbox_add = NULL;
lv_style_t com_style;
lv_obj_t *screen = NULL;
lv_obj_t *friend_list_cont = NULL; // 好友列表容器
lv_obj_t *group_list_cont = NULL;  // 群聊列表容器
lv_obj_t *cont_chat = NULL;        // 右侧聊天框底层容器
// 聊天状态
int current_chat_friend_id = -1;   // 当前聊天好友id
lv_obj_t *current_msg_area = NULL; // 当前聊天区域
lv_obj_t *account_info = NULL;     // 本账号信息
char time_buf[32];                 // 时间信息
static char last_msg_time[32] = "";
int current_chat_type = 0;              // 当前聊天类型(0:私聊，1：群聊)
int current_chat_id = -1;               // 存储当前聊天对象ID（好友ID或群ID）
static group_info_t cached_groups[100]; // 缓存当前用户的群聊
static int cached_groups_count = 0;     // 缓存当前用户群聊数量
// 好友缓存，点击时获取好友名字
friend_info_t cached_friends[100];
int cached_friends_count = 0;
static struct send_btn_data *current_btn_data = NULL;
// base64编码表
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// 文件发送窗口
static lv_obj_t *file_win = NULL;
static lv_obj_t *file_path_ta = NULL;
static lv_obj_t *cont_information = NULL;
static lv_obj_t *keyboard_chinese = NULL; // 聊天框中文输入法键盘
lv_obj_t *weather_cont = NULL;            // 天气容器
// 天气缓存相关变量
weather_cache_t g_weather_cache = {0};
pthread_mutex_t g_weather_lock = PTHREAD_MUTEX_INITIALIZER;
int weather_fetching = 0; // 标记是否正在获取天气
pthread_mutex_t weather_fetch_lock = PTHREAD_MUTEX_INITIALIZER;

// 消息发送按钮数据结构
struct send_btn_data
{
    lv_obj_t *ta;
    int friend_id;
};

// 文件接收管理
typedef struct file_transfer
{
    char file_id[64];
    char filename[256];
    char from_name[32]; // 发送者名称
    int msg_type;       // 0：普通文件；1:语音
    int duration;//语音时长(秒)
    FILE *fp;
    int total_chunks;
    int received_chunks;
    struct file_transfer *next;
} file_transfer_t;

// 语音消息显示结构(传递给UI线程)
typedef struct
{
    char *file_path;//音频文件完整路径
    char *sender;
    char *time;
    int is_self;
    int chat_id;   // 好友ID或群ID
    int chat_type; // 0：私聊，1：群聊
    int duration;//语音时长(秒)
} voice_msg_t;

static file_transfer_t *transfers = NULL; // 文件记录链表头指针
static pthread_mutex_t transfer_lock = PTHREAD_MUTEX_INITIALIZER;
#define CHUNK_SIZE (16 * 1024) // 16KB文件块大小

static pthread_t voice_record_thread = 0;                      // 录音线程ID
static int voice_recording = 0;                                // 录音状态标志：1:正在录音，0：停止
static char voice_file_path[256];                              // 临时录音文件路径
static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER; // 保护录音状态互斥锁

// 封装带长度头的发送函数
void send_with_len(int sockfd, const char *data)
{
    if (!data)
        return;
    uint32_t len = strlen(data);
    uint32_t net_len = htonl(len);
    // 发送长度头
    send(sockfd, &net_len, 4, 0);
    // 发送数据
    send(sockfd, data, len, 0);
}

// base64编码函数
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length)
{
    // input_length + 2,向上取整，以三字节为一块，计算有多少块，每三个字节有24位，每6位是一个base64字符，所以块数*4得到base64字符数
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = (char *)malloc(*output_length + 1);
    if (!encoded_data)
        return NULL;

    for (size_t i = 0, j = 0; i < input_length;)
    {
        uint32_t octet_a = i < input_length ? data[i++] : 0; // 循环，每次处理三个字节
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c; // 将三个字节拼成24位数字

        encoded_data[j++] = b64_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = b64_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = b64_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = b64_table[(triple) & 0x3F];
    }
    // 填充
    for (size_t i = 0; i < (3 - input_length % 3) % 3; i++)
    {
        encoded_data[*output_length - 1 - i] = '=';
    }
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

// base64解码函数
char *base64_decode(const char *data, size_t input_length, size_t *output_length)
{
    if (input_length % 4 != 0)
        return NULL;

    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=')
        (*output_length)--;
    if (data[input_length - 2] == '=')
        (*output_length)--;

    unsigned char *decoded_data = malloc(*output_length);
    if (!decoded_data)
        return NULL;

    for (size_t i = 0, j = 0; i < input_length;)
    {
        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : strchr(b64_table, data[i++]) - b64_table;
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : strchr(b64_table, data[i++]) - b64_table;
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : strchr(b64_table, data[i++]) - b64_table;
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : strchr(b64_table, data[i++]) - b64_table;
        uint32_t triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;

        if (j < *output_length)
            decoded_data[j++] = (triple >> 16) & 0xFF;
        if (j < *output_length)
            decoded_data[j++] = (triple >> 8) & 0xFF;
        if (j < *output_length)
            decoded_data[j++] = triple & 0xFF;
    }
    return decoded_data;
}

// 更新天气缓存(再天气线程中调用)
void weather_cache_update(const char *city, const char *weather, int temp)
{
    pthread_mutex_lock(&g_weather_lock);
    strncpy(g_weather_cache.city, city, sizeof(g_weather_cache.city) - 1);
    g_weather_cache.city[sizeof(g_weather_cache.city) - 1] = '\0';
    strncpy(g_weather_cache.weather, weather, sizeof(g_weather_cache.weather) - 1);
    g_weather_cache.weather[sizeof(g_weather_cache.weather) - 1] = '\0';
    g_weather_cache.temp = temp;
    g_weather_cache.valid = 1;
    pthread_mutex_unlock(&g_weather_lock);
}

// 读取天气缓存(返回0表示无缓存)
int weather_cache_get(char *city, size_t city_sz, char *weather, size_t weather_sz, int *temp)
{
    pthread_mutex_lock(&g_weather_lock);
    int valid = g_weather_cache.valid;
    if (valid)
    {
        strncpy(city, g_weather_cache.city, city_sz - 1);
        city[city_sz - 1] = '\0';
        strncpy(weather, g_weather_cache.weather, weather_sz - 1);
        weather[weather_sz - 1] = '\0';
        *temp = g_weather_cache.temp;
    }
    pthread_mutex_unlock(&g_weather_lock);
    return valid;
}

// 设置通用样式
void com_style_init()
{
    lv_style_init(&com_style);
    if (!lv_style_is_empty(&com_style))
    {
        lv_style_reset(&com_style);
    }
    lv_style_set_border_width(&com_style, 0);
    lv_style_set_outline_width(&com_style, 0);
    lv_style_set_pad_all(&com_style, 0);
    lv_style_set_radius(&com_style, 0);
    lv_style_set_bg_color(&com_style, lv_color_hex(0x000000));
}

// 设置字体
void obj_font_set(lv_obj_t *obj, FONT_TYPE type, int weight)
{
    lv_font_t *font = get_font(type, weight);
    if (font != NULL)
    {
        lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    }
}

// 获取时间函数
void get_current_time_str(char *buf, size_t size, const char *format)
{
    time_t timer;
    time(&timer);
    struct tm timeinfo;
    localtime_r(&timer, &timeinfo);
    strftime(buf, size, format, &timeinfo);
}

static void timer_cb_func(lv_timer_t *timer)
{
    // 获取当前时间
    get_current_time_str(time_buf, sizeof(time_buf), "%H : %M");
}

// 构建消息对象
void add_message(lv_obj_t *parent, const char *sender, const char *content, bool is_self, const char *time_str)
{
    // 当当前时间和上一次的时间不同时
    if (strcmp(time_buf, last_msg_time) != 0)
    {
        lv_obj_t *cont_time = lv_obj_create(parent);
        lv_obj_add_style(cont_time, &com_style, 0);
        lv_obj_set_style_bg_color(cont_time, lv_color_hex(0x222222), 0);
        lv_obj_set_size(cont_time, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(cont_time, 10, 0); // 内边距

        strcpy(last_msg_time, time_buf);

        // 创建时间标签
        lv_obj_t *label_time = lv_label_create(cont_time);
        lv_obj_set_style_text_color(label_time, lv_color_hex(0xffffff), 0);
        lv_label_set_text(label_time, time_buf);
        lv_obj_align(label_time, LV_ALIGN_CENTER, 0, 0);
    }
    // 创建消息气泡上方发送者名称容器
    lv_obj_t *label_cont = lv_obj_create(parent);
    lv_obj_add_style(label_cont, &com_style, 0);
    lv_obj_set_style_bg_color(label_cont, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(label_cont, 10, 0);
    lv_obj_set_size(label_cont, LV_PCT(100), 50);
    lv_obj_set_style_pad_all(label_cont, 20, 0); // 内边距
    lv_obj_clear_flag(label_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(label_cont, false, 0);
    // 发送者名称标签
    lv_obj_t *label_sendName = lv_label_create(label_cont);
    obj_font_set(label_sendName, FONT_TYPE_CN, 18);
    lv_obj_set_style_text_color(label_sendName, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_sendName, sender);
    // 创建消息气泡容器
    lv_obj_t *out_cont = lv_obj_create(parent);
    lv_obj_add_style(out_cont, &com_style, 0);
    lv_obj_set_style_bg_color(out_cont, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(out_cont, 10, 0);
    lv_obj_set_size(out_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(out_cont, 0, 0); // 内边距

    lv_obj_t *msg_cont = lv_obj_create(out_cont);
    lv_obj_add_style(msg_cont, &com_style, 0);
    lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0x3b3b3b), 0);
    lv_obj_set_style_radius(msg_cont, 10, 0);
    lv_obj_set_size(msg_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(msg_cont, 10, 0); // 内边距

    lv_obj_t *msg_label = lv_label_create(msg_cont);
    obj_font_set(msg_label, FONT_TYPE_CN, 18);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP); // 当文本超出其宽度时，换行处理
    lv_obj_set_width(msg_label, 400);                      // 消息框最大宽度
    lv_obj_set_size(msg_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text(msg_label, content);

    // 根据是否自己来设置文本框位置
    if (is_self)
    {
        lv_obj_set_style_text_color(msg_label, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0x97F097), 0);
        lv_obj_align(msg_cont, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_align(label_sendName, LV_ALIGN_RIGHT_MID, 0, 10);
    }
    else
    {
        lv_obj_set_style_text_color(msg_label, lv_color_hex(0x5A4B38), 0);
        lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0xF5EEDC), 0);
        lv_obj_align(msg_cont, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_align(label_sendName, LV_ALIGN_LEFT_MID, 0, 10);
    }
}

// 添加好友窗口中发送按钮回调函数
static void label_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    const char *friendName = lv_textarea_get_text(ta);
    if (strlen(friendName) == 0)
    {
        printf("请输入用户名\n");
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "请输入好友账号", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN_LIGHT, 20);
        lv_obj_center(msg);
        return;
    }
    printf("用户名:%s\n", friendName);
    // 构造发送给服务器的请求
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "add_friend");
    cJSON_AddStringToObject(req, "friend_username", friendName);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);

    // 关闭对话框
    lv_obj_t *content = lv_obj_get_parent(ta);
    lv_obj_t *win = lv_obj_get_parent(content);
    lv_obj_del(win);
    lv_obj_del(msgbox_add);
}

// 添加好友按钮回调事件
static void friend_event_cb(lv_event_t *e)
{
    printf("添加好友\n");
    // 创建新窗口
    lv_obj_t *win = lv_win_create(lv_scr_act(), 40);
    lv_obj_set_style_radius(win, 15, 0);
    lv_obj_set_size(win, 300, 300);
    lv_obj_align(win, LV_ALIGN_LEFT_MID, 200, 50);
    lv_win_add_title(win, "添加好友");
    lv_obj_t *content = lv_win_get_content(win);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x2c313c), 0);
    obj_font_set(win, FONT_TYPE_CN, 20);
    // 将窗口移到顶层
    lv_obj_move_foreground(win);

    // 输入框
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_align(ta, LV_ALIGN_CENTER, 0, -50);
    lv_textarea_set_placeholder_text(ta, "输入好友账号");
    lv_textarea_set_one_line(ta, true);
    obj_font_set(ta, FONT_TYPE_CN, 20);

    lv_obj_t *btn = lv_btn_create(content);
    lv_obj_set_size(btn, 80, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, label_btn_event_cb, LV_EVENT_CLICKED, ta);
    lv_obj_t *label_btn = lv_label_create(btn);
    lv_obj_center(label_btn);
    obj_font_set(label_btn, FONT_TYPE_CN, 20);
    lv_label_set_text(label_btn, "发送");
}

// 创建群聊确认回调
static void create_group_confirm_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    const char *name = lv_textarea_get_text(ta);
    if (strlen(name) == 0)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "群名称不能为空", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN_LIGHT, 20);
        lv_obj_center(msg);
        return;
    }
    // 构造发送给服务器的请求
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "create_group");
    cJSON_AddStringToObject(req, "name", name);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);

    // 关闭对话框
    lv_obj_t *content = lv_obj_get_parent(ta);
    lv_obj_t *win = lv_obj_get_parent(content);
    lv_obj_del(win);
    lv_obj_del(msgbox_add);
}

// 创建群聊按钮回调事件
static void create_group_event_cb(lv_event_t *e)
{
    printf("创建群聊\n");
    // 创建新窗口
    lv_obj_t *win = lv_win_create(lv_scr_act(), 40);
    lv_obj_set_style_radius(win, 15, 0);
    lv_obj_set_size(win, 300, 300);
    lv_obj_align(win, LV_ALIGN_LEFT_MID, 200, 50);
    lv_win_add_title(win, "创建群聊");
    lv_obj_t *content = lv_win_get_content(win);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x2c313c), 0);
    obj_font_set(win, FONT_TYPE_CN, 20);
    // 将窗口移到顶层
    lv_obj_move_foreground(win);

    // 输入框
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_align(ta, LV_ALIGN_CENTER, 0, -50);
    lv_textarea_set_placeholder_text(ta, "输入群名称");
    lv_textarea_set_one_line(ta, true);
    obj_font_set(ta, FONT_TYPE_CN, 20);

    lv_obj_t *btn = lv_btn_create(content);
    lv_obj_set_size(btn, 80, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, create_group_confirm_cb, LV_EVENT_CLICKED, ta);
    lv_obj_t *label_btn = lv_label_create(btn);
    lv_obj_center(label_btn);
    obj_font_set(label_btn, FONT_TYPE_CN, 20);
    lv_label_set_text(label_btn, "创建");
}

// 加入群聊按钮回调
static void join_group_confirm_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    const char *name = lv_textarea_get_text(ta);
    if (strlen(name) == 0)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "群名称不能为空", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN_LIGHT, 18);
        lv_obj_center(msg);
        return;
    }
    // 构造发送给服务器的请求
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "join_group_by_name");
    cJSON_AddStringToObject(req, "group_name", name);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);

    // 关闭对话框
    lv_obj_t *content = lv_obj_get_parent(ta);
    lv_obj_t *win = lv_obj_get_parent(content);
    lv_obj_del(win);
    lv_obj_del(msgbox_add);
}

// 添加群聊按钮回调事件
static void join_group_event_cb(lv_event_t *e)
{
    printf("添加群聊\n");
    // 创建新窗口
    lv_obj_t *win = lv_win_create(lv_scr_act(), 40);
    lv_obj_set_style_radius(win, 15, 0);
    lv_obj_set_size(win, 300, 300);
    lv_obj_align(win, LV_ALIGN_LEFT_MID, 200, 50);
    lv_win_add_title(win, "添加群聊");
    lv_obj_t *content = lv_win_get_content(win);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x2c313c), 0);
    obj_font_set(win, FONT_TYPE_CN, 20);
    // 将窗口移到顶层
    lv_obj_move_foreground(win);

    // 输入框
    lv_obj_t *ta = lv_textarea_create(content);
    lv_obj_align(ta, LV_ALIGN_CENTER, 0, -50);
    lv_textarea_set_placeholder_text(ta, "输入群名称");
    lv_textarea_set_one_line(ta, true);
    obj_font_set(ta, FONT_TYPE_CN, 20);

    lv_obj_t *btn = lv_btn_create(content);
    lv_obj_set_size(btn, 80, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, join_group_confirm_cb, LV_EVENT_CLICKED, ta);
    lv_obj_t *label_btn = lv_label_create(btn);
    lv_obj_center(label_btn);
    obj_font_set(label_btn, FONT_TYPE_CN, 20);
    lv_label_set_text(label_btn, "加入");
}

// 添加按钮回调事件
static void addImg_event_cb(lv_event_t *e)
{
    printf("点击添加按钮\n");
    // 点击添加按钮后选择添加好友还是添加群聊
    msgbox_add = lv_obj_create(screen);
    lv_obj_add_style(msgbox_add, &com_style, 0);
    lv_obj_set_style_bg_color(msgbox_add, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(msgbox_add, 8, 0);
    lv_obj_set_size(msgbox_add, 130, 105);
    lv_obj_align(msgbox_add, LV_ALIGN_TOP_LEFT, 270, 150);

    // 添加好友
    // 底层容器
    lv_obj_t *add_friend = lv_obj_create(msgbox_add);
    lv_obj_clear_flag(add_friend, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_add_style(add_friend, &com_style, 0);
    lv_obj_set_style_bg_color(add_friend, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(add_friend, 8, 0);
    lv_obj_set_size(add_friend, 130, 35);
    lv_obj_align(add_friend, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(add_friend, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(add_friend, friend_event_cb, LV_EVENT_CLICKED, NULL);
    // 图标
    lv_obj_t *cont_add_friend = lv_obj_create(add_friend);
    lv_obj_add_style(cont_add_friend, &com_style, 0);
    lv_obj_set_size(cont_add_friend, 35, 35);
    lv_obj_set_style_clip_corner(cont_add_friend, true, 0);     // 启用裁剪
    lv_obj_set_style_bg_opa(cont_add_friend, LV_OPA_TRANSP, 0); // 背景透明
    lv_obj_clear_flag(cont_add_friend, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_align(cont_add_friend, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(cont_add_friend, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *img_add_friend = lv_img_create(cont_add_friend);
    lv_img_set_src(img_add_friend, GET_IMAGE_PATH("addfriend.png"));
    lv_img_set_zoom(img_add_friend, 35);
    lv_obj_align(img_add_friend, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(img_add_friend, LV_OBJ_FLAG_EVENT_BUBBLE);
    // 标签
    lv_obj_t *label_add_friend = lv_label_create(add_friend);
    obj_font_set(label_add_friend, FONT_TYPE_CN, 18);
    lv_obj_set_style_text_color(label_add_friend, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_add_friend, "添加好友");
    lv_obj_align(label_add_friend, LV_ALIGN_RIGHT_MID, -20, -4);
    lv_obj_add_flag(label_add_friend, LV_OBJ_FLAG_EVENT_BUBBLE);

    // 创建群聊
    // 底层容器
    lv_obj_t *create_group = lv_obj_create(msgbox_add);
    lv_obj_clear_flag(create_group, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_add_style(create_group, &com_style, 0);
    lv_obj_set_style_bg_color(create_group, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(create_group, 8, 0);
    lv_obj_set_size(create_group, 130, 35);
    lv_obj_align(create_group, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_add_flag(create_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(create_group, create_group_event_cb, LV_EVENT_CLICKED, NULL);
    // 图标
    lv_obj_t *cont_create_group = lv_obj_create(create_group);
    lv_obj_add_style(cont_create_group, &com_style, 0);
    lv_obj_set_size(cont_create_group, 35, 35);
    lv_obj_set_style_clip_corner(cont_create_group, true, 0);     // 启用裁剪
    lv_obj_set_style_bg_opa(cont_create_group, LV_OPA_TRANSP, 0); // 背景透明
    lv_obj_clear_flag(cont_create_group, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_align(cont_create_group, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *img_create_cont_group = lv_img_create(cont_create_group);
    lv_img_set_src(img_create_cont_group, GET_IMAGE_PATH("addgroup.png"));
    lv_img_set_zoom(img_create_cont_group, 35);
    lv_obj_align(img_create_cont_group, LV_ALIGN_CENTER, 0, 0);
    // 标签
    lv_obj_t *label_create_group = lv_label_create(create_group);
    obj_font_set(label_create_group, FONT_TYPE_CN, 18);
    lv_obj_set_style_text_color(label_create_group, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_create_group, "创建群聊");
    lv_obj_align(label_create_group, LV_ALIGN_RIGHT_MID, -20, -4);

    // 添加群聊
    // 底层容器
    lv_obj_t *add_group = lv_obj_create(msgbox_add);
    lv_obj_clear_flag(add_group, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_add_style(add_group, &com_style, 0);
    lv_obj_set_style_bg_color(add_group, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(add_group, 8, 0);
    lv_obj_set_size(add_group, 130, 35);
    lv_obj_align(add_group, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(add_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(add_group, join_group_event_cb, LV_EVENT_CLICKED, NULL);
    // 图标
    lv_obj_t *cont_add_group = lv_obj_create(add_group);
    lv_obj_add_style(cont_add_group, &com_style, 0);
    lv_obj_set_size(cont_add_group, 35, 35);
    lv_obj_set_style_clip_corner(cont_add_group, true, 0);     // 启用裁剪
    lv_obj_set_style_bg_opa(cont_add_group, LV_OPA_TRANSP, 0); // 背景透明
    lv_obj_clear_flag(cont_add_group, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_align(cont_add_group, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *img_join_cont_group = lv_img_create(cont_add_group);
    lv_img_set_src(img_join_cont_group, GET_IMAGE_PATH("addgroup.png"));
    lv_img_set_zoom(img_join_cont_group, 35);
    lv_obj_align(img_join_cont_group, LV_ALIGN_CENTER, 0, 0);
    // 标签
    lv_obj_t *label_add_group = lv_label_create(add_group);
    obj_font_set(label_add_group, FONT_TYPE_CN, 18);
    lv_obj_set_style_text_color(label_add_group, lv_color_hex(0xffffff), 0);
    lv_label_set_text(label_add_group, "加入群聊");
    lv_obj_align(label_add_group, LV_ALIGN_RIGHT_MID, -20, -4);
}

// 聊天框的发送消息按钮回调函数(私聊，群聊)
static void send_btn_cb(lv_event_t *e)
{
    struct send_btn_data *data = lv_event_get_user_data(e);
    lv_obj_t *ta = data->ta;
    // int friend_id = data->friend_id;

    const char *msg_text = lv_textarea_get_text(ta);
    if (strlen(msg_text) == 0)
        return;

    if (current_chat_type == 0) // 私聊
    {
        // 本地显示自己发送的消息
        add_message(current_msg_area, my_username, msg_text, true, time_buf);

        if (current_chat_id == 0) // AI助手
        {
            // 发送给AI：启动AI专用线程
            char *user_msg = strdup(msg_text);
            pthread_t tid;
            pthread_create(&tid, NULL, ai_thread_func, user_msg);
            pthread_detach(tid);
        }
        else
        {
            // 发送给服务器
            cJSON *req = cJSON_CreateObject();
            cJSON_AddStringToObject(req, "type", "private_msg");
            cJSON_AddNumberToObject(req, "to_id", current_chat_id);
            cJSON_AddStringToObject(req, "content", msg_text);
            char *req_str = cJSON_PrintUnformatted(req);
            send_with_len(sockfd, req_str);
            free(req_str);
            cJSON_Delete(req);
        }
    }
    else // 群聊
    {
        // 显示自己发送的消息
        add_message(current_msg_area, my_username, msg_text, true, time_buf);

        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "type", "group_msg");
        cJSON_AddNumberToObject(req, "group_id", current_chat_id);
        cJSON_AddStringToObject(req, "content", msg_text);
        char *req_str = cJSON_PrintUnformatted(req);
        send_with_len(sockfd, req_str);
        free(req_str);
        cJSON_Delete(req);
    }

    lv_textarea_set_text(ta, "");
}

// 右侧聊天框初始状态
static void init_chat_area()
{
    // 聊天框
    cont_chat = lv_obj_create(screen);
    lv_obj_add_style(cont_chat, &com_style, 0);
    lv_obj_set_style_bg_color(cont_chat, lv_color_hex(0xffffff), 0);
    lv_obj_set_size(cont_chat, 800, 700);
    lv_obj_align(cont_chat, LV_ALIGN_RIGHT_MID, 0, 0);
    // 聊天框背景图
    lv_obj_t *bg_chat = lv_img_create(cont_chat);
    lv_img_set_zoom(bg_chat, 100);
    lv_obj_set_style_img_opa(bg_chat, 150, 0);
    lv_img_set_src(bg_chat, GET_IMAGE_PATH("qqImg.png"));
    lv_obj_center(bg_chat);

    current_chat_friend_id = -1;
    current_msg_area = NULL;
}

// 私聊界面关闭按钮回调函数
static void close_btn_cb(lv_event_t *e)
{
    if (cont_chat)
    {
        lv_obj_del(cont_chat);
        cont_chat = NULL;
    }

    if (keyboard_chinese)
    {
        lv_obj_del(keyboard_chinese);
        keyboard_chinese = NULL;
    }

    if (file_win)
    {
        lv_obj_del(file_win);
        file_win = NULL;
        file_path_ta = NULL;
    }

    init_chat_area();
}

// 实际发送文件
static void send_file(const char *filepath, int to_id,int duration)
{
    // 打印十六进制，检查隐藏字符
    printf("send_file: filepath hex = ");
    size_t len = strlen(filepath);
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", (unsigned char)filepath[i]);
    }
    printf(" (len=%zu)\n", len);
    printf("send_file: filepath string = '%s'\n", filepath); // 加上引号便于观察

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "无法打开文件：%s\n错误：%s", filepath, strerror(errno));
        printf("%s\n", err_msg); // 控制台输出
        lv_obj_t *msg = lv_msgbox_create(NULL, "错误", err_msg, NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 18);
        lv_obj_center(msg);
        return;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long total_size = ftell(fp);
    rewind(fp);

    if (total_size > 100 * 1024 * 1024) // 100mb
    {
        fclose(fp);
        lv_obj_t *msg = lv_msgbox_create(NULL, "错误", "文件过大(>100MB)", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 18);
        lv_obj_center(msg);
        return;
    }

    // 生成唯一file_id
    char file_id[64];
    snprintf(file_id, sizeof(file_id), "%ld_%d", time(NULL), rand());

    int total_chunks = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // 发送file_start
    cJSON *start = cJSON_CreateObject();
    cJSON_AddStringToObject(start, "type", "file_start");
    cJSON_AddNumberToObject(start, "to_id", to_id);
    if (current_chat_type == 1) // 群聊
    {
        cJSON_AddNumberToObject(start, "is_group", 1);
    }
    cJSON_AddStringToObject(start, "file_id", file_id);
    // 提取纯文件名，不含路径
    cJSON_AddStringToObject(start, "filename", strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : filepath);
    // 判断是否为语音文件(以voice_开头且后缀为.wav)
    if (strstr(filepath, "voice_") != NULL && strstr(filepath, ".wav") != NULL)
    {
        cJSON_AddStringToObject(start, "msg_type", "voice"); // 添加消息类型字段
        cJSON_AddNumberToObject(start,"duration",duration);//语音时长
    }
    cJSON_AddNumberToObject(start, "total_size", total_size);
    cJSON_AddNumberToObject(start, "total_chunks", total_chunks);
    char *start_str = cJSON_PrintUnformatted(start);
    send_with_len(sockfd, start_str);
    free(start_str);
    cJSON_Delete(start);

    unsigned char *buf = malloc(CHUNK_SIZE);
    for (int i = 0; i < total_chunks; i++)
    {
        int read_len = fread(buf, 1, CHUNK_SIZE, fp);
        if (read_len <= 0)
            break;

        size_t encoded_len;
        char *encoded = base64_encode(buf, read_len, &encoded_len);
        if (!encoded)
            break;

        cJSON *chunk = cJSON_CreateObject();
        cJSON_AddStringToObject(chunk, "type", "file_data");
        cJSON_AddNumberToObject(chunk, "to_id", to_id);
        if (current_chat_type == 1)
        {
            cJSON_AddNumberToObject(chunk, "is_group", 1);
        }
        cJSON_AddStringToObject(chunk, "file_id", file_id);
        cJSON_AddNumberToObject(chunk, "chunk_index", i);
        cJSON_AddStringToObject(chunk, "data", encoded);
        char *chunk_str = cJSON_PrintUnformatted(chunk);
        send_with_len(sockfd, chunk_str);
        free(chunk_str);
        cJSON_Delete(chunk);
        free(encoded);

        usleep(10000); // 稍微延时，避免阻塞
    }
    free(buf);
    fclose(fp);

    // 发送file_end
    cJSON *end = cJSON_CreateObject();
    cJSON_AddStringToObject(end, "type", "file_end");
    cJSON_AddNumberToObject(end, "to_id", to_id);
    if (current_chat_type == 1)
    {
        cJSON_AddNumberToObject(end, "is_group", 1);
    }
    cJSON_AddStringToObject(end, "file_id", file_id);
    cJSON_AddBoolToObject(end, "success", true);
    char *end_str = cJSON_PrintUnformatted(end);
    send_with_len(sockfd, end_str);
    free(end_str);
    cJSON_Delete(end);

    if (strstr(filepath, "voice_") != NULL && strstr(filepath, ".wav") != NULL){}
    else//如果不是语音文件再出现弹窗
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "文件发送完成：%s", strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : filepath);
        lv_obj_t *info = lv_msgbox_create(NULL, "提示", msg, NULL, true);
        obj_font_set(info, FONT_TYPE_CN, 18);
        lv_obj_center(info);
    }
}

// 查找传输记录
static file_transfer_t *find_transfer(const char *file_id)
{
    pthread_mutex_lock(&transfer_lock);
    file_transfer_t *cur = transfers;
    while (cur)
    {
        if (strcmp(cur->file_id, file_id) == 0)
        {
            pthread_mutex_unlock(&transfer_lock);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&transfer_lock);
    return NULL;
}

// 添加传输文件记录
static void add_transfer(file_transfer_t *t)
{
    // 头插法
    // 将传入的记录结构体链接到链表头部
    pthread_mutex_lock(&transfer_lock);
    t->next = transfers;
    transfers = t;
    pthread_mutex_unlock(&transfer_lock);
}

// 移除传输文件记录(重要)
static void remove_transfer(const char *file_id)
{
    pthread_mutex_lock(&transfer_lock);
    file_transfer_t **pp = &transfers;
    while (*pp)
    {
        if (strcmp((*pp)->file_id, file_id) == 0)
        {
            file_transfer_t *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&transfer_lock);
}

// 处理file_start
void handle_file_start(const char *file_id, const char *filename, int total_chunks)
{
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "/home/zhb/T113/app_sdk/app_client/res/Downloads/%s", filename);
    FILE *fp = fopen(save_path, "wb");
    if (!fp)
    {
        printf("无法创建文件:%s\n", save_path);
        return;
    }

    file_transfer_t *t = malloc(sizeof(file_transfer_t));
    strcpy(t->file_id, file_id);
    strcpy(t->filename, filename);
    t->fp = fp;
    t->total_chunks = total_chunks;
    t->received_chunks = 0;
    add_transfer(t);
}

// 处理文件开始接收（扩展）
void handle_file_start_ex(const char *file_id, const char *filename, int total_chunks, const char *from_name, const char *msg_type,int duration)
{
    // 构造保存路径(Downloads)
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "/home/zhb/T113/app_sdk/app_client/res/Downloads/%s", filename);

    FILE *fp = fopen(save_path, "wb");
    if (!fp)
    {
        printf("无法创建文件：%s\n", save_path);
        return;
    }

    // 创建新的传输记录节点
    file_transfer_t *t = (file_transfer_t *)malloc(sizeof(file_transfer_t));
    if (!t)
    {
        fclose(fp);
        return;
    }
    strcpy(t->file_id, file_id);
    strcpy(t->filename, filename);
    strncpy(t->from_name, from_name, sizeof(t->from_name) - 1);
    t->from_name[sizeof(t->from_name)] = '\0';
    t->msg_type = (strcmp(msg_type, "voice") == 0) ? 1 : 0; // 转换为整型标记
    t->duration = duration;
    t->fp = fp;
    t->total_chunks = total_chunks;
    t->received_chunks = 0;
    t->next = NULL;

    // 将节点加入链表
    add_transfer(t);
}

// 处理file_data
void handle_file_data(const char *file_id, int chunk_index, const char *data_b64)
{
    file_transfer_t *t = find_transfer(file_id);
    if (!t)
        return;

    size_t decoded_len;
    unsigned char *decoded = base64_decode(data_b64, strlen(data_b64), &decoded_len);
    if (!decoded)
        return;

    // 定位到正确位置(支持乱序)
    fseek(t->fp, chunk_index * CHUNK_SIZE, SEEK_SET);
    fwrite(decoded, 1, decoded_len, t->fp);
    t->received_chunks++;
    free(decoded);
}

//语音播放线程函数
static void *voice_play_thread_func(void* arg)
{
    const char* filepath = (const char*)arg;
    char cmd[256];
    snprintf(cmd,sizeof(cmd),"aplay %s",filepath);//拼接命令
    system(cmd);//执行命令，播放音频文件
    printf("播放完毕：%s\n",filepath);
    //播放完成后释放文件路径字符串(之前strdup分配的)
    free(filepath);
    return NULL;
}

//语音气泡点击回调函数(播放语音消息)(启动播放线程)
static void voice_play_click_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    const char* filepath = (const char*)lv_obj_get_user_data(btn);//获取文件路径

    //创建播放线程，避免阻塞UI
    //复制一份路径给线程，避免重复释放
    char* path_copy = strdup(filepath);
    pthread_t tid;
    pthread_create(&tid,NULL,voice_play_thread_func,path_copy);
    pthread_detach(tid);
}

// 在主线程中显示语音消息气泡
static void display_voice_message(void *arg)
{
    voice_msg_t *vm = (voice_msg_t *)arg;
    // 检查当前聊天状态是否匹配
    if (current_msg_area && current_chat_type == vm->chat_type && current_chat_id == vm->chat_id)
    {
        // 创建语音气泡按钮
        //  当当前时间和上一次的时间不同时
        if (strcmp(time_buf, last_msg_time) != 0)
        {
            lv_obj_t *cont_time = lv_obj_create(current_msg_area);
            lv_obj_add_style(cont_time, &com_style, 0);
            lv_obj_set_style_bg_color(cont_time, lv_color_hex(0x222222), 0);
            lv_obj_set_size(cont_time, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(cont_time, 10, 0); // 内边距

            strcpy(last_msg_time, time_buf);

            // 创建时间标签
            lv_obj_t *label_time = lv_label_create(cont_time);
            lv_obj_set_style_text_color(label_time, lv_color_hex(0xffffff), 0);
            lv_label_set_text(label_time, time_buf);
            lv_obj_align(label_time, LV_ALIGN_CENTER, 0, 0);
        }
        // 创建消息气泡上方发送者名称容器
        lv_obj_t *label_cont = lv_obj_create(current_msg_area);
        lv_obj_add_style(label_cont, &com_style, 0);
        lv_obj_set_style_bg_color(label_cont, lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(label_cont, 10, 0);
        lv_obj_set_size(label_cont, LV_PCT(100), 50);
        lv_obj_set_style_pad_all(label_cont, 20, 0); // 内边距
        lv_obj_clear_flag(label_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_clip_corner(label_cont, false, 0);
        // 发送者名称标签
        lv_obj_t *label_sendName = lv_label_create(label_cont);
        obj_font_set(label_sendName, FONT_TYPE_CN, 18);
        lv_obj_set_style_text_color(label_sendName, lv_color_hex(0xffffff), 0);
        lv_label_set_text(label_sendName, vm->sender);
        // 创建消息气泡容器
        lv_obj_t *out_cont = lv_obj_create(current_msg_area);
        lv_obj_add_style(out_cont, &com_style, 0);
        lv_obj_set_style_bg_color(out_cont, lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(out_cont, 10, 0);
        lv_obj_set_size(out_cont, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(out_cont, 0, 0); // 内边距

        //语音气泡按钮
        lv_obj_t *msg_cont = lv_obj_create(out_cont);
        lv_obj_add_style(msg_cont, &com_style, 0);
        lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0x3b3b3b), 0);
        lv_obj_set_style_radius(msg_cont, 10, 0);
        lv_obj_set_size(msg_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(msg_cont, 10, 0); // 内边距
        //存储文件路径到按钮的路径数据(点击播放)
        lv_obj_set_user_data(msg_cont,(void*)strdup(vm->file_path));
        lv_obj_add_event_cb(msg_cont,voice_play_click_cb,LV_EVENT_CLICKED,NULL);

        lv_obj_t *msg_label = lv_label_create(msg_cont);
        obj_font_set(msg_label, FONT_TYPE_CN, 18);
        lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP); // 当文本超出其宽度时，换行处理
        lv_obj_set_width(msg_label, 400);                      // 消息框最大宽度
        lv_obj_set_size(msg_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_label_set_text_fmt(msg_label, "语音%ds",vm->duration);

        // 根据是否自己来设置文本框位置
        if (vm->is_self)
        {
            lv_obj_set_style_text_color(msg_label, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0x97F097), 0);
            lv_obj_align(msg_cont, LV_ALIGN_RIGHT_MID, -10, 0);
            lv_obj_align(label_sendName, LV_ALIGN_RIGHT_MID, 0, 10);
        }
        else
        {
            lv_obj_set_style_text_color(msg_label, lv_color_hex(0x5A4B38), 0);
            lv_obj_set_style_bg_color(msg_cont, lv_color_hex(0xF5EEDC), 0);
            lv_obj_align(msg_cont, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_align(label_sendName, LV_ALIGN_LEFT_MID, 0, 10);
        }
    }
    free(vm->file_path);
    free(vm->sender);
    free(vm->time);
    free(vm);
}

// 处理file_end
void handle_file_end(const char *file_id, int success)
{
    file_transfer_t *t = find_transfer(file_id);
    if (!t)
        return;

    fclose(t->fp);
    if (success)
    {
        if (t->msg_type == 1) // 语音消息
        {
            // 构造传递给UI线程的数据
            voice_msg_t *vm = (voice_msg_t *)malloc(sizeof(voice_msg_t));
            // 保存文件完整路径(用于播放)
            char full_path[256];
            snprintf(full_path, sizeof(full_path), "/home/zhb/T113/app_sdk/app_client/res/Downloads/%s", t->filename);
            vm->file_path = strdup(full_path);
            vm->sender = strdup(t->from_name);

            // 获取当前时间
            get_current_time_str(time_buf, sizeof(time_buf), "%H : %M");
            vm->time = strdup(time_buf);
            // 判断是否自己发送：通过from_name与当前用户名比较
            vm->is_self = (strcmp(t->from_name, my_username) == 0) ? 1 : 0;
            vm->chat_id = current_chat_id;     // 当前聊天id
            vm->chat_type = current_chat_type; // 当前聊天类型
            vm->duration = t->duration;
            lv_async_call(display_voice_message, vm);
        }
        else
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "文件接收完成：%s", t->filename);
            lv_async_call(show_info_dialog, strdup(msg));
        }
    }
    else
    {
        // 删除不完整的文件
        char save_path[512];
        snprintf(save_path, sizeof(save_path), "/home/zhb/T113/app_sdk/app_client/res/Downloads/%s", t->filename);
        remove(save_path);
    }
    remove_transfer(file_id);
}

// 传输文件发送按钮回调函数
static void file_send_btn_cb(lv_event_t *e)
{
    const char *raw_path = lv_textarea_get_text(file_path_ta);
    char path[512];
    strcpy(path, raw_path);
    size_t len = strlen(path);
    while (len > 0 && isspace((unsigned char)path[len - 1]))
    {
        path[--len] = '\0';
    }
    size_t start = 0;
    while (isspace((unsigned char)path[start]))
    {
        start;
    }
    if (start > 0)
    {
        memmove(path, path + start, len - start + 1);
    }
    len = strlen(path);
    printf("修剪后的路径: '%s', 长度: %zu\n", path, len);
    if (len == 0)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "请输入文件路径", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 18);
        lv_obj_center(msg);
        return;
    }
    if (file_win)
    {
        lv_obj_del(file_win);
        file_win = NULL;
        file_path_ta = NULL;
    }

    send_file(path, current_chat_friend_id,0);
}

// 传输文件按钮回调函数
static void btn_file_event_cb(lv_event_t *e)
{
    printf("传输文件\n");
    if (current_chat_type == 0 && current_chat_friend_id == -1)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "请先选择一个好友", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 18);
        lv_obj_center(msg);
        return;
    }

    // 如果文件弹窗已存在，则返回
    if (file_win)
        return;

    file_win = lv_win_create(lv_scr_act(), 40);
    lv_obj_set_size(file_win, 400, 300);
    lv_obj_center(file_win);
    lv_win_add_title(file_win, "发送文件");
    obj_font_set(file_win, FONT_TYPE_CN, 18);
    lv_obj_set_style_radius(file_win, 10, 0);

    lv_obj_t *content = lv_win_get_content(file_win); // 获取窗口内容区域
    lv_obj_set_style_pad_all(content, 10, 0);

    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "请输入文件完整路径");
    obj_font_set(label, FONT_TYPE_CN, 18);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    file_path_ta = lv_textarea_create(content);
    lv_obj_set_width(file_path_ta, LV_PCT(100));
    lv_textarea_set_placeholder_text(file_path_ta, "例如 /home/zhb/test.jpg");
    obj_font_set(file_path_ta, FONT_TYPE_CN, 18);
    lv_obj_align_to(file_path_ta, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t *send_btn = lv_btn_create(content);
    lv_obj_set_size(send_btn, 100, 50);
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_event_cb(send_btn, file_send_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(send_btn);
    lv_label_set_text(btn_label, "发送");
    obj_font_set(btn_label, FONT_TYPE_CN, 18);
    lv_obj_center(btn_label);
}

// 录音线程函数
// 调用arecord录制音频，知道voice_recording为0
static void *voice_record_thread_func(void *arg)
{
    //构建录音命令
    char cmd[256];
    // 构建arecord命令：
    //-f cd:CD质量
    //-t wav:输出格式
    //-d 60:最长录制60秒
    snprintf(cmd, sizeof(cmd), "arecord -f cd -t wav -d 60 %s", voice_file_path);

    FILE *fp = popen(cmd, "r"); // 打开管道执行命令，读取输出
    if (!fp)
    {
        perror("popen failed");
        pthread_mutex_lock(&voice_lock);
        voice_recording = 0; // 失败时清除标志
        pthread_mutex_unlock(&voice_lock);
        return NULL;
    }

    // 循环检查voice_recording标志，若为0则停止录音
    while (voice_recording)
    {
        usleep(100000);
    }

    // 使用系统命令强制杀掉所有arecord进程
    system("pkill -f arecord");
    // 关闭管道，此时arecord进程仍在运行，需要强制终止
    pclose(fp); // 等待进程结束，但不会立即终止
    
    return NULL;
}

// 录制语音按钮回调函数
static void voice_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e); // 获取时间类型
    static time_t start_time = 0;//录音开始时间
    if (code == LV_EVENT_LONG_PRESSED)           // 长按：开始录音
    {
        pthread_mutex_lock(&voice_lock);
        if (voice_recording) // 如果已在录音，则忽略
        {
            pthread_mutex_unlock(&voice_lock);
            return;
        }
        start_time = time(NULL);
        voice_recording = 1; // 设置录音标志
        // 生成临时文件名：/tmp/voice_时间戳.wav
        system("mkdir -p /home/zhb/T113/app_sdk/app_client/res/tmp");
        snprintf(voice_file_path, sizeof(voice_file_path), "/home/zhb/T113/app_sdk/app_client/res/tmp/voice_%ld.wav", time(NULL));
        // 创建录音线程
        pthread_create(&voice_record_thread, NULL, voice_record_thread_func, NULL);
        pthread_mutex_unlock(&voice_lock);
        printf("开始录音\n");
    }
    else if (code == LV_EVENT_RELEASED) // 松开事件，停止录音并发送
    {
        pthread_mutex_lock(&voice_lock);
        if (voice_recording)
        {
            voice_recording = 0;                     // 清除标志
            pthread_mutex_unlock(&voice_lock);
            pthread_join(voice_record_thread, NULL); // 等待录音线程结束
            //计算时长
            int duration = (int)(time(NULL) - start_time);
            // 发送语音文件
            send_file(voice_file_path, current_chat_friend_id,duration);

            //本地显示自己的语音消息
            voice_msg_t* vm = (voice_msg_t*)malloc(sizeof(voice_msg_t));
            vm->file_path = strdup(voice_file_path);
            vm->sender = strdup(my_username);
            get_current_time_str(time_buf,sizeof(time_buf),"%H : %M");
            vm->time = strdup(time_buf);
            vm->is_self = 1;//本地发送
            vm->chat_id = current_chat_id;
            vm->chat_type = current_chat_type;
            vm->duration = duration;
            lv_async_call(display_voice_message,vm);
        }
        else
        {
            pthread_mutex_unlock(&voice_lock);
        }
        printf("结束录音并发送\n");
    }
}

// 右侧聊天室
static void show_chat_with_friend(int friend_id, const char *friend_name)
{
    if (!cont_chat)
        return;

    if (keyboard_chinese)
    {
        lv_obj_del(keyboard_chinese);
        keyboard_chinese = NULL;
    }

    // 释放之前按钮的数据
    if (current_btn_data)
    {
        free(current_btn_data);
        current_btn_data = NULL;
    }
    // 清空右侧
    lv_obj_clean(cont_chat);
    lv_obj_set_style_bg_color(cont_chat, lv_color_hex(0x222222), 0);
    // 创建右侧聊天框
    // 信息栏
    lv_obj_t *info_area = lv_obj_create(screen);
    lv_obj_add_style(info_area, &com_style, 0);
    lv_obj_set_style_radius(info_area, 5, 0);
    lv_obj_set_style_bg_color(info_area, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(info_area, 1, 0);
    lv_obj_set_style_border_color(info_area, lv_color_hex(0x000000), 0);
    lv_obj_set_size(info_area, 800, 80);
    lv_obj_align(info_area, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *name_label = lv_label_create(info_area);
    obj_font_set(name_label, FONT_TYPE_CN, 26);
    lv_label_set_text(name_label, friend_name ? friend_name : "未知");
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), 0);
    // 消息显示区域
    current_msg_area = lv_obj_create(screen);
    lv_obj_add_style(current_msg_area, &com_style, 0);
    lv_obj_set_style_radius(current_msg_area, 5, 0);
    lv_obj_set_style_bg_color(current_msg_area, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(current_msg_area, 1, 0);
    lv_obj_set_style_border_color(current_msg_area, lv_color_hex(0x000000), 0);
    lv_obj_set_size(current_msg_area, 800, 420);
    lv_obj_align_to(current_msg_area, info_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(current_msg_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(current_msg_area, 10, 0);
    lv_obj_set_style_pad_row(current_msg_area, 0, 0);
    // 发送区域
    lv_obj_t *send_obj = lv_obj_create(screen);
    lv_obj_add_style(send_obj, &com_style, 0);
    lv_obj_set_style_radius(send_obj, 5, 0);
    lv_obj_set_style_bg_color(send_obj, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(send_obj, 1, 0);
    lv_obj_set_style_border_color(send_obj, lv_color_hex(0x222222), 0);
    lv_obj_set_size(send_obj, 800, 200);
    lv_obj_align_to(send_obj, current_msg_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    // 消息类型选择区域(表情、传输文件等)
    lv_obj_t *select_msg_type = lv_obj_create(send_obj);
    lv_obj_add_style(select_msg_type, &com_style, 0);
    lv_obj_set_style_radius(select_msg_type, 5, 0);
    lv_obj_set_style_bg_color(select_msg_type, lv_color_hex(0x222222), 0);
    lv_obj_set_size(select_msg_type, LV_PCT(100), 50);
    lv_obj_align(select_msg_type, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(select_msg_type, LV_OBJ_FLAG_SCROLLABLE);
    // 传输文件按钮
    lv_obj_t *btn_file = lv_obj_create(select_msg_type);
    lv_obj_add_style(btn_file, &com_style, 0);
    lv_obj_set_style_bg_opa(btn_file, 0, 0);
    lv_obj_set_size(btn_file, 40, 40);
    lv_obj_align(btn_file, LV_ALIGN_TOP_RIGHT, -7, 15);
    lv_obj_add_flag(btn_file, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_file, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn_file, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_add_event_cb(btn_file, btn_file_event_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *img_file = lv_img_create(btn_file);
    lv_img_set_src(img_file, GET_IMAGE_PATH("file.png"));
    lv_obj_center(img_file);
    lv_img_set_zoom(img_file, 40);

    // 发送语音消息按钮
    lv_obj_t *btn_record = lv_obj_create(select_msg_type);
    lv_obj_add_style(btn_record, &com_style, 0);
    lv_obj_set_style_bg_opa(btn_record, 0, 0);
    lv_obj_set_size(btn_record, 35, 35);
    lv_obj_align(btn_record, LV_ALIGN_TOP_RIGHT, -7, 15);
    lv_obj_add_flag(btn_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_record, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(btn_record, btn_file, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
    lv_obj_add_event_cb(btn_record, voice_btn_event_cb, LV_EVENT_ALL, 0);
    lv_obj_t *img_record = lv_img_create(btn_record);
    lv_img_set_src(img_record, GET_IMAGE_PATH("record.png"));
    lv_obj_center(img_record);
    lv_img_set_zoom(img_record, 35);

    // 发送区域(聊天框)
    lv_obj_t *ta = lv_textarea_create(send_obj);
    lv_obj_add_style(ta, &com_style, 0);
    lv_obj_set_style_radius(ta, 5, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x222222), 0);
    lv_obj_set_size(ta, 780, 150);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xffffff), LV_PART_CURSOR | LV_STATE_FOCUSED);
    obj_font_set(ta, FONT_TYPE_CN, 18);
    // 设置中文输入法
    lv_obj_t *pinyin = lv_ime_pinyin_create(send_obj);
    obj_font_set(pinyin, FONT_TYPE_CN, 18);
    // 候选词区域
    lv_obj_t *cand_panel = lv_ime_pinyin_get_cand_panel(pinyin);
    lv_obj_set_style_pad_column(cand_panel, 10, LV_PART_ITEMS);
    lv_obj_set_style_text_color(cand_panel, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_width(cand_panel, 400);
    keyboard_chinese = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(keyboard_chinese, 400, 200);
    lv_obj_align_to(keyboard_chinese, send_obj, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    lv_ime_pinyin_set_keyboard(pinyin, keyboard_chinese);
    lv_keyboard_set_textarea(keyboard_chinese, ta);

    // 聊天框发送按钮
    lv_obj_t *btn_send = lv_btn_create(send_obj);
    lv_obj_set_size(btn_send, 100, 40);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    struct send_btn_data *btn_data = (struct send_btn_data *)malloc(sizeof(struct send_btn_data));
    btn_data->ta = ta;
    btn_data->friend_id = friend_id;
    lv_obj_add_event_cb(btn_send, send_btn_cb, LV_EVENT_CLICKED, (void *)btn_data);
    current_btn_data = btn_data;
    lv_obj_t *label_send = lv_label_create(btn_send);
    obj_font_set(label_send, FONT_TYPE_CN, 20);
    lv_label_set_text(label_send, "发送");
    lv_obj_center(label_send);
    // 关闭聊天框按钮
    lv_obj_t *btn_close = lv_btn_create(send_obj);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align_to(btn_close, btn_send, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_event_cb(btn_close, close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_close = lv_label_create(btn_close);
    obj_font_set(label_close, FONT_TYPE_CN, 20);
    lv_label_set_text(label_close, "关闭");
    lv_obj_center(label_close);

    // 保存当前聊天的好友id
    current_chat_friend_id = friend_id;
    // 设置当前聊天类型和ID
    current_chat_type = 0;       // 私聊
    current_chat_id = friend_id; // 存储好友id

    // 请求私聊历史消息
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "get_history");
    cJSON_AddNumberToObject(req, "friend_id", friend_id);
    cJSON_AddNumberToObject(req, "limit", 50);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);
}

// 点击左侧好友按钮后，开启和好友的对话
static void friend_btn_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    // 事件本身未携带数据，是btn携带的数据
    int friend_id = (int)(intptr_t)lv_obj_get_user_data(btn); // 没有为AI助手按钮设置数据，按钮值默认为0，正好是AI助手id

    // 从缓存中查找好友名
    char *friend_name = NULL;
    if (friend_id == 0)
    {
        friend_name = "AI助手";
    }
    for (int i = 0; i < cached_friends_count; i++)
    {
        if (cached_friends[i].id == friend_id)
        {
            friend_name = cached_friends[i].name;
            break;
        }
    }
    show_chat_with_friend(friend_id, friend_name);
}

// 显示群聊天界面
static void show_chat_with_group(int group_id, const char *group_name)
{
    if (!cont_chat)
        return;
    // 关闭键盘
    if (keyboard_chinese)
    {
        lv_obj_del(keyboard_chinese);
        keyboard_chinese = NULL;
    }

    // 释放之前按钮的数据
    if (current_btn_data)
    {
        free(current_btn_data);
        current_btn_data = NULL;
    }

    // 清空右侧聊天框
    lv_obj_clean(cont_chat);
    lv_obj_set_style_bg_color(cont_chat, lv_color_hex(0x222222), 0);

    // 信息栏
    lv_obj_t *info_area = lv_obj_create(screen);
    lv_obj_add_style(info_area, &com_style, 0);
    lv_obj_set_style_radius(info_area, 5, 0);
    lv_obj_set_style_bg_color(info_area, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(info_area, 1, 0);
    lv_obj_set_style_border_color(info_area, lv_color_hex(0x000000), 0);
    lv_obj_set_size(info_area, 800, 80);
    lv_obj_align(info_area, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *name_label = lv_label_create(info_area);
    obj_font_set(name_label, FONT_TYPE_CN, 26);
    lv_label_set_text(name_label, group_name ? group_name : "群聊");
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), 0);
    // 消息显示区域
    current_msg_area = lv_obj_create(screen);
    lv_obj_add_style(current_msg_area, &com_style, 0);
    lv_obj_set_style_radius(current_msg_area, 5, 0);
    lv_obj_set_style_bg_color(current_msg_area, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(current_msg_area, 1, 0);
    lv_obj_set_style_border_color(current_msg_area, lv_color_hex(0x000000), 0);
    lv_obj_set_size(current_msg_area, 800, 420);
    lv_obj_align_to(current_msg_area, info_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(current_msg_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(current_msg_area, 10, 0);
    lv_obj_set_style_pad_row(current_msg_area, 0, 0);
    // 发送区域
    lv_obj_t *send_obj = lv_obj_create(screen);
    lv_obj_add_style(send_obj, &com_style, 0);
    lv_obj_set_style_radius(send_obj, 5, 0);
    lv_obj_set_style_bg_color(send_obj, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(send_obj, 1, 0);
    lv_obj_set_style_border_color(send_obj, lv_color_hex(0x222222), 0);
    lv_obj_set_size(send_obj, 800, 200);
    lv_obj_align_to(send_obj, current_msg_area, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    // 消息类型选择区域(表情、传输文件等)
    lv_obj_t *select_msg_type = lv_obj_create(send_obj);
    lv_obj_add_style(select_msg_type, &com_style, 0);
    lv_obj_set_style_radius(select_msg_type, 5, 0);
    lv_obj_set_style_bg_color(select_msg_type, lv_color_hex(0x222222), 0);
    lv_obj_set_size(select_msg_type, LV_PCT(100), 50);
    lv_obj_align(select_msg_type, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(select_msg_type, LV_OBJ_FLAG_SCROLLABLE);
    // 传输文件按钮
    lv_obj_t *btn_file = lv_obj_create(select_msg_type);
    lv_obj_add_style(btn_file, &com_style, 0);
    lv_obj_set_style_bg_opa(btn_file, 0, 0);
    lv_obj_set_size(btn_file, 40, 40);
    lv_obj_align(btn_file, LV_ALIGN_TOP_RIGHT, -7, 15);
    lv_obj_add_flag(btn_file, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_file, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn_file, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_add_event_cb(btn_file, btn_file_event_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *img_file = lv_img_create(btn_file);
    lv_img_set_src(img_file, GET_IMAGE_PATH("file.png"));
    lv_obj_center(img_file);
    lv_img_set_zoom(img_file, 40);

    // 发送语音消息按钮
    lv_obj_t *btn_record = lv_obj_create(select_msg_type);
    lv_obj_add_style(btn_record, &com_style, 0);
    lv_obj_set_style_bg_opa(btn_record, 0, 0);
    lv_obj_set_size(btn_record, 35, 35);
    lv_obj_align(btn_record, LV_ALIGN_TOP_RIGHT, -7, 15);
    lv_obj_add_flag(btn_record, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_record, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(btn_record, btn_file, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
    lv_obj_add_event_cb(btn_record, voice_btn_event_cb, LV_EVENT_ALL, 0);
    lv_obj_t *img_record = lv_img_create(btn_record);
    lv_img_set_src(img_record, GET_IMAGE_PATH("record.png"));
    lv_obj_center(img_record);
    lv_img_set_zoom(img_record, 35);

    // 发送区域(聊天框)
    lv_obj_t *ta = lv_textarea_create(send_obj);
    lv_obj_add_style(ta, &com_style, 0);
    lv_obj_set_style_radius(ta, 5, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x222222), 0);
    lv_obj_set_size(ta, 780, 150);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xffffff), LV_PART_CURSOR | LV_STATE_FOCUSED);
    obj_font_set(ta, FONT_TYPE_CN, 18);
    // 设置中文输入法
    lv_obj_t *pinyin = lv_ime_pinyin_create(send_obj);
    obj_font_set(pinyin, FONT_TYPE_CN, 18);
    // 候选词区域
    lv_obj_t *cand_panel = lv_ime_pinyin_get_cand_panel(pinyin);
    lv_obj_set_style_pad_column(cand_panel, 10, LV_PART_ITEMS);
    lv_obj_set_style_text_color(cand_panel, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_width(cand_panel, 400);
    keyboard_chinese = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(keyboard_chinese, 400, 200);
    lv_obj_align_to(keyboard_chinese, send_obj, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    lv_ime_pinyin_set_keyboard(pinyin, keyboard_chinese);
    lv_keyboard_set_textarea(keyboard_chinese, ta);

    // 聊天框发送按钮
    lv_obj_t *btn_send = lv_btn_create(send_obj);
    lv_obj_set_size(btn_send, 100, 40);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    struct send_btn_data *btn_data = (struct send_btn_data *)malloc(sizeof(struct send_btn_data));
    btn_data->ta = ta;
    btn_data->friend_id = group_id; // 复用friend_id存储群ID
    lv_obj_add_event_cb(btn_send, send_btn_cb, LV_EVENT_CLICKED, (void *)btn_data);
    current_btn_data = btn_data;
    lv_obj_t *label_send = lv_label_create(btn_send);
    obj_font_set(label_send, FONT_TYPE_CN, 20);
    lv_label_set_text(label_send, "发送");
    lv_obj_center(label_send);
    // 关闭聊天框按钮
    lv_obj_t *btn_close = lv_btn_create(send_obj);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align_to(btn_close, btn_send, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_event_cb(btn_close, close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_close = lv_label_create(btn_close);
    obj_font_set(label_close, FONT_TYPE_CN, 20);
    lv_label_set_text(label_close, "关闭");
    lv_obj_center(label_close);

    // 设置当前聊天类型和ID
    current_chat_type = 1; // 群聊
    current_chat_id = group_id;
    current_chat_friend_id = group_id;

    // 请求群历史消息
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "get_group_history");
    cJSON_AddNumberToObject(req, "group_id", group_id);
    cJSON_AddNumberToObject(req, "limit", 100);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);
}

// 群聊列表按钮回调函数
static void group_btn_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int group_id = (int)(intptr_t)lv_obj_get_user_data(btn);

    // 从缓存中查找群名称
    char *group_name = NULL;
    for (int i = 0; i < cached_groups_count; i++)
    {
        if (cached_groups[i].id == group_id)
        {
            group_name = cached_groups[i].name;
            break;
        }
    }
    if (!group_name)
        group_name = "未知群聊";
    // 调用显示群聊界面函数
    show_chat_with_group(group_id, group_name);
}

// 更新群聊列表UI
void update_group_list_ui(void *arg)
{
    cJSON *groups = (cJSON *)arg; // 传入的是groups_list中的groups数组
    if (!group_list_cont)
    {
        cJSON_Delete(groups);
        return;
    }
    lv_obj_clean(group_list_cont);

    int size = cJSON_GetArraySize(groups);
    // 更新缓存
    cached_groups_count = size;
    for (int i = 0; i < size; i++)
    {
        cJSON *item = cJSON_GetArrayItem(groups, i);
        cJSON *id_item = cJSON_GetObjectItem(item, "id");
        cJSON *name_item = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsNumber(id_item) && cJSON_IsString(name_item))
        {
            // 缓存群消息
            cached_groups[i].id = id_item->valueint;
            strncpy(cached_groups[i].name, name_item->valuestring, sizeof(cached_groups[i].name) - 1);
            cached_groups[i].name[sizeof(cached_groups[i].name) - 1] = '\0';

            // 创建列表按钮
            lv_obj_t *btn = lv_list_add_btn(group_list_cont, NULL, NULL);
            lv_obj_add_style(btn, &com_style, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x282828), 0);
            lv_obj_set_size(btn, LV_PCT(100), 70);
            lv_obj_t *label = lv_label_create(btn);
            lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
            lv_obj_set_pos(label, 70, 25); // 预留群头像位置
            obj_font_set(label, FONT_TYPE_CN, 25);
            lv_label_set_text(label, cached_groups[i].name);
            lv_obj_set_user_data(btn, (void *)(intptr_t)cached_groups[i].id);
            lv_obj_add_event_cb(btn, group_btn_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }
    cJSON_Delete(groups);
}

// 刷新好友列表
void update_friend_list_ui(friend_list_t *list)
{
    if (!friend_list_cont)
        return;

    // 清空好友列表
    lv_obj_clean(friend_list_cont);

    // 创建AI助手好友（ID = 0）
    lv_obj_t *btn = lv_list_add_btn(friend_list_cont, NULL, NULL);
    lv_obj_add_style(btn, &com_style, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x282828), 0);
    lv_obj_set_size(btn, LV_PCT(100), 70);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(label, 70, 25); // 预留头像位置
    obj_font_set(label, FONT_TYPE_CN, 25);
    lv_label_set_text(label, "AI助手");
    lv_obj_add_event_cb(btn, friend_btn_click_cb, LV_EVENT_CLICKED, NULL);

    // 添加普通好友
    cached_friends_count = list->count;
    for (int i = 0; i < list->count; i++)
    {
        // 好友结构体拷贝
        cached_friends[i] = list->friends[i];

        friend_info_t *f = &list->friends[i];
        lv_obj_t *btn = lv_list_add_btn(friend_list_cont, NULL, NULL);
        lv_obj_add_style(btn, &com_style, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x282828), 0);
        lv_obj_set_size(btn, LV_PCT(100), 70);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(label, 70, 25); // 预留头像位置
        obj_font_set(label, FONT_TYPE_CN, 25);
        lv_label_set_text(label, f->name);
        // 将用户id(整数)转换为指针类型，存储在控件中
        // intptr_t为64/32位的整数类型
        // 可以安全地转换为指针
        lv_obj_set_user_data(btn, (void *)(intptr_t)f->id);
        lv_obj_add_event_cb(btn, friend_btn_click_cb, LV_EVENT_CLICKED, NULL);
    }

    if (list->friends)
        free(list->friends);
    free(list);
}

static void btn_weather_event_cb(lv_event_t *e)
{
    printf("获取天气\n");
    // 如果天气容器不存在则创建
    if (!weather_cont)
    {
        weather_cont = lv_obj_create(lv_scr_act());
        lv_obj_add_style(weather_cont, &com_style, 0);
        lv_obj_set_style_bg_color(weather_cont, lv_color_hex(0xa3c6da), 0);
        lv_obj_set_size(weather_cont, 360, 280);
        lv_obj_set_style_radius(weather_cont, 10, 0);
        lv_obj_align_to(weather_cont, cont_information, LV_ALIGN_OUT_RIGHT_TOP, 10, 20);
        lv_obj_clear_flag(weather_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_clip_corner(weather_cont, true, 0);
        lv_obj_clear_state(weather_cont, LV_STATE_FOCUS_KEY);
    }
    else
    {
        lv_obj_move_foreground(weather_cont); // 将天气容器移到前台，防止被其他控件遮挡
    }
    // 尝试用缓存显示
    char city[32], weather[32];
    int temp;
    if (weather_cache_get(city, sizeof(city), weather, sizeof(weather), &temp))
    {
        // 直接使用缓存更新UI
        weather_result_t *res = malloc(sizeof(weather_result_t));
        res->weather_cont = weather_cont;
        res->city = strdup(city);
        res->weather = strdup(weather);
        res->temp = temp;
        update_weather_ui(res); // 释放res
    }
    else
    {
        // 无缓存，显示加载动画
        lv_obj_clean(weather_cont);
        lv_obj_t *label_loading = lv_label_create(weather_cont);
        obj_font_set(label_loading, FONT_TYPE_CN, 20);
        lv_label_set_text(label_loading, "加载天气中...");
        lv_obj_center(label_loading);
    }

    // 检查是否已有天气线程在运行
    pthread_mutex_lock(&weather_fetch_lock);
    if (weather_fetching)
    {
        pthread_mutex_unlock(&weather_fetch_lock);
        printf("已有天气线程在运行，本次忽略\n");
        return;
    }
    weather_fetching = 1;
    pthread_mutex_unlock(&weather_fetch_lock);

    // 启动线程获取天气数据(更新缓存并刷新UI)
    pthread_t tid;
    char *cityinput = strdup("xian");
    pthread_create(&tid, NULL, weather_thread_func, cityinput);
    pthread_detach(tid);
}

// 切换好友列表回调函数
static void btn_frients_event_cb(lv_event_t *e)
{
    puts("切换到好友列表");
    // 显示好友列表，隐藏群聊列表
    lv_obj_clear_flag(friend_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(group_list_cont, LV_OBJ_FLAG_HIDDEN);
}

// 切换群聊列表回调函数
static void btn_group_event_cb(lv_event_t *e)
{
    puts("切换到群聊列表");
    // 显示群聊列表，隐藏好友列表
    lv_obj_clear_flag(group_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(friend_list_cont, LV_OBJ_FLAG_HIDDEN);
    // 向服务器请求群列表
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "get_groups");
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);
}

void init_page_users_chat(void)
{
    // 创建新屏幕，并在新屏幕上加载控件
    screen = lv_obj_create(NULL);
    // 创建定时器
    lv_timer_create(timer_cb_func, 1000, NULL);
    // 创建底层容器，容纳用户列表
    lv_obj_t *cont_users = lv_obj_create(screen);
    lv_obj_clear_flag(cont_users, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_users, &com_style, 0);
    lv_obj_set_style_bg_opa(cont_users, 0, 0);
    lv_obj_set_size(cont_users, 400, 700);
    lv_obj_align(cont_users, LV_ALIGN_LEFT_MID, 0, 0);

    // 好友列表背景图
    lv_obj_t *bg_users = lv_img_create(cont_users);
    lv_img_set_zoom(bg_users, 1100);
    lv_img_set_src(bg_users, GET_IMAGE_PATH("bg_1.png"));

    // 创建好友列表上方信息栏对象
    cont_information = lv_obj_create(cont_users);
    lv_obj_clear_flag(cont_information, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(cont_information, &com_style, 0);
    lv_obj_set_style_bg_color(cont_information, lv_color_hex(0x222222), 0);
    lv_obj_set_size(cont_information, 400, 150);
    lv_obj_align(cont_information, LV_ALIGN_TOP_MID, 0, 0);
    // qq头像
    lv_obj_t *cont_headimg = lv_obj_create(cont_information);
    lv_obj_add_style(cont_headimg, &com_style, 0);
    lv_obj_set_style_radius(cont_headimg, 90, 0);
    lv_obj_set_size(cont_headimg, 75, 75);
    lv_obj_align(cont_headimg, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_clip_corner(cont_headimg, true, 0);
    lv_obj_set_style_bg_opa(cont_headimg, 0, 0);
    lv_obj_clear_flag(cont_headimg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *headimg = lv_img_create(cont_headimg);
    lv_img_set_zoom(headimg, 53);
    lv_img_set_src(headimg, GET_IMAGE_PATH("headimg1.png"));
    lv_obj_center(headimg);

    // 账号信息标签
    account_info = lv_label_create(cont_information);
    lv_obj_set_style_text_color(account_info, lv_color_hex(0xffffff), 0);
    lv_obj_align_to(account_info, cont_headimg, LV_ALIGN_OUT_RIGHT_MID, 15, -15);

    // 添加好友按钮
    lv_obj_t *addBtn = lv_obj_create(cont_information);
    lv_obj_add_style(addBtn, &com_style, 0);
    lv_obj_set_size(addBtn, 50, 50);
    lv_obj_align(addBtn, LV_ALIGN_BOTTOM_RIGHT, -7, -15);
    lv_obj_set_style_bg_opa(addBtn, 0, 0);
    lv_obj_set_style_clip_corner(addBtn, true, 0);
    lv_obj_clear_flag(addBtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *addImg = lv_img_create(addBtn);
    lv_img_set_src(addImg, GET_IMAGE_PATH("img_add.png"));
    lv_img_set_zoom(addImg, 50);
    lv_obj_center(addImg);
    lv_obj_add_flag(addImg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(addImg, addImg_event_cb, LV_EVENT_CLICKED, 0);

    // 天气按钮
    lv_obj_t *btn_weather = lv_obj_create(cont_information);
    lv_obj_add_style(btn_weather, &com_style, 0);
    lv_obj_set_style_bg_opa(btn_weather, 0, 0);
    lv_obj_set_size(btn_weather, 50, 50);
    lv_obj_align(btn_weather, LV_ALIGN_TOP_RIGHT, -7, 15);
    lv_obj_add_flag(btn_weather, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_weather, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_weather, btn_weather_event_cb, LV_EVENT_CLICKED, 0);
    lv_obj_t *img_weather = lv_img_create(btn_weather);
    lv_img_set_src(img_weather, GET_IMAGE_PATH("weather.png"));
    lv_obj_center(img_weather);
    lv_img_set_zoom(img_weather, 50);

    // 右侧聊天框初始状态
    init_chat_area();

    // 创建最左侧查看好友或群聊按钮的底层容器
    lv_obj_t *cont_friends_groups = lv_obj_create(cont_users);
    lv_obj_set_style_opa(cont_friends_groups, 200, 0);
    lv_obj_set_style_bg_color(cont_friends_groups, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(cont_friends_groups, 5, 0);
    lv_obj_set_style_border_width(cont_friends_groups, 1, 0);
    lv_obj_set_style_border_color(cont_friends_groups, lv_color_hex(0x3f3f3f), 0);
    lv_obj_set_size(cont_friends_groups, 50, 550);
    lv_obj_align(cont_friends_groups, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_clear_flag(cont_friends_groups, LV_OBJ_FLAG_SCROLLABLE);
    // 切换好友列表按钮
    lv_obj_t *btn_frients = lv_obj_create(cont_friends_groups);
    lv_obj_add_style(btn_frients, &com_style, 0);
    lv_obj_set_size(btn_frients, 40, 40);
    lv_obj_set_style_bg_opa(btn_frients, 0, 0);
    lv_obj_set_style_clip_corner(btn_frients, true, 0);
    lv_obj_clear_flag(btn_frients, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn_frients, LV_ALIGN_TOP_MID, 0, -15);
    lv_obj_add_event_cb(btn_frients, btn_frients_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *img_btn_frients = lv_img_create(btn_frients);
    lv_img_set_src(img_btn_frients, GET_IMAGE_PATH("friends.png"));
    lv_img_set_zoom(img_btn_frients, 40);
    lv_obj_center(img_btn_frients);
    // 切换群聊列表按钮
    lv_obj_t *btn_group = lv_obj_create(cont_friends_groups);
    lv_obj_add_style(btn_group, &com_style, 0);
    lv_obj_set_size(btn_group, 50, 50);
    lv_obj_set_style_bg_opa(btn_group, 0, 0);
    lv_obj_set_style_clip_corner(btn_group, true, 0);
    lv_obj_clear_flag(btn_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(btn_group, btn_frients, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_add_event_cb(btn_group, btn_group_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *img_btn_group = lv_img_create(btn_group);
    lv_img_set_src(img_btn_group, GET_IMAGE_PATH("group.png"));
    lv_img_set_zoom(img_btn_group, 50);
    lv_obj_center(img_btn_group);

    // 创建左侧好友列表
    friend_list_cont = lv_list_create(cont_users);
    lv_obj_add_style(friend_list_cont, &com_style, 0);
    lv_obj_set_style_opa(friend_list_cont, 200, 0);
    lv_obj_set_style_bg_color(friend_list_cont, lv_color_hex(0x222222), 0);
    lv_obj_set_size(friend_list_cont, 350, 550);
    lv_obj_align(friend_list_cont, LV_ALIGN_BOTTOM_LEFT, 50, 0);

    group_list_cont = lv_list_create(cont_users);
    lv_obj_add_style(group_list_cont, &com_style, 0);
    lv_obj_set_style_opa(group_list_cont, 200, 0);
    lv_obj_set_style_bg_color(group_list_cont, lv_color_hex(0x222222), 0);
    lv_obj_set_size(group_list_cont, 350, 550);
    lv_obj_align(group_list_cont, LV_ALIGN_BOTTOM_LEFT, 50, 0);
    lv_obj_add_flag(group_list_cont, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
}