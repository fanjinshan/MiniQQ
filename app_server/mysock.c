#include "mysock.h"

// 在线用户映射表
int online_fd[10001] = {-1};
pthread_mutex_t online_lock = PTHREAD_MUTEX_INITIALIZER;

// 确保发送所有数据的函数
int send_all(int sockfd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0)
    {
        ssize_t sent = send(sockfd, p, left, 0);
        if (sent <= 0)
            return -1;
        p += sent;
        left -= sent;
    }
    return 0;
}

// 封装带长度头的发送函数
void send_with_len(int sockfd, const char *data)
{
    if (!data)
        return;
    uint32_t len = strlen(data);
    uint32_t net_len = htonl(len);
    if (send_all(sockfd, &net_len, 4) == -1)
        return;
    send_all(sockfd, data, len);
}

// 读取用户
static cJSON *read_users(void)
{
    FILE *fp = fopen("/home/zhb/T113/app_sdk/app_server/users.json", "r");
    if (!fp)
    {
        // 文件为空，则返回空数组
        return cJSON_CreateArray();
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp); // 将文件指针重置到文件头部
    char *buf = (char *)malloc(size + 1);
    if (!buf)
    {
        perror("malloc failed");
        fclose(fp);
        return cJSON_CreateArray();
    }
    fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    if (!json)
    {
        json = cJSON_CreateArray();
    }
    return json;
}

// 注册用户
static void write_users(cJSON *users)
{
    // 将JSON对象转换为格式化字符串
    char *str = cJSON_Print(users);
    FILE *fp = fopen("/home/zhb/T113/app_sdk/app_server/users.json", "w");
    if (fp)
    {
        fprintf(fp, "%s", str);
        fclose(fp);
    }
    free(str);
}

// 读取好友关系文件
static cJSON *read_friends(void)
{
    FILE *fp = fopen("/home/zhb/T113/app_sdk/app_server/friends.json", "r");
    if (!fp)
        return cJSON_CreateArray(); // 返回空数组
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    char *buf = (char *)malloc(size + 1);
    fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json ? json : cJSON_CreateArray();
}

// 写入好友关系文件
static void write_friends(cJSON *friends)
{
    char *str = cJSON_Print(friends);
    FILE *fp = fopen("/home/zhb/T113/app_sdk/app_server/friends.json", "w");
    if (fp)
    {
        fprintf(fp, "%s", str);
        fclose(fp);
    }
    free(str);
}

// 根据用户名查找用户id
static int find_user_id_by_username(const char *username)
{
    cJSON *users = read_users();
    int size = cJSON_GetArraySize(users);
    int id = -1;
    for (int i = 0; i < size; i++)
    {
        // 遍历每个用户
        cJSON *user = cJSON_GetArrayItem(users, i);
        // 获取每个用户的用户名项
        cJSON *u = cJSON_GetObjectItem(user, "username");
        if (strcmp(u->valuestring, username) == 0)
        {
            // 找到匹配的用户名，获取该用户的id
            cJSON *id_item = cJSON_GetObjectItem(user, "id");
            if (id_item)
            {
                id = id_item->valueint;
                break;
            }
        }
    }
    cJSON_Delete(users);
    return id;
}

// 根据用户id查找用户名(返回的是动态分配的内存，谁调用就需要free)
static char *get_username_by_id(int user_id)
{
    cJSON *users = read_users();
    int size = cJSON_GetArraySize(users);
    char *name = NULL;
    for (int i = 0; i < size; i++)
    {
        // 遍历每个用户
        cJSON *user = cJSON_GetArrayItem(users, i);
        // 获取每个用户的id项
        cJSON *id_item = cJSON_GetObjectItem(user, "id");
        if (id_item && id_item->valueint == user_id)
        {
            // 找到匹配id，获取该用户的用户名
            cJSON *u = cJSON_GetObjectItem(user, "username");
            if (u)
            {
                // strdup:自动动态分配足够的内存，并将字符串复制到该内存中，需要手动free
                name = strdup(u->valuestring);
                break;
            }
        }
    }
    cJSON_Delete(users);
    return name;
}

