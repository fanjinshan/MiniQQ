#include "db.h"

//数据库参数
#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS "123"
#define DB_NAME "chat_app"
#define DB_PORT 3306

//建立数据库连接
MYSQL *db_connect(void)
{
    MYSQL* conn = mysql_init(NULL);
    if(conn == NULL)
    {
        //输出信息到标准错误流
        fprintf(stderr,"mysql_init failed\n");
        return NULL;
    }

    //设置连接超时
    unsigned int timeout = 5;
    mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);

    //实际连接
    if(mysql_real_connect(conn,DB_HOST,DB_USER,DB_PASS,DB_NAME,DB_PORT,NULL,0) == NULL)
    {
        fprintf(stderr,"mysql_real_connect failed:%s\n",mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    //设置字符集为utf8mb4
    if(mysql_set_character_set(conn,"utf8mb4") != 0)
    {
        fprintf(stderr,"mysql_set_character_set failed%s\n",mysql_error(conn));
    }
    return conn;
}

//关闭数据库连接
void db_close(MYSQL* conn)
{
    if(conn)
    {
        mysql_close(conn);
    }
}

//存储私聊消息
int db_store_private_msg(MYSQL* conn,int from_id,int to_id,const char* content,time_t ts)
{
    //将time_t时间戳转换为本地时间的结构体tm
    struct tm tm_info;
    localtime_r(&ts,&tm_info);//线程安全的本地时间转换函数
    char time_str[20];
    strftime(time_str,sizeof(time_str),"%Y-%m-%d %H:%M:%S",&tm_info);

    //sql语句模板，使用占位符？代替实际参数，防止SQL注入，且is_group和is_read固定为0
    const char* sql = "INSERT INTO messages(from_id,to_id,content,timestamp,is_group,is_read) VALUES (?,?,?,?,0,0)";
    //预处理语句四个阶段：准备、绑定、执行、关闭
    //初始化一个预处理语句句柄，关联到已连接的数据库conn
    //预处理之后，sql语法就固定了，后续传入的参数就只是参数，而不会被当作是sql语句的一部分，防止了sql注入，避免篡改数据等
    MYSQL_STMT *stmt = mysql_stmt_init(conn);//预处理语句
    if(!stmt)
    {
        fprintf(stderr,"mysql_stmt_init failed\n");
        return -1;
    }

    //准备sql语句，将sql模板发送到服务器进行解析、编译，等待绑定参数
    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"mysql_stmt_prepare failed :%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    //定义参数绑定数组，共四个，对应sql中的四个？
    MYSQL_BIND bind[4];
    memset(bind,0,sizeof(bind));

    //绑定第一个参数：from_id(整形)
    bind[0].buffer_type = MYSQL_TYPE_LONG;//指定C变量的数据类型为MYSQL的长整形（对应int）
    bind[0].buffer = &from_id;//指向实际数据的指针
    bind[0].is_unsigned = 0;//声明该值是否为无符号数，0表示有符号（int默认有符号）

    //绑定第二个参数:to_id(整形)
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &to_id;
    bind[1].is_unsigned = 0;

    bind[2].buffer_type = MYSQL_TYPE_STRING;//字符串类型
    bind[2].buffer = (char*)content;//content是const char*,需强制转换为char*
    bind[2].buffer_length = strlen(content);//设置缓冲区的实际长度（字节数）

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = time_str;//指向时间字符串的指针
    bind[3].buffer_length = strlen(time_str);//设置字符串长度

    //将绑定数组关联到预处理语句，通知MYSQL参数的实际数据位置
    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"mysql_stmt_bind_param failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    //执行预处理语句，将绑定的数据插入数据库
    if(mysql_stmt_execute(stmt) != 0)
    {
        fprintf(stderr,"mysql_stmt_execute failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    //关闭预处理语句，释放资源
    mysql_stmt_close(stmt);
    return 0;
}

//查询私聊历史记录
//返回JSON字符串数组
char *db_get_history(MYSQL* conn,int user_id,int friend_id,int limit,int offset)
{
    const char* sql = "SELECT from_id,content,timestamp,(from_id = ?) as is_self FROM messages "
                      "WHERE (from_id = ? AND to_id = ?) OR (from_id = ? AND to_id = ?) "
                      "ORDER BY timestamp ASC LIMIT ? OFFSET ?";//按时间倒序，分页
    MYSQL_STMT * stmt = mysql_stmt_init(conn);
    if(!stmt) return NULL;
    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)//准备SQL
    {
        fprintf(stderr,"prepare failed :%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return NULL;
    }

    int params[7];//定义参数数组
    params[0] = user_id;
    params[1] = user_id;
    params[2] = friend_id;
    params[3] = friend_id;
    params[4] = user_id;
    params[5] = limit;
    params[6] = offset;

    MYSQL_BIND bind[7];//定义绑定数据
    memset(bind,0,sizeof(bind));
    for(int i = 0;i < 7;i++)
    {
        bind[i].buffer_type = MYSQL_TYPE_LONG;
        bind[i].buffer = &params[i];//缓冲区指向对应的数组元素
    }

    if(mysql_stmt_bind_param(stmt,bind) != 0)//绑定参数
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return NULL;
    }

    if(mysql_stmt_execute(stmt) != 0)//执行查询
    {
        fprintf(stderr,"execute failed :%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return NULL;
    }

    MYSQL_BIND result[4];//定义结果集绑定数组，4列
    memset(result,0,sizeof(result));
    int from_id;
    char content[8192];
    unsigned long content_len;  //实际消息长度
    char timestamp[20]; //时间戳缓冲区
    unsigned long ts_len;//实际时间字符串长度
    int is_self;    //是否自己发送

    result[0].buffer_type = MYSQL_TYPE_LONG;    //第一列：from_id
    result[0].buffer = &from_id;    //绑定到from_id

    result[1].buffer_type = MYSQL_TYPE_STRING;  //第二列：content
    result[1].buffer = content; //缓冲区指向content数据
    result[1].buffer_length = sizeof(content);//缓冲区大小
    result[1].length = &content_len;//保存实际长度

    result[2].buffer_type = MYSQL_TYPE_STRING;  //第三列：timestamp
    result[2].buffer = timestamp; //缓冲区指向content数据
    result[2].buffer_length = sizeof(timestamp);//缓冲区大小
    result[2].length = &ts_len;//保存实际长度

    result[3].buffer_type = MYSQL_TYPE_LONG;    //第四列：if_self
    result[3].buffer = &is_self;    //绑定到is_self变量

    if(mysql_stmt_bind_result(stmt,result) != 0)//绑定结果集缓冲区
    {
        fprintf(stderr,"bind_result failed :%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return NULL;
    }

    cJSON* msgs = cJSON_CreateArray();  //创建cJSON数组对象，用于存放消息列表
    while(mysql_stmt_fetch(stmt) == 0)  //循环获取每一行的结果，返回0表示成功
    {
        content[content_len] = '\0';
        timestamp[ts_len] = '\0';
        cJSON* msg = cJSON_CreateObject();
        cJSON_AddNumberToObject(msg,"from_id",from_id);
        cJSON_AddStringToObject(msg,"content",content);
        cJSON_AddStringToObject(msg,"timestamp",timestamp);
        cJSON_AddBoolToObject(msg,"is_self",is_self?true:false);//添加is_self字段
        cJSON_AddItemToArray(msgs,msg);
    }

    mysql_stmt_close(stmt); //关闭语句

    //反转数组为升序
    int size = cJSON_GetArraySize(msgs);
    cJSON* reversed = cJSON_CreateArray();  
    for(int i = size - 1;i >= 0;i--)
    {
        cJSON *item = cJSON_DetachItemFromArray(msgs,i);//从原数组中分离出元素(不释放内存)
        cJSON_AddItemToArray(reversed,item);
    }
    cJSON_Delete(msgs);//删除原数组，释放内存

    char* result_str = cJSON_PrintUnformatted(reversed);
    cJSON_Delete(reversed);//删除反转数组
    return result_str;//返回JSON字符串，需要调用者free
}

//查询离线消息
char *db_get_offline_msgs(MYSQL* conn,int user_id)
{
    const char* sql = "SELECT from_id ,content, timestamp FROM messages "
                      "WHERE to_id = ? AND is_read = 0 AND is_group = 0 "//条件：接收者是user_id,未读，且不是群消息
                      "ORDER BY from_id, timestamp";//按发送者id和时间排序
    MYSQL_STMT *stmt = mysql_stmt_init(conn);//初始化预处理语句
    if(!stmt) return NULL;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }

    MYSQL_BIND bind;//定义一个参数绑定
    memset(&bind,0,sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &user_id;
    if(mysql_stmt_bind_param(stmt,&bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }

    int from_id;
    char content[4096];
    unsigned long content_len;
    char timestamp[20];
    unsigned long ts_len;

    MYSQL_BIND result[3];//定义结果集绑定数组，3列
    memset(result,0,sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONG;//第一列，from_id
    result[0].buffer = &from_id;
    result[1].buffer_type = MYSQL_TYPE_STRING;//第二列，content
    result[1].buffer = content;
    result[1].buffer_length = sizeof(content);
    result[1].length = &content_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;//第三列，timestamp
    result[2].buffer = timestamp;
    result[2].buffer_length = sizeof(timestamp);
    result[2].length = &ts_len;

    if(mysql_stmt_bind_result(stmt,result) != 0)//绑定结果集
    {
        fprintf(stderr,"bind_result failed");
        mysql_stmt_close(stmt);
        return NULL;
    }

    cJSON* offline_list = cJSON_CreateArray();//创建JSON数组，用于存放按好友分组的数据
    int current_friend = -1;//当前正在处理的好友id，初始为-1
    cJSON* current_friend_obj = NULL;   //当前好友的JSON对象
    cJSON* current_msgs = NULL;//当前好友的消息数组

    while(mysql_stmt_fetch(stmt) == 0)//循环获取每一行
    {
        content[content_len] = '\0';
        timestamp[ts_len] = '\0';

        if(from_id != current_friend)
        {
            current_friend = from_id;//更新当前好友id
            current_friend_obj = cJSON_CreateObject();//创建新的好友对象
            cJSON_AddNumberToObject(current_friend_obj,"friend_id",from_id);
            current_msgs = cJSON_CreateArray();
            cJSON_AddItemToObject(current_friend_obj,"messages",current_msgs);//将消息数组添加到好友对象
            cJSON_AddItemToArray(offline_list,current_friend_obj);//将好友对象添加到离线列表
        }
        cJSON* msg = cJSON_CreateObject();//创建消息对象
        cJSON_AddStringToObject(msg,"content",content);
        cJSON_AddStringToObject(msg,"timestamp",timestamp);
        cJSON_AddItemToArray(current_msgs,msg);
    }

    mysql_stmt_close(stmt);//关闭语句
    char* result_str = cJSON_PrintUnformatted(offline_list);
    cJSON_Delete(offline_list);
    return result_str;//返回字符串，调用者需free
}

//标记私聊消息为已读
int db_mark_read(MYSQL* conn,int user_id,int friend_id)
{
    const char*sql = "UPDATE messages SET is_read = 1 WHERE to_id = ? AND from_id = ? AND is_read = 0";
    MYSQL_STMT * stmt = mysql_stmt_init(conn);//初始化预处理语句
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)//准备SQL
    {
        fprintf(stderr,"prepare failed");
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[2];
    memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &friend_id;
    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    mysql_stmt_close(stmt);
    return 0;
}

//清空私聊聊天记录
int db_clear_history(MYSQL* conn,int user_id,int friend_id)
{
    const char* sql = "DELETE FROM messages WHERE (from_id = ? AND to_id = ?) OR (from_id = ? AND to_id = ?)";//删除两人之间的所有消息
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed");
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[4];
    memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &friend_id;
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &friend_id;
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = &user_id;

    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    mysql_stmt_close(stmt);
    return 0;
}

//创建群聊
int db_create_group(MYSQL* conn,const char* name,int creator_id,int* group_id)//group_id用于返回新群ID
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now,&tm_info);
    char time_str[20];
    strftime(time_str,sizeof(time_str),"%Y-%m-%d %H:%M:%S",&tm_info);//格式化时间

    const char* sql = "INSERT INTO `groups` (name, creator_id,create_time) VALUES (?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed");
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[3];
    memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)name;
    bind[0].buffer_length = strlen(name);
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &creator_id;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = time_str;
    bind[2].buffer_length = strlen(time_str);

    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    *group_id = mysql_insert_id(conn);//获取自增id，存入group_id指向的变量
    mysql_stmt_close(stmt);

    //将创建者加入群成员，角色为owner
    db_add_group_member(conn,*group_id,creator_id,"owner");//调用添加成员函数，将创建者设为群主
    return 0;
}

//添加群成员
int db_add_group_member(MYSQL* conn,int group_id,int user_id,const char* role)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now,&tm_info);//本地时间
    char time_str[20];
    strftime(time_str,sizeof(time_str),"%Y-%m-%d %H:%M:%S",&tm_info);//格式化

    const char*sql = "INSERT INTO group_members(group_id,user_id,join_time,role) VALUES (?,?,?,?)";
    MYSQL_STMT * stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed\n");
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[4];
    memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &group_id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &user_id;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = time_str;
    bind[2].buffer_length = strlen(time_str);
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)role;
    bind[3].buffer_length = strlen(role);

    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    mysql_stmt_close(stmt);
    return 0;
}

