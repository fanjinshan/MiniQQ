#include "page_config.h"

// 聊天界面
extern lv_obj_t *screen;
lv_obj_t *g_ta1 = NULL;
lv_obj_t *g_ta2 = NULL;
extern int current_chat_friend_id;
extern lv_obj_t *current_msg_area;

typedef struct
{
    char *from_name;
    char *content;
} msg_data;

static void switch_screen_main_to_chat(void *arg)
{
    // 获取对象layer层，lv_scr_act对象
    lv_obj_t *act_scr = lv_scr_act();
    // 获取显示屏幕对象
    lv_disp_t *d = lv_obj_get_disp(act_scr);
    // 不在页面切换或者加载过程中才清除,避免页面触发过快，手指还停留着导致显示异常
    // 页面加载完成后d->prev_scr会清空，d->scr_to_load会指向当前焦点界面
    if (d->prev_scr == NULL && (d->scr_to_load == NULL || d->scr_to_load == act_scr))
    {
        // 清除旧屏幕，并显示新屏幕
        lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
    }
}

// A接收到B回传的消息弹窗（待优化）
void show_info_dialog(void *arg)
{
    char *msg = (char *)arg;
    lv_obj_t *mbox = lv_msgbox_create(NULL, "提示", msg, NULL, true);
    obj_font_set(mbox, FONT_TYPE_CN, 18);
    lv_obj_center(mbox);
    free(msg);
}

static void accept_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *mbox = lv_obj_get_parent(target);
    request_info_t *info = (request_info_t *)lv_event_get_user_data(e);
    cJSON *req1 = cJSON_CreateObject();
    cJSON_AddStringToObject(req1, "type", "respond_friend");
    cJSON_AddNumberToObject(req1, "from_id", info->from_id);
    cJSON_AddStringToObject(req1, "action", "accept");
    char *req_str = cJSON_PrintUnformatted(req1);
    send_with_len(sockfd, req_str);
    cJSON_Delete(req1);

    free(req_str);
    lv_msgbox_close(mbox);
    free(info);
}

static void reject_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *mbox = lv_obj_get_parent(target);
    request_info_t *info = (request_info_t *)lv_event_get_user_data(e);
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "respond_friend");
    cJSON_AddNumberToObject(req, "from_id", info->from_id);
    cJSON_AddStringToObject(req, "action", "reject");
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);

    lv_msgbox_close(mbox);
    free(info);
}

// B收到添加好友请求后，选择同意还是拒绝
static void show_friend_request_dialog(void *arg)
{
    request_info_t *info = (request_info_t *)arg;
    char buf[128];
    snprintf(buf, sizeof(buf), "用户%s请求添加你为好友", info->from_name);
    // 弹窗底层容器
    lv_obj_t *mbox = lv_obj_create(lv_layer_top());
    lv_obj_add_style(mbox, &com_style, 0);
    lv_obj_set_style_bg_color(mbox, lv_color_hex(0x282828), 0);
    lv_obj_set_size(mbox, 300, 400);
    lv_obj_center(mbox);

    lv_obj_t *label_title = lv_label_create(mbox);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xffffff), 0);
    obj_font_set(label_title, FONT_TYPE_CN, 20);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_text(label_title, "好友请求");

    lv_obj_t *label_content = lv_label_create(mbox);
    lv_obj_set_style_text_color(label_content, lv_color_hex(0xffffff), 0);
    obj_font_set(label_content, FONT_TYPE_CN, 20);
    lv_obj_align(label_content, LV_ALIGN_TOP_MID, 0, 100);
    lv_label_set_text(label_content, buf);

    lv_obj_t *btn_accept = lv_btn_create(mbox);
    lv_obj_add_event_cb(btn_accept, accept_event_cb, LV_EVENT_CLICKED, info);
    lv_obj_align(btn_accept, LV_ALIGN_BOTTOM_MID, -50, -50);
    lv_obj_t *label_accept = lv_label_create(btn_accept);
    obj_font_set(label_accept, FONT_TYPE_CN, 20);
    lv_label_set_text(label_accept, "同意");
    lv_obj_center(label_accept);

    lv_obj_t *btn_reject = lv_btn_create(mbox);
    lv_obj_add_event_cb(btn_reject, reject_event_cb, LV_EVENT_CLICKED, info);
    lv_obj_align(btn_reject, LV_ALIGN_BOTTOM_MID, 50, -50);
    lv_obj_t *label_reject = lv_label_create(btn_reject);
    obj_font_set(label_reject, FONT_TYPE_CN, 20);
    lv_label_set_text(label_reject, "拒绝");
    lv_obj_center(label_reject);
}

