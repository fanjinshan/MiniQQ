#ifndef __DB_H
#define __DB_H

#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

/**
 * @brief 建立与数据库连接
 * @return 成功返回MYSQL指针，失败返回NULL
 */
MYSQL *db_connect(void);

/**
 * @brief 关闭MYSQL连接
 * @param conn 要关闭的连接指针
 */
void db_close(MYSQL* conn);

/**
 * @brief 存储私聊消息
 * @return 0成功，-1失败
 */
int db_store_private_msg(MYSQL* conn,int from_id,int to_id,const char* content,time_t ts);

/**
 * @brief 查询与某好友的历史消息（私聊）
 * @param limit 返回条数
 * @param offset 偏移量
 * @return 返回JSON数组字符串，需要调用者free
 */
char *db_get_history(MYSQL* conn,int user_id,int friend_id,int limit,int offset);

/**
 * @brief 查询用户的离线消息（所有未读私聊）
 * @return 返回JSON对象字符串，按好友分组
 */
char *db_get_offline_msgs(MYSQL* conn,int user_id);

/**
 * @brief 将某好友消息标记为已读
 * @return 0成功，-1失败
 */
int db_mark_read(MYSQL* conn,int user_id,int friend_id);

/**
 * @brief清空与某好友的聊天记录
 * @return 0成功，-1失败
 */
int db_clear_history(MYSQL* conn,int user_id,int friend_id);

//群聊相关
/**
 * @brief 创建群聊
 * @param group_id 输出参数，返回新群ID
 */
int db_create_group(MYSQL* conn,const char* name,int creator_id,int* group_id);

/**
 * @brief 添加群成员
 * @param role 角色 “owner”，“admin”，“member”
 */
int db_add_group_member(MYSQL* conn,int group_id,int user_id,const char* role);

/**
 * @brief 获取用户的所有群聊
 * @return 返回JSON数组字符串[{"id":1,"name":"群名","creator_id":123},...]
 */
char* db_get_user_groups(MYSQL* conn,int user_id);

/**
 * @brief 存储群消息
 */
int db_store_group_msg(MYSQL* conn,int from_id,int group_id,const char* content,time_t ts);

/**
 * @brief 获取群历史消息
 * @param user_id 当前用户ID，判断消息是否自己发送
 * @param limit 返回条数
 * @return 返回JSON数组字符串，每条包含from_id,from_name,content,timestamp,is_self
 */
char* db_get_group_history(MYSQL* conn,int group_id,int user_id,int limit,int offset);

/**
 * @brief 获取群所有成员ID（用于转发消息）
 * @param members 输出参数，指向动态分配的整数数组，需要调用者free
 * @param count 输出参数，成员数量
 */
int db_get_group_members(MYSQL* conn,int group_id,int **members,int *count);

//根据群名称查找群ID(返回第一个匹配的群ID)
int db_find_group_by_name(MYSQL* conn,const char *name);

#endif