//获取用户的所有群聊
char* db_get_user_groups(MYSQL* conn,int user_id)
{
    const char* sql = "SELECT g.id,g.name,g.creator_id FROM `groups` g "//查询用户加入的群
                      "JOIN group_members gm ON g.id = gm.group_id "//关联群成员表，显示内连接(显示两张表的交集部分)
                      "WHERE gm.user_id = ?";//条件：成员表中用户ID是否等于给定值
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if(!stmt) return NULL;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed\n");
        mysql_stmt_close(stmt);
        return NULL;
    }

    MYSQL_BIND bind;
    memset(&bind,0,sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &user_id;
    if(mysql_stmt_bind_param(stmt,&bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }

    MYSQL_BIND result[3];//绑定结果集，三列
    int id;//群id
    char name[100];//群名称
    unsigned long name_len;//实际名称长度
    int creator_id;//创建者id
    memset(result,0,sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name;
    result[1].buffer_length = sizeof(name);
    result[1].length = &name_len;
    result[2].buffer_type = MYSQL_TYPE_LONG;
    result[2].buffer = &creator_id;

    if(mysql_stmt_bind_result(stmt,result) != 0)
    {
        fprintf(stderr,"bind_result failed\n");
        mysql_stmt_close(stmt);
        return NULL;
    }
    mysql_stmt_store_result(stmt);//缓存结果集到客户端

    cJSON* groups = cJSON_CreateArray();
    while(mysql_stmt_fetch(stmt) == 0)//循环获取每一行
    {
        name[name_len] = '\0';
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item,"id",id);
        cJSON_AddStringToObject(item,"name",name);
        cJSON_AddNumberToObject(item,"creator_id",creator_id);
        cJSON_AddItemToArray(groups,item);
    }
    mysql_stmt_close(stmt);

    char* result_str = cJSON_PrintUnformatted(groups);
    cJSON_Delete(groups);
    return result_str;
}

//存储群消息
int db_store_group_msg(MYSQL* conn,int from_id,int group_id,const char* content,time_t ts)
{
    struct tm tm_info;
    localtime_r(&ts,&tm_info);
    char time_str[20];
    strftime(time_str,sizeof(time_str),"%Y-%m-%d %H:%M:%S",&tm_info);//格式化

    const char* sql = "INSERT INTO messages (from_id,to_id,content,timestamp,is_group,group_id,is_read)"
                      "VALUES (?,?,?,?,1,?,0)";
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[5];
    memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &from_id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &group_id;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)content;
    bind[2].buffer_length = strlen(content);
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = time_str;
    bind[3].buffer_length = strlen(time_str);
    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer = &group_id;

    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"db_store_group_msg execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    mysql_stmt_close(stmt);
    return 0;
}