// 1.根据给定的ip和port初始化服务端的套接字描述符
int mysock_init(const char *ip, uint16_t port)
{
    // 1.创建socket描述符----socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket failed");
        return -1;
    }
    // 2.绑定自己的地址信息----bind
    SockAddrIn addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    // inet_aton(SERVER_IP,&addr.sin_addr.s_addr);//点分十进制字符串转网络字节序

    // 解决重启服务器时需要更换端口的问题
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt reuseaddr failed");
        close(sockfd);
        return -1;
    }
    int r = bind(sockfd, (SockAddr *)&addr, sizeof(addr));
    if (r == -1)
    {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    // 3.监听客户端的连接----listen
    r = listen(sockfd, 3);
    if (r == -1)
    {
        perror("listen failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// 2.根据套接字描述符建立连接，并返回已经连接上的客户端套接字描述符
int mysock_build_connect(int sockfd)
{
    // 4.接收客户端的连接----accept
    SockAddrIn clientAddr;
    socklen_t len = (unsigned int)sizeof(clientAddr); // 大转小
    int c_sockfd = accept(sockfd, (SockAddr *)&clientAddr, &len);
    printf("c_sockfd:%d\n", c_sockfd);
    if (c_sockfd == -1)
    {
        perror("accept failed");
        return -1;
    }

    // 解析客户端的ip和端口号
    uint16_t port = ntohs(clientAddr.sin_port);
    char *ip = inet_ntoa(clientAddr.sin_addr);
    printf("[%s:%hu]的客户端已经连接到服务器\n", ip, port);
    return c_sockfd;
}

// 3.关闭套接字描述符
int mysock_close(int sockfd)
{
    return close(sockfd);
}

// 处理添加好友请求(第一次调用服务器)
// current_user_id是A的id，c_sockfd是A的描述符，root中存放的是B的账号
static void handle_add_friend(int current_user_id, cJSON *root, int c_sockfd)
{
    cJSON *friend_username = cJSON_GetObjectItem(root, "friend_username");
    if (!cJSON_IsString(friend_username))
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "add_friend_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "用户名格式错误");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // target_name 是B的账号
    const char *target_name = friend_username->valuestring;
    // 检查是否是自己
    char *my_name = get_username_by_id(current_user_id);
    if (my_name && strcmp(my_name, target_name) == 0)
    {
        free(my_name);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "add_friend_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "不能添加自己为好友");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    if (my_name)
        free(my_name);
    // target_id是B的id
    int target_id = find_user_id_by_username(target_name);
    if (target_id == -1)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "add_friend_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "目标用户不存在或不在线");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 检查目标用户是否已经存在好友关系(pending或accepted)
    cJSON *friends = read_friends();
    int size = cJSON_GetArraySize(friends);
    // 是否有好友关系标志位(待定(pending)或者已经是好友(accepted))
    int already = 0;
    for (int i = 0; i < size; i++)
    {
        cJSON *rel = cJSON_GetArrayItem(friends, i);
        cJSON *uid = cJSON_GetObjectItem(rel, "user_id");
        cJSON *fid = cJSON_GetObjectItem(rel, "friend_id");
        if (uid && fid && uid->valueint == current_user_id && fid->valueint == target_id)
        {
            already = 1;
            break;
        }
    }

    // 如果已经存在好友关系
    if (already)
    {
        cJSON_Delete(friends);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "add_friend_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "已经是好友或请求已发送");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 执行到此，正式将添加好友请求写入到friends.json文件中
    cJSON *new_req = cJSON_CreateObject();
    cJSON_AddNumberToObject(new_req, "user_id", current_user_id);
    cJSON_AddNumberToObject(new_req, "friend_id", target_id);
    cJSON_AddStringToObject(new_req, "status", "pending");
    cJSON_AddItemToArray(friends, new_req);
    write_friends(friends);
    cJSON_Delete(friends);

    // 通知对方(如果在线)
    pthread_mutex_lock(&online_lock);
    int target_fd = online_fd[target_id];
    pthread_mutex_unlock(&online_lock);
    if (target_fd != -1)
    {
        cJSON *notify = cJSON_CreateObject();
        cJSON_AddStringToObject(notify, "type", "friend_request");
        cJSON_AddNumberToObject(notify, "from_id", current_user_id);
        char *from_name = get_username_by_id(current_user_id);
        cJSON_AddStringToObject(notify, "from_username", from_name ? from_name : "");
        if (from_name)
            free(from_name);
        char *s = cJSON_PrintUnformatted(notify);
        send_with_len(target_fd, s);
        free(s);
        cJSON_Delete(notify);
    }

    // 向请求方返回请求消息发送成功(仅仅是发送成功，仍需等待接收方处理)
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "add_friend_resp");
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddStringToObject(resp, "msg", "好友请求已发送");
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
}

// 处理好友请求响应(第二次调用服务器，同意/拒绝)
// current_user_id是B的id，root是B发送给服务器的，root中存储的from_id是A的，c_sockfd是B的
static void handle_respond_friend(int current_user_id, cJSON *root, int c_sockfd)
{
    if (current_user_id == -1)
        return;
    cJSON *from_id_item = cJSON_GetObjectItem(root, "from_id");
    cJSON *action_item = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsNumber(from_id_item) || !cJSON_IsString(action_item))
        return;

    // A的id
    int from_id = from_id_item->valueint;
    const char *action = action_item->valuestring;

    cJSON *friends = read_friends();
    int size = cJSON_GetArraySize(friends);
    int found = -1;
    for (int i = 0; i < size; i++)
    {
        cJSON *rel = cJSON_GetArrayItem(friends, i);
        cJSON *uid = cJSON_GetObjectItem(rel, "user_id");
        cJSON *fid = cJSON_GetObjectItem(rel, "friend_id");
        cJSON *st = cJSON_GetObjectItem(rel, "status");
        if (uid && fid && st && uid->valueint == from_id && fid->valueint == current_user_id && strcmp(st->valuestring, "pending") == 0)
        {
            found = i;
            break;
        }
    }
    if (found == -1)
    {
        cJSON_Delete(friends);
        return;
    }

    if (strcmp(action, "accept") == 0)
    {
        // 修改为accepted
        cJSON *rel = cJSON_GetArrayItem(friends, found);
        cJSON_SetValuestring(cJSON_GetObjectItem(rel, "status"), "accepted");
        // 添加反向记录(B将选择结果发送给服务器，由服务器将好友关系写入文件)
        cJSON *new_rel = cJSON_CreateObject();
        cJSON_AddNumberToObject(new_rel, "user_id", current_user_id); // B的id
        cJSON_AddNumberToObject(new_rel, "friend_id", from_id);       // A的id
        cJSON_AddStringToObject(new_rel, "status", "accepted");
        cJSON_AddItemToArray(friends, new_rel);
        write_friends(friends);

        // 通知发起方
        pthread_mutex_lock(&online_lock);
        int target_fd = online_fd[from_id];
        pthread_mutex_unlock(&online_lock);
        // 如果发起方还在线的话(给A发)
        if (target_fd != -1)
        {
            cJSON *notify = cJSON_CreateObject();
            cJSON_AddStringToObject(notify, "type", "friend_added"); // 已经添加为好友
            cJSON_AddNumberToObject(notify, "friend_id", current_user_id);
            char *name = get_username_by_id(current_user_id);
            cJSON_AddStringToObject(notify, "friend_name", name ? name : "");
            if (name)
                free(name);
            char *s = cJSON_PrintUnformatted(notify);
            send_with_len(target_fd, s);
            free(s);
            cJSON_Delete(notify);
        }
        // 返回成功（给B发）
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "respond_friend_resp");
        cJSON_AddStringToObject(resp, "status", "success");
        cJSON_AddStringToObject(resp, "msg", "已同意好友请求");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
    }
    else if (strcmp(action, "reject") == 0)
    {
        // 通知发起方
        pthread_mutex_lock(&online_lock);
        int target_fd = online_fd[from_id];
        pthread_mutex_unlock(&online_lock);
        // 如果发起方还在线的话(给A发)
        if (target_fd != -1)
        {
            cJSON *notify = cJSON_CreateObject();
            cJSON_AddStringToObject(notify, "type", "friend_added_failed"); // 拒绝添加为好友
            cJSON_AddNumberToObject(notify, "friend_id", current_user_id);
            char *name = get_username_by_id(current_user_id);
            cJSON_AddStringToObject(notify, "friend_name", name ? name : "");
            if (name)
                free(name);
            char *s = cJSON_PrintUnformatted(notify);
            send_with_len(target_fd, s);
            free(s);
            cJSON_Delete(notify);
        }
        // 删除记录
        cJSON_DeleteItemFromArray(friends, found);
        write_friends(friends);
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "respond_friend_resp");
        cJSON_AddStringToObject(resp, "status", "success");
        cJSON_AddStringToObject(resp, "msg", "已拒绝好友请求");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s); // 给B返回结果
        free(s);
        cJSON_Delete(resp);
    }
    cJSON_Delete(friends);
}