static void friend_added_dialog(void *arg)
{
    friend_info_t *info = (friend_info_t *)arg;
    char buf[128];
    sprintf(buf, "%s同意添加你为好友", info->name);
    lv_obj_t *mbox = lv_msgbox_create(NULL, "好友通知", buf, NULL, true);
    obj_font_set(mbox, FONT_TYPE_CN, 20);
    lv_obj_center(mbox);
}

static void friend_added_failed_dialog(void *arg)
{
    friend_info_t *info = (friend_info_t *)arg;
    char buf[128];
    sprintf(buf, "%s拒绝添加你为好友", info->name);
    lv_obj_t *mbox = lv_msgbox_create(NULL, "好友通知", buf, NULL, true);
    obj_font_set(mbox, FONT_TYPE_CN, 20);
    lv_obj_center(mbox);
}

// 显示收到的消息
static void display_received_message(void *arg)
{
    msg_data *data = (msg_data *)arg;
    if (current_msg_area && current_chat_friend_id != -1)
    {
        add_message(current_msg_area, data->from_name, data->content, false, time_buf);
    }
    free(data->from_name);
    free(data->content);
    free(data);
}

//显示历史消息(群聊)
static void display_history_message(void *arg)
{
    history_item_t* item = (history_item_t*)arg;
    if(current_msg_area && current_chat_type == 1 && current_chat_id != -1)
    {
        add_message(current_msg_area,item->sender,item->content,item->is_self,item->time);
    }
    free(item->sender);
    free(item->content);
    free(item->time);
    free(item);
}

//显示私聊历史消息（私聊）
static void display_private_history_message(void* arg)
{
    history_item_t* item = (history_item_t*)arg;
    if(current_msg_area && current_chat_type == 0 && current_chat_id != -1)
    {
        add_message(current_msg_area,item->sender,item->content,item->is_self,item->time);
    }
    free(item->sender);
    free(item->content);
    free(item->time);
    free(item);
}