//获取群历史消息
char* db_get_group_history(MYSQL* conn,int group_id,int user_id,int limit,int offset)
{
    const char* sql = "SELECT m.from_id,u.username,m.content,m.timestamp "//查询群消息，关联用户表获取用户名
                      "FROM messages m JOIN users u ON m.from_id = u.id "//连接users表
                      "WHERE m.group_id = ? AND m.is_group = 1 "//指定群ID且是群消息
                      "ORDER BY m.timestamp ASC LIMIT ? OFFSET ?";//按时间倒序，分页
    printf("db_get_group_history: sql = %s, params: group_id=%d, limit=%d, offset=%d\n", sql, group_id, limit, offset);

    MYSQL_STMT * stmt = mysql_stmt_init(conn);
    if(!stmt) return NULL;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed\n");
        mysql_stmt_close(stmt);
        return NULL;
    }

    int params[3] = {group_id,limit,offset};
    MYSQL_BIND bind[3];
    memset(bind,0,sizeof(bind));
    for(int i =0;i<3;i++)
    {
        bind[i].buffer_type = MYSQL_TYPE_LONG;
        bind[i].buffer = &params[i];
    }

    if(mysql_stmt_bind_param(stmt,bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return NULL;
    }

    MYSQL_BIND result[4];
    int from_id;
    char from_name[32];
    unsigned long name_len;
    char content[8192];
    unsigned long content_len;
    char timestamp[20];
    unsigned long ts_len;
    memset(result,0,sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &from_id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = from_name;
    result[1].buffer_length = sizeof(from_name);
    result[1].length = &name_len;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = content;
    result[2].buffer_length = sizeof(content);
    result[2].length = &content_len;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = timestamp;
    result[3].buffer_length = sizeof(timestamp);
    result[3].length = &ts_len;

    if(mysql_stmt_bind_result(stmt,result) != 0)
    {
        fprintf(stderr,"bind_result failed\n");
        mysql_stmt_close(stmt);
        return NULL;
    }
    // if(mysql_stmt_store_result(stmt) != 0)//缓存结果集
    // {
    //     fprintf(stderr,"store_result failed:%s\n",mysql_stmt_error(stmt));
    //     mysql_stmt_close(stmt);
    //     return NULL;
    // }

    my_ulonglong row_count = mysql_stmt_num_rows(stmt);
    printf("db_get_group_history:row_count = %llu\n",row_count);

    cJSON* msgs = cJSON_CreateArray();
    int fetch_ret = 0;
    while((fetch_ret = mysql_stmt_fetch(stmt)) == 0)//循环获取每一行
    {
        from_name[name_len] = '\0';
        content[content_len] = '\0';
        timestamp[ts_len] = '\0';

        //printf("db_get_group_history:fetched :from_id = %d,from_name = %s,content = %s,timestamp = %s\n",from_id,from_name,content,timestamp);

        cJSON* msg = cJSON_CreateObject();
        cJSON_AddNumberToObject(msg,"from_id",from_id);
        cJSON_AddStringToObject(msg,"from_name",from_name);
        cJSON_AddStringToObject(msg,"content",content);
        cJSON_AddStringToObject(msg,"timestamp",timestamp);
        cJSON_AddBoolToObject(msg,"is_self",from_id == user_id ? true : false);
        cJSON_AddItemToArray(msgs,msg);
    }
    printf("db_get_group_history:actual fetched rows = %d\n",row_count);
    if(fetch_ret != 0 && fetch_ret != MYSQL_NO_DATA)
    {
        fprintf(stderr,"fetch error:%d,%s\n",fetch_ret,mysql_stmt_error(stmt));
    }
    mysql_stmt_close(stmt);

    //打印消息数量
    int size = cJSON_GetArraySize(msgs);
    printf("db_get_group_hostory:msgs array size = %d\n",size);
    //反转数组为升序
    cJSON* reversed = cJSON_CreateArray();
    for(int i = size - 1;i >= 0;i--)
    {
        cJSON* item = cJSON_DetachItemFromArray(msgs,i);
        cJSON_AddItemToArray(reversed,item);   //添加到新数组
    }
    cJSON_Delete(msgs);

    char* result_str = cJSON_PrintUnformatted(reversed);
    cJSON_Delete(reversed);
    return result_str;
}

//获取群所有成员ID
int db_get_group_members(MYSQL* conn,int group_id,int **members,int *count)
{
    const char*sql = "SELECT user_id FROM group_members WHERE group_id = ?";
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"prepare failed\n");
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind;
    memset(&bind,0,sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &group_id;
    if(mysql_stmt_bind_param(stmt,&bind) != 0)
    {
        fprintf(stderr,"bind_param failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }
    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    //先获取行数
    mysql_stmt_store_result(stmt);
    int rows = mysql_stmt_num_rows(stmt);//获取结果集行数
    if(rows<= 0)//如果没有成员
    {
        *members = NULL;//设置输出指针为NULL
        *count = 0;//设置计数为0
        mysql_stmt_close(stmt);//关闭语句
        return 0;
    }

    int *result_array = (int*)malloc(sizeof(int) * rows);//存放成员id
    if(!result_array)
    {
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND result;//结果集绑定(单列)
    int user_id;//接收用户ID
    memset(&result,0,sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &user_id;
    if(mysql_stmt_bind_result(stmt,&result) != 0)//绑定结果集
    {
        free(result_array);
        mysql_stmt_close(stmt);
        return -1;
    }

    int idx = 0;
    while(mysql_stmt_fetch(stmt) == 0)//循环获取每一行，每次调用fetch，当前行的user_id会自动存入user_id中
    {
        result_array[idx++] = user_id;//将用户ID存入数组
    }

    *members = result_array;//输出数组指针
    *count = rows;//输出成员数量
    mysql_stmt_close(stmt);
    return 0;
}

//根据群名称查找群ID(返回第一个匹配的群ID)
int db_find_group_by_name(MYSQL* conn,const char *name)
{
    const char* sql = "SELECT id FROM `groups` WHERE name = ? LIMIT 1";
    MYSQL_STMT * stmt = mysql_stmt_init(conn);
    if(!stmt) return -1;

    if(mysql_stmt_prepare(stmt,sql,strlen(sql)) != 0)
    {
        fprintf(stderr,"db_find_group_by_name prepare failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    MYSQL_BIND bind;
    memset(&bind,0,sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = (char*)name;
    bind.buffer_length = strlen(name);

    if(mysql_stmt_bind_param(stmt,&bind) != 0)
    {
        fprintf(stderr,"db_find_group_by_name bind_param failed:%s\n",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    if(mysql_stmt_execute(stmt) != 0)//运行SQL
    {
        fprintf(stderr,"db_find_group_by_name execute failed:%s\n",mysql_stmt_error(stmt));//打印错误
        mysql_stmt_close(stmt);
        return -1;
    }

    int group_id = -1;
    MYSQL_BIND result;
    memset(&result,0,sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &group_id;

    if(mysql_stmt_bind_result(stmt,&result) != 0)
    {
        fprintf(stderr,"db_find_group_by_name bind_result failed\n");
        mysql_stmt_close(stmt);
        return NULL;
    }

    mysql_stmt_store_result(stmt);
    int fetch_ret = mysql_stmt_fetch(stmt);
    if(fetch_ret == 0)
    {
        //成功获取一行
        mysql_stmt_close(stmt);
        printf("handle_join_group_by_name: group_name='%s', group_id=%d\n", name, group_id);
        return group_id;
    }
    else
    {
        //无结果或错误
        if(fetch_ret != MYSQL_NO_DATA)
        {
            fprintf(stderr,"db_find_group_by_name fetch error:%d\n",fetch_ret);
        }
        mysql_stmt_close(stmt);
        return -1;
    }
}