static void handle_get_friends(int current_user_id, int c_sockfd)
{
    cJSON *friends = read_friends();
    cJSON *friends_list = cJSON_CreateArray();
    int size = cJSON_GetArraySize(friends);
    for (int i = 0; i < size; i++)
    {
        cJSON *rel = cJSON_GetArrayItem(friends, i);
        cJSON *uid = cJSON_GetObjectItem(rel, "user_id");
        cJSON *fid = cJSON_GetObjectItem(rel, "friend_id");
        cJSON *st = cJSON_GetObjectItem(rel, "status");
        if (uid && fid && current_user_id == uid->valueint && strcmp(st->valuestring, "accepted") == 0)
        {
            int friend_id = fid->valueint;
            char *friend_name = get_username_by_id(friend_id);
            if (friend_name)
            {
                cJSON *friend_item = cJSON_CreateObject();
                cJSON_AddNumberToObject(friend_item, "id", friend_id);
                cJSON_AddStringToObject(friend_item, "name", friend_name);
                cJSON_AddItemToArray(friends_list, friend_item);
                free(friend_name);
            }
        }
    }
    cJSON_Delete(friends);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "friends_list");
    cJSON_AddItemToObject(resp, "friends", friends_list);
    char *msg = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, msg);
    free(msg);
    cJSON_Delete(resp);
}

// 处理私聊消息
static void handle_private_msg(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    if (current_user_id == -1)
        return;

    cJSON *to_id_item = cJSON_GetObjectItem(root, "to_id");
    cJSON *content_item = cJSON_GetObjectItem(root, "content");
    if (!cJSON_IsNumber(to_id_item) || !cJSON_IsString(content_item))
        return;

    int to_id = to_id_item->valueint;
    const char *content = content_item->valuestring;

    char *from_name = get_username_by_id(current_user_id);
    if (!from_name)
        return;

    time_t now = time(NULL);
    // 存储消息到数据库
    db_store_private_msg(db_conn, current_user_id, to_id, content, now);

    // 获取接收方socket套接字
    pthread_mutex_lock(&online_lock);
    int target_fd = online_fd[to_id];
    pthread_mutex_unlock(&online_lock);

    if (target_fd != -1)
    {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "private_msg");
        cJSON_AddNumberToObject(msg, "from_id", current_user_id);
        cJSON_AddStringToObject(msg, "from_name", from_name);
        cJSON_AddStringToObject(msg, "content", content);
        char time_buf[20];
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        cJSON_AddStringToObject(msg, "time_stamp", time_buf);
        char *s = cJSON_PrintUnformatted(msg);
        send_with_len(target_fd, s);
        free(s);
        cJSON_Delete(msg);
    }
    else
    {
        printf("该用户%d不在线\n", to_id);
    }
    free(from_name);
}