// 客户端收发线程回调函数
void *network_thread(void *arg)
{
    // 3.发送消息给服务器----
    char recv_buf[65536 * 2]; // 接收缓冲区
    size_t recv_len = 0;      // 缓冲区中有效数据长度
    size_t recv_pos = 0;      // 当前解析位置
    while (1)
    {
        // 如果缓冲区中还有位置，继续接收数据
        if (recv_len < sizeof(recv_buf))
        {
            // 4.接收服务器响应----recv
            ssize_t m = recv(sockfd, recv_buf + recv_len, sizeof(recv_buf) - recv_len, 0);
            if (m == -1)
            {
                perror("recv failed");
                close(sockfd);
                break;
            }
            if (m == 0)
            {
                printf("服务器错误退出！\n");
                break;
            }
            recv_len += m;
        }

        // 循环处理所有完整的消息
        while (recv_len - recv_pos >= 4)
        {
            // 读取长度头
            uint32_t net_len;
            memcpy(&net_len, recv_buf + recv_pos, 4);
            uint32_t msg_len = ntohl(net_len);
            if (msg_len > 65536) // 防止超大消息
            {
                printf("消息长度异常：%u\n", msg_len);
                close(sockfd);
                return NULL;
            }

            // 检查是否完整
            if (recv_len - recv_pos - 4 < msg_len)
            {
                break; // 不完整，等待更多数据
            }
            cJSON *root = cJSON_Parse(recv_buf + recv_pos + 4);

            if (!root)
            {
                printf("JSON parse error, msg: %s\n", recv_buf + recv_pos + 4);
                continue;
            }
            cJSON *type_item = cJSON_GetObjectItem(root, "type");
            if (!type_item || !cJSON_IsString(type_item))
            {
                cJSON_Delete(root);
                continue;
            }
            if (strcmp(type_item->valuestring, "LOGIN_RESP") == 0)
            {
                cJSON *item_msg = cJSON_GetObjectItem(root, "msg");
                if (cJSON_IsString(item_msg))
                {
                    char *result = item_msg->valuestring;
                    printf("%s\n", result);
                    if (strcmp(result, "登录成功") == 0)
                    {
                        lv_async_call(switch_screen_main_to_chat, NULL);
                        // 登陆成功后立即更新好友列表
                        // 给服务器发送get_friends请求，服务器返回friends_list请求
                        // 客户端执行相应函数
                        cJSON *req = cJSON_CreateObject();
                        cJSON_AddStringToObject(req, "type", "get_friends");
                        char *req_str = cJSON_PrintUnformatted(req);
                        send_with_len(sockfd, req_str);
                        free(req_str);
                        cJSON_Delete(req);

                        //预加载天气(启动线程获取天气，更新缓存)
                        pthread_t tid;
                        char* city = strdup("xian");
                        pthread_create(&tid,NULL,weather_thread_func,city);
                        pthread_detach(tid);
                    }
                    else
                    {
                        printf("登录失败\n");
                    }
                }
                else
                {
                    puts("status类型错误\n");
                }
            }
            else if(strcmp(type_item->valuestring,"REGISTER_RESP") == 0)
            {
                cJSON* stastus_item = cJSON_GetObjectItem(root,"status");
                cJSON* msg_item = cJSON_GetObjectItem(root,"msg");
                if(cJSON_IsString(stastus_item) && cJSON_IsString(msg_item))
                {
                    char* full_msg = malloc(100);
                    sprintf(full_msg,"%s",msg_item->valuestring);
                    lv_async_call(show_info_dialog,full_msg);
                }
            }
            else if (strcmp(type_item->valuestring, "add_friend_resp") == 0) // A发送请求后，返回给A的结果
            {
                printf("添加好友响应\n");
                cJSON *status_item = cJSON_GetObjectItem(root, "status");
                cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
                if (cJSON_IsString(status_item) && cJSON_IsString(msg_item))
                {
                    char *full_msg = (char *)malloc(100);
                    sprintf(full_msg, "%s", msg_item->valuestring);
                    lv_async_call(show_info_dialog, full_msg);
                }
            }
            else if (strcmp(type_item->valuestring, "friend_request") == 0) // 服务器发给B的
            {
                cJSON *from_id = cJSON_GetObjectItem(root, "from_id");
                cJSON *from_name = cJSON_GetObjectItem(root, "from_username");
                if (cJSON_IsNumber(from_id) && cJSON_IsString(from_name))
                {
                    request_info_t *info = (request_info_t *)malloc(sizeof(request_info_t));
                    info->from_id = from_id->valueint;
                    strncpy(info->from_name, from_name->valuestring, sizeof(info->from_name) - 1);
                    info->from_name[sizeof(info->from_name) - 1] = '\0';
                    lv_async_call(show_friend_request_dialog, info);
                }
            }
            else if (strcmp(type_item->valuestring, "friend_added") == 0) // 这是B同意后，由服务器返回给A的消息
            {
                cJSON *friend_id = cJSON_GetObjectItem(root, "friend_id");
                cJSON *friend_name = cJSON_GetObjectItem(root, "friend_name");
                friend_info_t *info = (friend_info_t *)malloc(sizeof(friend_info_t));
                info->id = friend_id->valueint;
                strncpy(info->name, friend_name->valuestring, sizeof(info->name) - 1);
                info->name[sizeof(info->name) - 1] = '\0';
                lv_async_call(friend_added_dialog, info);

                // 刷新好友列表
                cJSON *req = cJSON_CreateObject();
                cJSON_AddStringToObject(req, "type", "get_friends");
                char *req_str = cJSON_PrintUnformatted(req);
                send_with_len(sockfd, req_str);
                free(req_str);
                cJSON_Delete(req);
            }
            else if (strcmp(type_item->valuestring, "friend_added_failed") == 0) // 这是B拒绝后，由服务器返回给A的消息
            {
                cJSON *friend_id = cJSON_GetObjectItem(root, "friend_id");
                cJSON *friend_name = cJSON_GetObjectItem(root, "friend_name");
                friend_info_t *info = (friend_info_t *)malloc(sizeof(friend_info_t));
                info->id = friend_id->valueint;
                strncpy(info->name, friend_name->valuestring, sizeof(info->name) - 1);
                info->name[sizeof(info->name) - 1] = '\0';
                lv_async_call(friend_added_failed_dialog, info);
            }
            else if (strcmp(type_item->valuestring, "respond_friend_resp") == 0)
            {
                cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
                if (cJSON_IsString(msg_item))
                {
                    char *msg = (char *)malloc(128);
                    sprintf(msg, "对方%s", msg_item->valuestring);
                    lv_async_call(show_info_dialog, (void *)msg);
                }
                cJSON *req = cJSON_CreateObject();
                cJSON_AddStringToObject(req, "type", "get_friends");
                char *req_str = cJSON_PrintUnformatted(req);
                send_with_len(sockfd, req_str);
                cJSON_Delete(req);
                free(req_str);
            }
            else if (strcmp(type_item->valuestring, "friends_list") == 0) // 更新好友列表请求
            {
                printf("更新好友列表！\n");
                cJSON *friends_array = cJSON_GetObjectItem(root, "friends");
                if (cJSON_IsArray(friends_array))
                {
                    int count = cJSON_GetArraySize(friends_array);
                    friend_list_t *list = (friend_list_t *)malloc(sizeof(friend_list_t));
                    list->count = count;
                    list->friends = (friend_info_t *)malloc(count * sizeof(friend_info_t));
                    for (int i = 0; i < count; i++)
                    {
                        cJSON *friend = cJSON_GetArrayItem(friends_array, i);
                        cJSON *id_item = cJSON_GetObjectItem(friend, "id");
                        cJSON *name_item = cJSON_GetObjectItem(friend, "name");

                        if (cJSON_IsNumber(id_item) && cJSON_IsString(name_item))
                        {
                            list->friends[i].id = id_item->valueint;
                            strncpy(list->friends[i].name, name_item->valuestring, sizeof(list->friends[i].name) - 1);
                            list->friends[i].name[sizeof(list->friends[i].name) - 1] = '\0';
                        }
                    }
                    lv_async_call((lv_async_cb_t)update_friend_list_ui, list);
                }
            }
            else if (strcmp(type_item->valuestring, "private_msg") == 0) // 私聊请求
            {
                cJSON *from_id_item = cJSON_GetObjectItem(root, "from_id");
                cJSON *from_name_item = cJSON_GetObjectItem(root, "from_name");
                cJSON *content_item = cJSON_GetObjectItem(root, "content");
                cJSON* timestamp_item = cJSON_GetObjectItem(root,"time_stamp");
                if (cJSON_IsNumber(from_id_item) && cJSON_IsString(from_name_item) && cJSON_IsString(content_item))
                {
                    int from_id = from_id_item->valueint;
                    char *from_name = from_name_item->valuestring;
                    char *content = content_item->valuestring;
                    char* timestamp = timestamp_item->valuestring;
                    if (from_id == current_chat_friend_id) // 如果是当前正在聊天的好友，则立即显示消息
                    {
                        history_item_t *item = (history_item_t *)malloc(sizeof(history_item_t));
                        item->sender = strdup(from_name);
                        item->content = strdup(content);
                        item->time = strdup(timestamp);
                        item->is_self = false;
                        lv_async_call(display_private_history_message, item);
                    }
                    else
                    {
                        printf("收到来自%s的消息，但不在当前聊天界面\n", from_name);
                    }
                }
            }
            else if (strcmp(type_item->valuestring, "file_start") == 0) // 开始传输文件请求
            {
                char *file_id = cJSON_GetObjectItem(root, "file_id")->valuestring;
                char *filename = cJSON_GetObjectItem(root, "filename")->valuestring;
                int total_chunks = cJSON_GetObjectItem(root, "total_chunks")->valueint;
                char* from_name = cJSON_GetObjectItem(root,"from_name")->valuestring;
                cJSON* msg_type_item = cJSON_GetObjectItem(root,"msg_type");
                char* msg_type = msg_type_item ? msg_type_item->valuestring : "file";//消息类型，默认为file
                int duration = 0;
                cJSON* dur_item = cJSON_GetObjectItem(root,"duration");
                if(dur_item && cJSON_IsNumber(dur_item))
                {
                    duration = dur_item->valueint;
                }
                handle_file_start_ex(file_id,filename,total_chunks,from_name,msg_type,duration);
            }
            else if (strcmp(type_item->valuestring, "file_data") == 0) // 传输文件数据块
            {
                char *file_id = cJSON_GetObjectItem(root, "file_id")->valuestring;
                int chunk_index = cJSON_GetObjectItem(root, "chunk_index")->valueint;
                char *data_b64 = cJSON_GetObjectItem(root, "data")->valuestring;
                handle_file_data(file_id, chunk_index, data_b64);
            }
            else if (strcmp(type_item->valuestring, "file_end") == 0) // 传输文件结束
            {
                char *file_id = cJSON_GetObjectItem(root, "file_id")->valuestring;
                int success = cJSON_GetObjectItem(root, "success")->valueint;
                handle_file_end(file_id, success);
            }
            else if(strcmp(type_item->valuestring,"groups_list") == 0)
            {
                //收到群列表，更新UI
                cJSON *groups = cJSON_GetObjectItem(root,"groups");
                if(cJSON_IsArray(groups))
                {
                    //深拷贝一份，后续会删除root
                    cJSON* groups_copy = cJSON_Duplicate(groups,1);
                    lv_async_call(update_group_list_ui,groups_copy);
                }
            }
            else if(strcmp(type_item->valuestring,"create_group_resp") == 0)
            {
                //创建群聊响应
                cJSON* status = cJSON_GetObjectItem(root,"status");
                cJSON* msg = cJSON_GetObjectItem(root,"msg");
                if(cJSON_IsString(status) && cJSON_IsString(msg))
                {
                    char *full_msg = malloc(128);
                    sprintf(full_msg,"%s",msg->valuestring);
                    lv_async_call(show_info_dialog,full_msg);

                    //如果成功，刷新群列表
                    if(strcmp(status->valuestring,"success") == 0)
                    {
                        //主动请求刷新群列表
                        cJSON* req = cJSON_CreateObject();
                        cJSON_AddStringToObject(req,"type","get_groups");
                        char* req_str = cJSON_PrintUnformatted(req);
                        send_with_len(sockfd,req_str);
                        free(req_str);
                        cJSON_Delete(req);
                    }
                }
            }
            else if(strcmp(type_item->valuestring,"join_group_resp") == 0)
            {
                //加入群聊响应
                cJSON* status = cJSON_GetObjectItem(root,"status");
                cJSON* msg = cJSON_GetObjectItem(root,"msg");
                if(cJSON_IsString(status) && cJSON_IsString(msg))
                {
                    char* full_msg = (char*)malloc(128);
                    sprintf(full_msg,"%s",msg->valuestring);
                    lv_async_call(show_info_dialog,full_msg);
                    if(strcmp(status->valuestring,"success") == 0)
                    {
                        //刷新群列表
                        cJSON* req = cJSON_CreateObject();
                        cJSON_AddStringToObject(req,"type","get_groups");
                        char* req_str = cJSON_PrintUnformatted(req);
                        send_with_len(sockfd,req_str);
                        free(req_str);
                        cJSON_Delete(req);
                    }
                }
            }
            else if(strcmp(type_item->valuestring,"group_msg") == 0)
            {
                //收到群消息
                cJSON* from_id = cJSON_GetObjectItem(root,"from_id");
                cJSON* from_name = cJSON_GetObjectItem(root,"from_name");
                cJSON* group_id = cJSON_GetObjectItem(root,"group_id");
                cJSON* content = cJSON_GetObjectItem(root,"content");
                cJSON* timestamp = cJSON_GetObjectItem(root,"time_stamp");
                if(cJSON_IsNumber(from_id) && cJSON_IsString(from_name) && cJSON_IsNumber(group_id) && cJSON_IsString(content) && cJSON_IsString(timestamp))
                {
                    int gid = group_id->valueint;
                    //如果当前正在聊天的群正是此群,则立刻显示
                    if(current_chat_type == 1 && current_chat_id == gid)
                    {
                        history_item_t * item = (history_item_t*)malloc(sizeof(history_item_t));
                        item->sender = strdup(from_name->valuestring);
                        item->content = strdup(content->valuestring);
                        item->time = strdup(timestamp->valuestring);
                        item->is_self = false;
                        lv_async_call(display_history_message,item);
                    }
                    else
                    {
                        //todo：更新未读消息标志
                    }
                }
            }
            else if(strcmp(type_item->valuestring,"group_history") == 0)
            {
                printf("收到group_history响应\n");
                //收到群历史消息
                cJSON* messages = cJSON_GetObjectItem(root,"messages");
                if(cJSON_IsArray(messages))
                {
                    int size = cJSON_GetArraySize(messages);
                    printf("消息数量：%d\n",size);
                    for(int i = 0;i<size;i++)
                    {
                        cJSON* msg = cJSON_GetArrayItem(messages,i);
                        cJSON* from_id = cJSON_GetObjectItem(msg,"from_id");
                        cJSON* from_name = cJSON_GetObjectItem(msg,"from_name");
                        cJSON* content = cJSON_GetObjectItem(msg,"content");
                        cJSON* timestamp = cJSON_GetObjectItem(msg,"timestamp");
                        cJSON* is_self = cJSON_GetObjectItem(msg,"is_self");
                        if(cJSON_IsNumber(from_id) && cJSON_IsString(from_name) && cJSON_IsString(content)\
                         && cJSON_IsString(timestamp) && cJSON_IsBool(is_self))
                        {
                            //构造消息数据并显示
                            history_item_t *item = (history_item_t *)malloc(sizeof(history_item_t));
                            item->sender = strdup(from_name->valuestring);
                            item->content = strdup(content->valuestring);
                            item->time = strdup(timestamp->valuestring);
                            item->is_self = is_self->valueint;
                            lv_async_call(display_history_message,item);
                        }
                    }
                }
            }
            else if(strcmp(type_item->valuestring,"history") == 0)
            {
                //收到私聊历史消息
                cJSON* messages = cJSON_GetObjectItem(root,"messages");
                if(cJSON_IsArray(messages))
                {
                    int size = cJSON_GetArraySize(messages);
                    for(int i = 0;i<size;i++)
                    {
                        cJSON* msg = cJSON_GetArrayItem(messages,i);
                        cJSON* from_id = cJSON_GetObjectItem(msg,"from_id");
                        cJSON* content = cJSON_GetObjectItem(msg,"content");
                        cJSON* timestamp = cJSON_GetObjectItem(msg,"timestamp");
                        cJSON* is_self = cJSON_GetObjectItem(msg,"is_self");
                        if(cJSON_IsNumber(from_id) && cJSON_IsString(content) && cJSON_IsString(timestamp) && cJSON_IsBool(is_self))
                        {
                            //构造消息数据并显示
                            history_item_t* item = (history_item_t*)malloc(sizeof(history_item_t));
                            char* sender_name = NULL;
                            if(is_self->valueint)
                            {
                                sender_name = my_username;
                            }
                            else
                            {
                                //从好友缓存中找
                                for(int j = 0;j < cached_friends_count;j++)
                                {
                                    if(cached_friends[j].id == from_id->valueint)
                                    {
                                        sender_name = cached_friends[j].name;
                                        break;
                                    }
                                }

                                if(!sender_name) sender_name = "未知";         
                            }

                            item->sender = strdup(sender_name);
                            item->content = strdup(content->valuestring);
                            item->time = strdup(timestamp->valuestring);
                            item->is_self = is_self->valueint;
                            lv_async_call(display_private_history_message,item);
                        }
                    }
                }
            }
            else if(strcmp(type_item->valuestring,"offline_messages") == 0)
            {
                //收到离线消息(私聊)
                cJSON* offline = cJSON_GetObjectItem(root,"offline");
                if(cJSON_IsArray(offline))
                {
                    int count = cJSON_GetArraySize(offline);
                    for(int i = 0;i<count ;i++)
                    {
                        cJSON* item = cJSON_GetArrayItem(offline,i);
                        cJSON* friend_id = cJSON_GetObjectItem(item,"friend_id");
                        cJSON* messages = cJSON_GetObjectItem(item,"messages");
                        if(cJSON_IsNumber(friend_id) && cJSON_IsArray(messages))
                        {
                            int fid = friend_id->valueint;
                            int msg_count = cJSON_GetArraySize(messages);
                            for(int j = 0;j < msg_count;j++)
                            {
                                cJSON* msg = cJSON_GetArrayItem(messages,j);
                                cJSON* content = cJSON_GetObjectItem(msg,"content");
                                cJSON* timestamp = cJSON_GetObjectItem(msg,"timestamp");
                                if(cJSON_IsString(content) && cJSON_IsString(timestamp))
                                {
                                    //获取好友名
                                    char* friend_name = NULL;
                                    for(int k = 0;k < cached_friends_count;k++)
                                    {
                                        if(cached_friends[k].id == fid)
                                        {
                                            friend_name = cached_friends[k].name;
                                            break;
                                        }
                                    }

                                    if(!friend_name) friend_name = "未知";

                                    //如果当前聊天正是该好友，则立刻显示
                                    if(current_chat_type == 0 && current_chat_id == fid)
                                    {
                                        history_item_t* item = (history_item_t*)malloc(sizeof(history_item_t));
                                        item->sender = strdup(friend_name);
                                        item->content = strdup(content->valuestring);
                                        item->time = strdup(timestamp->valuestring);
                                        item->is_self = false;
                                        lv_async_call(display_private_history_message,item);
                                    }
                                    else
                                    {
                                        //todo:更新缓存或更新未读标记
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                printf("未知类型\n");
            }

            cJSON_Delete(root);
            recv_pos += 4 + msg_len;
        }
        //如果已解析完所有数据，将剩余数据移到缓冲区开头
        if(recv_pos > 0)
        {
            if(recv_len > recv_pos)
            {
                memmove(recv_buf,recv_buf + recv_pos,recv_len - recv_pos);
                recv_len -= recv_pos;
            }
            else
            {
                recv_len = 0;
            }
            recv_pos = 0;
        }
        usleep(1000);
    }
    close(sockfd);
    return NULL;
}

// 登录按钮回调函数
static void login_btn_cb(lv_event_t *e)
{
    printf("请求登录\n");
    const char *username = lv_textarea_get_text(g_ta1);
    const char *password = lv_textarea_get_text(g_ta2);
    if (strlen(username) == 0 || strlen(password) == 0)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "请输入账号和密码", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 20);
        lv_obj_center(msg);
        return;
    }
    strncpy(my_username, username, sizeof(my_username) - 1);
    my_username[sizeof(my_username) - 1] = '\0';

    // 更新用户账号信息
    lv_label_set_text(account_info, my_username);

    printf("账号：%s，密码：%s\n", username, password);
    // 构建JSON字符串
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "login");
    cJSON_AddStringToObject(req, "username", username);
    cJSON_AddStringToObject(req, "password", password);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);
}

static void label_register_event_cb(lv_event_t *e)
{
    printf("请求注册\n");
    const char *username = lv_textarea_get_text(g_ta1);
    const char *password = lv_textarea_get_text(g_ta2);
    if (strlen(username) == 0 || strlen(password) == 0)
    {
        lv_obj_t *msg = lv_msgbox_create(NULL, "提示", "请输入账号和密码", NULL, true);
        obj_font_set(msg, FONT_TYPE_CN, 20);
        lv_obj_center(msg);
    }
    printf("账号：%s，密码：%s\n", username, password);
    // 构建JSON字符串
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "register");
    cJSON_AddStringToObject(req, "username", username);
    cJSON_AddStringToObject(req, "password", password);
    char *req_str = cJSON_PrintUnformatted(req);
    send_with_len(sockfd, req_str);
    free(req_str);
    cJSON_Delete(req);
}

void init_page_main(void)
{
    com_style_init();
    // 设置登陆界面背景图
    lv_obj_t *background = lv_img_create(lv_scr_act());
    lv_img_set_src(background, GET_IMAGE_PATH("background.png"));
    lv_img_set_zoom(background, 1100);
    lv_obj_center(background);
    // 创建页面对象
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_add_style(cont, &com_style, 0);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_size(cont, 440, 636);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
    // 头像
    lv_obj_t *contHead = lv_obj_create(cont);
    lv_obj_add_style(contHead, &com_style, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(contHead, true, 0);
    lv_obj_clear_flag(contHead, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(contHead, 90, 0);
    lv_obj_set_style_bg_color(contHead, lv_color_hex(0xffffff), 0);
    lv_obj_set_size(contHead, 110, 110);
    lv_obj_set_style_border_color(contHead, lv_color_hex(0xffffff), 0);
    lv_obj_align(contHead, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_t *headImg = lv_img_create(contHead);
    lv_img_set_src(headImg, GET_IMAGE_PATH("qqImg.png"));
    lv_img_set_zoom(headImg, 90);
    lv_obj_align(headImg, LV_ALIGN_CENTER, 0, 0);

    // 设置账号文本框
    g_ta1 = lv_textarea_create(cont);
    lv_obj_add_style(g_ta1, &com_style, 0);
    lv_obj_set_style_radius(g_ta1, 15, 0);
    lv_obj_clear_state(g_ta1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(g_ta1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(g_ta1, lv_color_hex(0xffffff), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_top(g_ta1, 2, 0);
    lv_obj_set_style_text_align(g_ta1, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_one_line(g_ta1, true);
    lv_obj_set_style_bg_color(g_ta1, lv_color_hex(0x483545), LV_PART_MAIN);
    lv_obj_set_size(g_ta1, 355, 50);
    lv_obj_align(g_ta1, LV_ALIGN_TOP_LEFT, 45, 210);
    obj_font_set(g_ta1, FONT_TYPE_CN, 26);
    lv_textarea_set_placeholder_text(g_ta1, "输入QQ号");

    // 设置密码文本框
    g_ta2 = lv_textarea_create(cont);
    lv_obj_add_style(g_ta2, &com_style, 0);
    lv_obj_set_style_radius(g_ta2, 15, 0);
    lv_obj_clear_state(g_ta2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(g_ta2, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(g_ta2, lv_color_hex(0xffffff), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_top(g_ta2, 2, 0);
    lv_obj_set_style_text_align(g_ta2, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_one_line(g_ta2, true);
    lv_obj_set_style_bg_color(g_ta2, lv_color_hex(0x483545), LV_PART_MAIN);
    lv_obj_set_size(g_ta2, 355, 50);
    lv_obj_align(g_ta2, LV_ALIGN_TOP_LEFT, 45, 280);
    obj_font_set(g_ta2, FONT_TYPE_CN, 26);
    lv_textarea_set_password_mode(g_ta2, true);
    lv_textarea_set_placeholder_text(g_ta2, "输入QQ密码");

    // 登陆按钮
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_add_event_cb(btn, login_btn_cb, LV_EVENT_CLICKED, g_ta1);
    lv_obj_set_size(btn, 355, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 45, 425);
    lv_obj_t *label = lv_label_create(btn);
    obj_font_set(label, FONT_TYPE_CN, 20);
    lv_label_set_text(label, "登录");
    lv_obj_center(label);

    // 注册按钮
    lv_obj_t *label_register = lv_label_create(cont);
    obj_font_set(label_register, FONT_TYPE_CN, 20);
    lv_obj_align_to(label_register, btn, LV_ALIGN_OUT_TOP_MID, -22, -25);
    lv_obj_set_style_text_color(label_register, lv_color_hex(0x1E90FF), 0);
    lv_label_set_text(label_register, "注册账号");
    lv_obj_add_flag(label_register, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label_register, label_register_event_cb, LV_EVENT_CLICKED, NULL);
}