// 处理创建群聊请求
static void handle_create_group(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    // 解析群名称
    cJSON *name_item = cJSON_GetObjectItem(root, "name");
    // 添加日志
    if (name_item)
    {
        printf("创建群聊: name = %s, type = %d\n", name_item->valuestring, name_item->type);
    }
    else
    {
        printf("创建群聊: name_item 为 NULL\n");
    }

    if (!cJSON_IsString(name_item))
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "create_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "群名称不能为空");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    int group_id;
    // 调用数据库函数创建群聊
    if (db_create_group(db_conn, name_item->valuestring, current_user_id, &group_id) != 0)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "create_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "创建群聊失败");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 创建成功，返回群ID
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "create_group_resp");
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddNumberToObject(resp, "group_id", group_id);
    cJSON_AddStringToObject(resp, "msg", "创建成功");
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 处理加入群聊请求
static void handle_join_group(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    // 解析群ID
    cJSON *group_id_item = cJSON_GetObjectItem(root, "group_id");
    if (!cJSON_IsNumber(group_id_item))
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "join_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "群ID无效");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }
    int group_id = group_id_item->valueint;

    // 添加成员到群，角色为普通成员
    if (db_add_group_member(db_conn, group_id, current_user_id, "member") != 0)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "join_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "加入失败，可能已在群中或者群不存在");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 返回成功
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "join_group_resp");
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddStringToObject(resp, "msg", "加入成功");
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 处理获取群聊列表请求
static void handle_get_groups(int current_user_id, int c_sockfd, MYSQL *db_conn)
{
    // 从数据库获取用户的所有群聊
    char *groups_json = db_get_user_groups(db_conn, current_user_id);
    if (!groups_json)
    {
        groups_json = strdup("[]"); // 失败时返回空数组
    }
    cJSON *groups = cJSON_Parse(groups_json);
    free(groups_json);

    // 构造响应JSON
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "groups_list");
    cJSON_AddItemToObject(resp, "groups", groups);
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 处理群聊消息
static void handle_group_msg(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    printf("handle_group_msg:received\n");
    // 解析群ID和消息内容
    cJSON *group_id_item = cJSON_GetObjectItem(root, "group_id");
    cJSON *content_item = cJSON_GetObjectItem(root, "content");
    if (!cJSON_IsNumber(group_id_item) || !cJSON_IsString(content_item))
    {
        printf("handle_group_msg:invalid params\n");
        return;
    }

    int group_id = group_id_item->valueint;
    const char *content = content_item->valuestring;
    printf("handle_group_msg:group_id = %d,content= %s\n", group_id, content);

    // 获取发送者用户名
    char *from_name = get_username_by_id(current_user_id);
    if (!from_name)
        return;

    // 存储消息到数据库
    time_t now = time(NULL);
    int ret = db_store_group_msg(db_conn, current_user_id, group_id, content, now);
    if (ret == 0)
    {
        // 验证插入是否可见
        MYSQL_STMT *test_stmt = mysql_stmt_init(db_conn);
        const char *test_sql = "SELECT COUNT(*) FROM messages WHERE group_id = ? AND is_group = 1";
        if (test_stmt)
        {
            if (mysql_stmt_prepare(test_stmt, test_sql, strlen(test_sql)) == 0)
            {
                MYSQL_BIND test_bind;
                memset(&test_bind, 0, sizeof(test_bind));
                test_bind.buffer_type = MYSQL_TYPE_LONG;
                test_bind.buffer = &group_id;
                if (mysql_stmt_bind_param(test_stmt, &test_bind) == 0 && mysql_stmt_execute(test_stmt) == 0)
                {
                    MYSQL_BIND test_result;
                    int count = 0;
                    memset(&test_result, 0, sizeof(test_result));
                    test_result.buffer_type = MYSQL_TYPE_LONG;
                    test_result.buffer = &count;
                    mysql_stmt_bind_result(test_stmt, &test_result);
                    mysql_stmt_store_result(test_stmt);
                    if (mysql_stmt_fetch(test_stmt) == 0)
                    {
                        printf("AFTER INSERT:count for group_id %d = %d\n", group_id, count);
                    }
                    else
                    {
                        printf("AFTER INSERT:fetch failed\n");
                    }
                }
            }
            mysql_stmt_close(test_stmt);
        }
    }
    if (ret != 0)
    {
        fprintf(stderr, "db_store_group_msg FAILED for group_id = %d, user_id = %d\n", group_id, current_user_id);
        free(from_name);
        return;
    }
    printf("db_store_group_msg success\n");

    // 获取群所有成员ID
    int *members = NULL;
    int member_count = 0;
    if (db_get_group_members(db_conn, group_id, &members, &member_count) != 0 || member_count == 0)
    {
        free(from_name);
        return;
    }

    // 构造要转发的消息
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "group_msg");
    cJSON_AddNumberToObject(msg, "from_id", current_user_id);
    cJSON_AddStringToObject(msg, "from_name", from_name);
    cJSON_AddNumberToObject(msg, "group_id", group_id);
    cJSON_AddStringToObject(msg, "content", content);
    char time_buf[20];
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    cJSON_AddStringToObject(msg, "time_stamp", time_buf);
    char *msg_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    printf("群消息转发：group_id:%d,from=%d,member_count=%d\n", group_id, current_user_id, member_count);
    // 遍历所有成员,向在线成员转发消息
    for (int i = 0; i < member_count; i++)
    {
        int member_id = members[i];
        if (member_id == current_user_id)
            continue; // 不给自己发
        pthread_mutex_lock(&online_lock);
        int target_id = online_fd[member_id];
        pthread_mutex_unlock(&online_lock);
        if (target_id != -1)
        {
            printf("转发给成员%d，fd=%d\n", member_id, target_id);
            send_with_len(target_id, msg_str);
        }
        else
        {
            printf("成员%d不在线\n", member_id);
        }
    }
    free(msg_str);
    free(members);
    free(from_name);
}

// 处理获取群历史消息请求
static void handle_get_group_history(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    // 解析群ID
    cJSON *group_id_item = cJSON_GetObjectItem(root, "group_id");
    if (!cJSON_IsNumber(group_id_item))
        return;
    int group_id = group_id_item->valueint;
    printf("handle_get_group_history:group_id=%d\n", group_id);

    // 可选参数：limit和offset
    int limit = 100, offset = 0;
    cJSON *limit_item = cJSON_GetObjectItem(root, "limit");
    if (cJSON_IsNumber(limit_item))
        limit = limit_item->valueint;
    cJSON *offset_item = cJSON_GetObjectItem(root, "offset");
    if (cJSON_IsNumber(offset_item))
        offset = offset_item->valueint;

    // 从数据库获取历史消息
    char *history_json = db_get_group_history(db_conn, group_id, current_user_id, limit, offset);
    if (!history_json)
    {
        history_json = strdup("[]"); // 失败返回空数组
    }

    printf("history_json (first 200):%.200s\n", history_json);

    cJSON *msgs = cJSON_Parse(history_json);
    free(history_json);
    if (!msgs)
        msgs = cJSON_CreateArray();

    // 构造响应
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "group_history");
    cJSON_AddItemToObject(resp, "messages", msgs);
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 处理通过群名称加入群聊请求
static void handle_join_group_by_name(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    cJSON *name_item = cJSON_GetObjectItem(root, "group_name");
    if (!cJSON_IsString(name_item) || strlen(name_item->valuestring) == 0)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "join_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "群名称不能为空");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    const char *group_name = name_item->valuestring;
    int group_id = db_find_group_by_name(db_conn, group_name);
    if (group_id <= 0)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "join_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "群不存在");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 添加成员到群，角色为普通成员
    if (db_add_group_member(db_conn, group_id, current_user_id, "member") != 0)
    {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "join_group_resp");
        cJSON_AddStringToObject(resp, "status", "fail");
        cJSON_AddStringToObject(resp, "msg", "加入失败，可能已在群中");
        char *s = cJSON_PrintUnformatted(resp);
        send_with_len(c_sockfd, s);
        free(s);
        cJSON_Delete(resp);
        return;
    }

    // 返回成功
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "join_group_resp");
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddStringToObject(resp, "msg", "加入成功");
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 处理获取私聊历史消息请求
static void handle_get_history(int current_user_id, cJSON *root, int c_sockfd, MYSQL *db_conn)
{
    if (current_user_id == -1)
        return;

    cJSON *friend_id_item = cJSON_GetObjectItem(root, "friend_id");
    if (!cJSON_IsNumber(friend_id_item))
    {
        return;
    }
    int friend_id = friend_id_item->valueint;

    int limit = 50, offset = 0;
    cJSON *limit_item = cJSON_GetObjectItem(root, "limit");
    if (cJSON_IsNumber(limit_item))
        limit = limit_item->valueint;
    cJSON *offset_item = cJSON_GetObjectItem(root, "offset");
    if (cJSON_IsNumber(offset_item))
        offset = offset_item->valueint;

    char *history_json = db_get_history(db_conn, current_user_id, friend_id, limit, offset);
    if (!history_json)
    {
        history_json = strdup("[]");
    }

    cJSON *msgs = cJSON_Parse(history_json);
    free(history_json);
    if (!msgs)
        msgs = cJSON_CreateArray();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "history");
    cJSON_AddItemToObject(resp, "messages", msgs);
    char *s = cJSON_PrintUnformatted(resp);
    send_with_len(c_sockfd, s);
    free(s);
    cJSON_Delete(resp);
}

// 4.线程执行的任务函数--提供读写服务
void *doService(void *arg)
{
    int c_sockfd = *(int *)arg;
    int current_user_id = -1;

    // 每个线程独立建立数据库连接
    MYSQL *db_conn = db_connect();
    if (!db_conn)
    {
        printf("数据库连接失败，线程退出！\n");
        close(c_sockfd);
        return NULL;
    }

    while (1)
    {
        // ①先读取4字节长度头
        uint32_t net_len;
        ssize_t n = recv(c_sockfd, &net_len, 4, MSG_WAITALL); // MSG_WAITALL确保读取完整的四个字节

        if (n == -1)
        {
            perror("接收长度错误");
            close(c_sockfd);
            return NULL;
        }

        if (n == 0)
        {
            // 客户端正常关闭，获取地址信息用于打印
            SockAddrIn clientAddr;
            socklen_t len = sizeof(clientAddr);
            getpeername(c_sockfd, (SockAddr *)&clientAddr, &len);
            uint16_t port = ntohs(clientAddr.sin_port);
            char *ip = inet_ntoa(clientAddr.sin_addr);
            printf("recv:[%s:%hu]客户端断开连接\n", ip, port);
            goto cleanup;
        }

        uint32_t msg_len = ntohl(net_len);
        if (msg_len > 65536) // 防止超大消息
        {
            printf("消息长度异常：%u\n", msg_len);
            close(c_sockfd);
            goto cleanup;
        }

        // ②.读取消息体
        char *data = (char *)malloc(msg_len + 1);
        if (!data)
        {
            perror("malloc failed");
            close(c_sockfd);
            goto cleanup;
        }
        size_t left = msg_len;
        size_t pos = 0;
        while (left > 0)
        {
            n = recv(c_sockfd, data + pos, left, 0);
            if (n <= 0)
            {
                perror("recv data failed");
                free(data);
                close(c_sockfd);
                goto cleanup;
            }
            pos += n;
            left -= n;
        }
        data[msg_len] = '\0';

        // 解析客户端请求
        // 构建cJSON对象树
        cJSON *root = cJSON_Parse(data);
        free(data);
        if (!root)
        {
            const char *err = "{\"type\":\"ERROR\",\"msg\":\"Invalid JSON\"}";
            send_with_len(c_sockfd, err);
            continue;
        }

        cJSON *type_item = cJSON_GetObjectItem(root, "type");
        if (!type_item || !cJSON_IsString(type_item))
        {
            cJSON_Delete(root);
            continue;
        }
        const char *type = type_item->valuestring;
        printf("type:%s\n", type);
        if (!strcmp(type, "login")) // 登录
        {
            puts("login");
            cJSON *username_item = cJSON_GetObjectItem(root, "username");
            cJSON *password_item = cJSON_GetObjectItem(root, "password");
            // 如果账号或者密码格式有问题或者压根没读到账号或密码
            if (!username_item || !password_item || !cJSON_IsString(username_item) || !cJSON_IsString(password_item))
            {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "LOGIN_RESP");
                cJSON_AddStringToObject(resp, "username", "fail");
                cJSON_AddStringToObject(resp, "password", "参数错误");
                char *resp_str = cJSON_PrintUnformatted(resp);
                send_with_len(c_sockfd, resp_str);
                free(resp_str);
                cJSON_Delete(resp);
            }
            else
            {
                const char *username = username_item->valuestring;
                const char *password = password_item->valuestring;

                // 读取用户列表
                cJSON *users = read_users();
                int found = 0;
                int user_index = -1;
                int size = cJSON_GetArraySize(users);
                for (int i = 0; i < size; i++)
                {
                    cJSON *user = cJSON_GetArrayItem(users, i);
                    cJSON *u = cJSON_GetObjectItem(user, "username");
                    cJSON *p = cJSON_GetObjectItem(user, "password");
                    // 如果存在这个账号
                    if (u && p && strcmp(u->valuestring, username) == 0)
                    {
                        found = 1;
                        user_index = i;
                        break;
                    }
                }

                cJSON *resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "LOGIN_RESP");
                if (!found) // 如果没有该账号
                {
                    cJSON_AddStringToObject(resp, "status", "fail");
                    cJSON_AddStringToObject(resp, "msg", "该账号不存在");
                }
                else // 如果有该账号
                {
                    cJSON *user = cJSON_GetArrayItem(users, user_index);
                    cJSON *p = cJSON_GetObjectItem(user, "password");
                    if (strcmp(p->valuestring, password) == 0) // 如果密码正确
                    {
                        cJSON *user = cJSON_GetArrayItem(users, user_index);
                        cJSON *id_item = cJSON_GetObjectItem(user, "id");
                        int user_id = id_item->valueint;
                        current_user_id = user_id;

                        // 同步用户到MYSQL users表
                        char sync_sql[512];
                        snprintf(sync_sql, sizeof(sync_sql),
                                 "INSERT INTO users (id,username,password) VALUES (%d,'%s','%s') "
                                 "ON DUPLICATE KEY UPDATE username = '%s',password = '%s'",
                                 user_id, username, password, username, password);
                        if (mysql_query(db_conn, sync_sql) != 0)
                        {
                            fprintf(stderr, "同步用户到数据库失败：%s\n", mysql_error(db_conn));
                        }
                        else
                        {
                            printf("用户%d同步到数据库成功\n", user_id);
                        }
                        // 加入在线表
                        pthread_mutex_lock(&online_lock);
                        online_fd[user_id] = c_sockfd;
                        pthread_mutex_unlock(&online_lock);
                        cJSON_AddStringToObject(resp, "status", "success");
                        cJSON_AddStringToObject(resp, "msg", "登录成功");

                        // 查询离线消息(私聊未读)
                        char *offline_json = db_get_offline_msgs(db_conn, current_user_id);
                        if (offline_json)
                        {
                            cJSON *offline_root = cJSON_Parse(offline_json);
                            free(offline_json);
                            if (offline_root)
                            {
                                cJSON *push = cJSON_CreateObject();
                                cJSON_AddStringToObject(push, "type", "offline_messages");
                                cJSON_AddItemToObject(push, "offline", offline_root);
                                char *s = cJSON_PrintUnformatted(push);
                                send_with_len(c_sockfd, s);
                                free(s);
                                cJSON_Delete(push);

                                // 标记所有离线消息为已读
                                const char *update_sql = "UPDATE messages SET is_read = 1 WHERE to_id = ? AND is_read = 0";
                                MYSQL_STMT *stmt = mysql_stmt_init(db_conn);
                                if (stmt)
                                {
                                    if (mysql_stmt_prepare(stmt, update_sql, strlen(update_sql)) == 0)
                                    {
                                        MYSQL_BIND bind;
                                        memset(&bind, 0, sizeof(bind));
                                        bind.buffer_type = MYSQL_TYPE_LONG;
                                        bind.buffer = &current_user_id;
                                        if (mysql_stmt_bind_param(stmt, &bind) == 0)
                                        {
                                            mysql_stmt_execute(stmt);
                                        }
                                    }
                                    mysql_stmt_close(stmt);
                                }
                            }
                            else
                            {
                                cJSON_Delete(offline_root);
                            }
                        }
                    }
                    else // 如果密码错误
                    {
                        cJSON_AddStringToObject(resp, "status", "fail");
                        cJSON_AddStringToObject(resp, "msg", "密码错误");
                    }
                }
                char *resp_str = cJSON_PrintUnformatted(resp);
                send_with_len(c_sockfd, resp_str);
                free(resp_str);
                cJSON_Delete(resp);
                cJSON_Delete(users);
            }
        }
        else if (!strcmp(type, "register")) // 如果是注册
        {
            puts("register");
            cJSON *username_item = cJSON_GetObjectItem(root, "username");
            cJSON *password_item = cJSON_GetObjectItem(root, "password");
            // 如果客户端发送的账号和密码格式有问题或者为空
            if (!username_item || !password_item || !cJSON_IsString(username_item) || !cJSON_IsString(password_item))
            {
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "LOGIN_RESP");
                cJSON_AddStringToObject(resp, "username", "fail");
                cJSON_AddStringToObject(resp, "password", "参数错误");
                char *resp_str = cJSON_PrintUnformatted(resp);
                send_with_len(c_sockfd, resp_str);
                free(resp_str);
                cJSON_Delete(resp);
            }
            else // 如果账号和密码格式没问题或不为空
            {
                const char *username = username_item->valuestring;
                const char *password = password_item->valuestring;

                // 读取用户列表
                cJSON *users = read_users();
                int exists = 0;
                int size = cJSON_GetArraySize(users);
                for (int i = 0; i < size; i++)
                {
                    cJSON *user = cJSON_GetArrayItem(users, i);
                    cJSON *u = cJSON_GetObjectItem(user, "username");
                    if (strcmp(u->valuestring, username) == 0)
                    {
                        exists = 1;
                        break;
                    }
                }

                cJSON *resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "REGISTER_RESP");
                if (exists) // 如果存在该用户，则注册失败
                {
                    cJSON_AddStringToObject(resp, "status", "fail");
                    cJSON_AddStringToObject(resp, "msg", "用户名已存在");
                }
                else // 若不存在该用户，则创建新用户
                {
                    int new_id = 1;
                    for (int i = 0; i < size; i++)
                    {
                        cJSON *user = cJSON_GetArrayItem(users, i);
                        cJSON *id_item = cJSON_GetObjectItem(user, "id");
                        if (id_item && id_item->valueint >= new_id)
                        {
                            new_id = id_item->valueint + 1;
                        }
                    }
                    cJSON *new_user = cJSON_CreateObject();
                    cJSON_AddNumberToObject(new_user, "id", new_id);
                    cJSON_AddStringToObject(new_user, "username", username);
                    cJSON_AddStringToObject(new_user, "password", password);
                    cJSON_AddItemToArray(users, new_user);
                    write_users(users);
                    cJSON_Delete(users);
                    cJSON_AddStringToObject(resp, "status", "success");
                    cJSON_AddStringToObject(resp, "msg", "注册成功");

                    // 同步用户到MYSQL users表
                    char insert_sql[256];
                    snprintf(insert_sql, sizeof(insert_sql),
                             "INSERT INTO users (id,username,password) VALUES (%d,'%s','%s');",
                             new_id, username, password);
                    if (mysql_query(db_conn, insert_sql) != 0)
                    {
                        fprintf(stderr, "同步用户到数据库失败：%s\n", mysql_error(db_conn));
                    }
                    else
                    {
                        printf("用户%d同步到数据库成功\n", new_id);
                    }
                }
                char *resp_str = cJSON_PrintUnformatted(resp);
                send_with_len(c_sockfd, resp_str);
                free(resp_str);
                cJSON_Delete(resp);
            }
        }
        else if (strcmp(type, "add_friend") == 0) // A发送的添加好友请求
        {
            handle_add_friend(current_user_id, root, c_sockfd);
        }
        else if (strcmp(type, "respond_friend") == 0) // B对A添加好友请求的响应
        {
            handle_respond_friend(current_user_id, root, c_sockfd);
        }
        else if (strcmp(type, "get_friends") == 0) // 客户端获取好友列表请求
        {
            handle_get_friends(current_user_id, c_sockfd);
        }
        else if (strcmp(type, "private_msg") == 0) // 好友私聊请求
        {
            handle_private_msg(current_user_id, root, c_sockfd, db_conn);
        }
        else if (strcmp(type, "create_group") == 0) // 创建群聊请求
        {
            handle_create_group(current_user_id, root, c_sockfd, db_conn);
        }
        else if (strcmp(type, "join_group_by_name") == 0) // 通过群名称加入群聊请求
        {
            handle_join_group_by_name(current_user_id, root, c_sockfd, db_conn);
        }
        else if (strcmp(type, "get_groups") == 0) // 获取用户群聊列表
        {
            handle_get_groups(current_user_id, c_sockfd, db_conn);
        }
        else if (strcmp(type, "group_msg") == 0) // 获取群聊消息
        {
            handle_group_msg(current_user_id, root, c_sockfd, db_conn);
        }
        else if (strcmp(type, "get_group_history") == 0) // 获取群历史消息
        {
            handle_get_group_history(current_user_id, root, c_sockfd, db_conn);
        }
        else if (strcmp(type, "get_history") == 0) // 获取私聊历史消息
        {
            handle_get_history(current_user_id, root, c_sockfd, db_conn);
        }
        // 开始传输文件请求、传输文件数据块请求、结束传输文件请求
        else if (strcmp(type, "file_start") == 0 || strcmp(type, "file_data") == 0 || strcmp(type, "file_end") == 0)
        {
            cJSON *to_id_item = cJSON_GetObjectItem(root, "to_id");
            if (!cJSON_IsNumber(to_id_item))
            {
                cJSON_Delete(root);
                continue;
            }
            int to_id = to_id_item->valueint;
            cJSON *is_group_item = cJSON_GetObjectItem(root, "is_group");
            int is_group = (is_group_item && cJSON_IsNumber(is_group_item) ? is_group_item->valueint : 0);

            // 添加发送者信息
            cJSON_AddNumberToObject(root, "from_id", current_user_id);
            char *from_name = get_username_by_id(current_user_id);
            if (from_name)
            {
                cJSON_AddStringToObject(root, "from_name", from_name);
                free(from_name);
            }
            
            if (is_group)
            {
                // 群聊文件传输，获取群成员并转发
                int member_count = 0;
                int *members = NULL;
                if (db_get_group_members(db_conn, to_id, &members, &member_count) != 0 || member_count == 0)
                {
                    printf("获取群成员失败或群不存在，group_id = %d\n", to_id);
                    cJSON_Delete(root);
                    continue;
                }

                char *msg_str = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                printf("群文件转发：群ID=%d，成员数=%d\n",to_id,member_count);
                for (int i = 0; i < member_count; i++)
                {
                    int member_id = members[i];
                    if (member_id == current_user_id)
                        continue;
                    pthread_mutex_lock(&online_lock);
                    int target_fd = online_fd[member_id];
                    pthread_mutex_unlock(&online_lock);
                    // 如果对方在线
                    if (target_fd != -1)
                    {
                        printf("转发给成员%d(fd=%d)\n",member_id,target_fd);
                        send_with_len(target_fd, msg_str);
                    }
                    else
                    {
                        printf("成员%d不在线，文件传输失败\n", member_id);
                    }
                }
                free(msg_str);
                free(members);
            }
            else// 私聊文件传输
            {
                char *to_name = get_username_by_id(to_id);
                if (!to_name)
                {
                    printf("目标用户不存在，ID=%d\n", to_id);
                    cJSON_Delete(root);
                    continue;
                }

                char *msg_str = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                pthread_mutex_lock(&online_lock);
                int target_fd = online_fd[to_id];
                pthread_mutex_unlock(&online_lock);
                // 如果对方在线
                if (target_fd != -1)
                {
                    printf("私聊文件：转发给用户%s(fd=%d)\n",to_name,target_fd);
                    send_with_len(target_fd, msg_str);
                }
                else
                {
                    printf("用户%s不在线，文件传输失败\n", to_name);
                }
                free(msg_str);
                free(to_name);
            }
            continue;
        }
        else // 未知请求类型
        {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "type", "ERROR");
            cJSON_AddStringToObject(resp, "msg", "UnKnown request type");
            char *resp_str = cJSON_PrintUnformatted(resp);
            send_with_len(c_sockfd, resp_str);
            free(resp_str);
            cJSON_Delete(resp);
        }
        cJSON_Delete(root);
    }
cleanup:
    // 统一清理资源
    if (current_user_id != -1)
    {
        pthread_mutex_lock(&online_lock);
        online_fd[current_user_id] = -1;
        pthread_mutex_unlock(&online_lock);
    }
    close(c_sockfd);
    if (db_conn)
    {
        db_close(db_conn);
    }
    return NULL;
}