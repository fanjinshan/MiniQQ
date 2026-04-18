#include "page_config.h"
/*
GET请求：
1.拼接完整的 URL（包含查询参数）。
2.创建 CURL 句柄，设置 CURLOPT_URL 为拼接好的 URL。
3.可选：设置超时、SSL 选项等。
4.设置写回调函数以接收响应数据。
5.调用 curl_easy_perform 执行请求。
天气请求（GET）：
请求参数（城市、key等）直接拼在 URL 中，通过 CURLOPT_URL 设置，
不需要额外发送数据体。服务器返回的天气数据（JSON）通过写回调接收。



POST 请求：
1.设置请求 URL（通常不带查询参数）。
2.设置 CURLOPT_POSTFIELDS 为要发送的数据字符串。
3.设置请求头（如 Content-Type: application/json）。
4.同样设置写回调以接收响应。
5.调用 curl_easy_perform。
AI 请求（POST）：
需要将用户消息等数据以 JSON 格式放入请求体，因此必须使用 POST。
代码中构造了 JSON 字符串 post_data，通过 CURLOPT_POSTFIELDS 设置，
并添加了 Authorization 头进行认证。服务器返回的 AI 回复同样通过写回调接收。
*/

extern lv_obj_t *weather_cont;

/**
 * @brief 组装HTTP请求URL
 * @param host 服务器主机地址(如："http://example.com")
 * @param path 资源路径(如："/api/data")
 * @param out_url 输出参数用于存储拼接后的URL（需要调用者手动释放）
 * @return 0:成功；-1:内存分配失败；-2：输入参数无效
 */
static int assemble_url(const char *host, const char *path, char **out_url)
{
    // 总长度：host长度 + path长度 + 1
    *out_url = malloc(strlen(host) + strlen(path) + 1);
    strcpy(*out_url, host); // 复制host到缓冲区
    strcat(*out_url, path); // 追加path到缓冲区
    return 0;
}

/**
 * @brief CURL数据接收回调函数
 * @param data 本次接收的数据
 * @param size 单个数据单元大小(通常为1)
 * @param nmemb 数据单元数量
 * @param userp 指向http_resp_data_t的指针，用于存储数据
 * @return 实际处理的字节数，0表示失败
 */
size_t write_callback(void *data, size_t size, size_t nmemb, void *userp)
{
    // 计算本次回调接收到的总字节数
    size_t realsize = size * nmemb;
    // 将userp转换为http_resp_data_t指针，用于访问存储结构
    http_resp_data_t *mem = (http_resp_data_t *)userp;

    // 计算新缓冲区大小并重新分配内存
    //  为什么不用malloc？因为write_callback回调函数需要持续接收并拼接多批次的数据，
    // realloc能在保留已有数据的基础上扩容，malloc则做不到。
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr)
        return 0; // 内存分配失败，返回0通知libcurl终止传输
    // 存储新接收的数据
    // 更新指针，指向新的地址
    mem->data = ptr;
    // 将新接收的数据拷贝到缓冲区末尾
    memcpy(mem->data + mem->size, data, realsize);
    // 更新已存储数据的大小
    mem->size += realsize;
    // 在末尾添加字符串结束符，以便作为字符串处理
    mem->data[mem->size] = '\0';
    // 返回处理的字节数，libcurl会继续调用
    return realsize;
}

/**
 * @brief 通用HTTP请求函数：发送HTTP请求
 * @param host 服务器主机地址
 * @param path 资源路径
 * @param method 请求方法(GET/POST等)
 * @param request_json POST 请求时的提交数据（NULL表示无）
 * @param[out] response_json 输出参数：用于返回响应数据（需要调用者手动释放）
 */

void http_request_method(const char *host, const char *path, const char *method, const char *request_json, char **response_json)
{
    printf("Request: %s%s,Method: %s\n", host, path, method);
    // 创建一个传输会话的句柄
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        // 创建失败，返回
        return;
    }
    // 组装拼接url
    char *url = NULL;
    assemble_url(host, path, &url);
    // 设置请求的URL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // 通用配置
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);        // 调试模式：启用详细输出模式
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);       // 设置请求超过时间（单位：秒），20L表示超过20秒无响应则终止请求
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 跳过SSL证书验证(0L表示关闭)，跳过对服务器SSL证书的有效性检查
    // 初始化响应数据结构，设置响应处理
    http_resp_data_t response_data = {0};                          // data = NULL,size = 0
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // 注册响应数据接收回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);     // 指定回调函数的用户数据
    // 若是POST请求，设置POST相关选项
    if (strcmp(method, "POST") == 0)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    }
    // 执行HTTP请求，code为返回状态
    CURLcode code = curl_easy_perform(curl);
    // 处理响应
    if (code == CURLE_OK)
    {
        // 请求成功，将响应数据的内存所有权转移给调用者response_json
        *response_json = response_data.data;
    }
    else // 请求失败
    {
        printf("Request failed:%s(%d)\n", curl_easy_strerror(code), code);
        free(response_data.data); // 失败时释放内存
    }
    // 资源清理
    free(url);               // 释放拼接的URL字符串
    curl_easy_cleanup(curl); // 清理libcurl会话
}

/**
 * 在主线程中更新天气容器内容(由lv_async_call调用)
 * @param arg 指向weather_result_t的指针
 */
void update_weather_ui(void *arg)
{
    weather_result_t *res = arg;
    lv_obj_t *container = res->weather_cont; // 获取天气容器对象
    // 如果容器已被用户关闭(为NULL)，则释放资源返回
    if (!container || !lv_obj_is_valid(container))
    {
        // 容器可能已被关闭
        free(res->city);
        free(res->weather);
        free(res);
        return;
    }

    // 清空容器内所有子控件
    lv_obj_clean(container);
    //净化天气文本，去除可能的换行，回车等
    char clean_weather[32];
    strncpy(clean_weather,res->weather,sizeof(clean_weather) - 1);
    clean_weather[sizeof(clean_weather) - 1] = '\0';
    for(int i = 0;clean_weather[i];i++)
    {
        if(clean_weather[i] == '\n' || clean_weather[i] == '\r')
        {
            clean_weather[i] = ' ';
        }
    }

    // 根据天气情况选择图片
    const char *bg_path;
    if (strcmp(clean_weather, "晴") == 0)
    {
        bg_path = GET_IMAGE_PATH("weather_sunny.png");
    }
    else if (strcmp(clean_weather, "多云") == 0 || strcmp(clean_weather, "阴") == 0)
    {
        bg_path = GET_IMAGE_PATH("weather_cloudy.png");
    }
    else if (strcmp(clean_weather, "雨") == 0)
    {
        bg_path = GET_IMAGE_PATH("weather_rainy.png");
    }
    else
    {
        bg_path = GET_IMAGE_PATH("weather_sunny.png");
    }
    // 添加背景图
    lv_obj_t *img_weather = lv_img_create(container);
    lv_obj_center(img_weather);
    lv_img_set_src(img_weather, bg_path);
    lv_obj_clear_state(img_weather, LV_STATE_FOCUS_KEY);

    // 显示城市和天气信息
    lv_obj_t *label_city = lv_label_create(img_weather);
    obj_font_set(label_city, FONT_TYPE_CN, 16);
    lv_obj_align(label_city, LV_ALIGN_TOP_LEFT, 80, 30);
    lv_label_set_text(label_city, res->city);
    lv_obj_t *label_temp = lv_label_create(img_weather);
    obj_font_set(label_temp, FONT_TYPE_CN, 60);
    lv_obj_align(label_temp, LV_ALIGN_LEFT_MID, 55, 45);
    lv_label_set_text_fmt(label_temp, "%d", res->temp);
    lv_obj_t *label_o = lv_label_create(img_weather);
    obj_font_set(label_o, FONT_TYPE_CN, 20);
    lv_obj_align_to(label_o, label_temp, LV_ALIGN_OUT_RIGHT_TOP, 10, 10);
    lv_label_set_text(label_o, "o");
    lv_obj_t *label_weather = lv_label_create(img_weather);
    obj_font_set(label_weather, FONT_TYPE_CN, 20);
    lv_obj_align_to(label_weather, label_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 10);
    lv_label_set_text(label_weather, clean_weather);

    // 坐标容器
    lv_obj_t *pos_cont = lv_obj_create(container);
    lv_obj_add_style(pos_cont, &com_style, 0);
    lv_obj_set_size(pos_cont, 30, 30);
    lv_obj_set_style_clip_corner(pos_cont, true, 0);
    lv_obj_align_to(pos_cont, label_city, LV_ALIGN_OUT_LEFT_MID, 0, 2);
    lv_obj_set_style_bg_opa(pos_cont, 0, 0);
    lv_obj_clear_flag(pos_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *img_pos = lv_img_create(pos_cont);
    lv_img_set_src(img_pos, GET_IMAGE_PATH("pos.png"));

    lv_img_set_zoom(img_pos, 30);
    lv_obj_center(img_pos);

    free(res->city);
    free(res->weather);
    free(res);
}

/**
 * 测试http get请求：在子线程中调用HTTP接口获取天气，解析JSON，通过lv_async_call更新UI
 * @param arg 城市名指针（需要释放）
 * @return NULL
 */
void *weather_thread_func(void *arg)
{
    // 将参数转换为城市名字字符串
    char *city = (char *)arg;
    // 用户KEY
    char weather_key[] = "Sd3KKAWWRziyjhm1h";
    // 构建请求路径，包含key、城市、语言、单位
    char get_path[256];
    snprintf(get_path, sizeof(get_path), "/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c", weather_key, city);
    // 发送GTTP GET请求,响应存储在response中
    char *response = NULL;
    http_request_method("https://api.seniverse.com", get_path, "GET", "", &response);
    // 定义变量存储解析结果
    char *city_name = NULL;
    char *weather_text = NULL;
    int temperature = 0;

    if (response)
    {
        printf("%s", response);
        cJSON *root = cJSON_Parse(response);
        if (root)
        {
            // 获取results数组
            cJSON *results = cJSON_GetObjectItem(root, "results");
            if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0)
            {
                cJSON *first = cJSON_GetArrayItem(results, 0);
                cJSON *location = cJSON_GetObjectItem(first, "location"); // 位置对象
                cJSON *now = cJSON_GetObjectItem(first, "now");           // 当前天气对象
                if (location && now)
                {
                    // 提取城市名、天气描述、温度
                    cJSON *name = cJSON_GetObjectItem(location, "name");
                    cJSON *text = cJSON_GetObjectItem(now, "text");
                    cJSON *temp = cJSON_GetObjectItem(now, "temperature");
                    // 若是字符串，就复制一份(因为cJSON内存将在后面释放)
                    if (cJSON_IsString(name))
                        city_name = strdup(name->valuestring);
                    if (cJSON_IsString(text))
                        weather_text = strdup(text->valuestring);
                    if (cJSON_IsString(temp))
                        temperature = atoi(temp->valuestring);
                }
            }
            cJSON_Delete(root); // 释放JSON对象树
        }
        free(response); // 释放原始响应字符串
    }
    else
    {
        printf("获取天气失败!\n");
    }
    // 解析失败，设置默认值
    if (!city_name)
        city_name = strdup("未知");
    if (!weather_text)
        weather_text = strdup("未知");

    // 更新天气缓存
    weather_cache_update(city_name, weather_text, temperature);

    //保存当前容器的快照，避免全局变量被修改
    lv_obj_t* current_cont = weather_cont;
    // 更新UI数据
    // 创建传递给UI线程的数据结构
    //如果天气容器存在，再刷新UI显示(没有点击获取天气按钮就不会显示UI)
    if (current_cont)
    {
        weather_result_t *res = (weather_result_t *)malloc(sizeof(weather_result_t));
        res->weather_cont = weather_cont; // 指向天气容器的指针
        res->city = city_name ? city_name : strdup("未知");
        res->weather = weather_text ? weather_text : strdup("未知");
        res->temp = temperature;

        lv_async_call(update_weather_ui, res); // 切换到主线程执行UI更新函数
    }
    else
    {
        //容器不存在，就不更新UI，释放内存
        free(city_name);
        free(weather_text);
    }

    //重置fetching标志
    pthread_mutex_lock(&weather_fetch_lock);
    weather_fetching = 0;
    pthread_mutex_unlock(&weather_fetch_lock);

    // 释放城市名字符串
    free(city);
    return NULL;
}

/**
 * 在主线程中显示AI回复
 * @param arg 指向ai_result_t的指针
 */
static void display_ai_reply(void *arg)
{
    ai_result_t *res = arg;
    // 检查当前是否仍在和AI聊天
    if (res->msg_area && current_chat_friend_id == 0)
    {
        add_message(res->msg_area, "AI助手", res->reply, false, time_buf);
    }
    free(res->reply);
    free(res);
}

/*
 curl https://api.deepseek.com/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer ${DEEPSEEK_API_KEY}" \
  -d '{
        "model": "deepseek-chat",
        "messages": [
          {"role": "system", "content": "You are a helpful assistant."},
          {"role": "user", "content": "Hello!"}
        ],
        "stream": false
      }'
 */

/**
 * AI线程
 * @param arg 用户输入的消息字符串
 */
void *ai_thread_func(void *arg)
{
    char *user_msg = (char *)arg; // 用户输入的消息
    char *reply = NULL;           // 存储AI回复
    // 初始化libcurl会话
    CURL *curl = curl_easy_init();
    if (curl)
    {
        // API密钥
        const char *api_key = "sk-56ce6e81c43f44fc8538f52ffa5b8c02"; // API KEY
        // API请求地址
        // POST端点
        const char *url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"; // API 地址

        // 构造请求JSON
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "model", "deepseek-v3.2"); // 指定模型
        cJSON *messages = cJSON_AddArrayToObject(root, "messages");
        // 构造用户消息对象
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "user");      // 角色为用户
        cJSON_AddStringToObject(msg, "content", user_msg); // 内容为用户输入
        cJSON_AddItemToArray(messages, msg);               // 将消息加入数组
        // 管理深度思考
        cJSON_AddBoolToObject(root, "enable_thinking", true);
        char *post_data = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        // 设置HTTP请求头
        struct curl_slist *headers = NULL;
        // 告知AI服务器发送数据类型为JSON
        headers = curl_slist_append(headers, "Content-Type: application/json"); // 内容类型为JSON

        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key); // 认证头（POST请求中携带API密钥的标准方式）
        headers = curl_slist_append(headers, auth_header);

        // 配置CURL选项
        curl_easy_setopt(curl, CURLOPT_URL, url);              // 设置URL
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);   // 设置请求头
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data); // 将post_data设置为请求体，设置POST数据，自动将请求方法设为POST
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);          // 超时20秒
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);    // 跳过SSL验证

        // 设置接收回调
        // 准备接收响应数据
        http_resp_data_t response_data = {0};
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // 注册写回调
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);     // 传递数据结构

        // ！！！执行请求
        CURLcode code = curl_easy_perform(curl);
        long http_code = 0;
        if (code == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }
        // 清理请求资源
        curl_slist_free_all(headers); // 释放请求头链表
        curl_easy_cleanup(curl);      // 清理CURL会话
        free(post_data);              // 释放POST数据字符串

        // 如果请求成功且收到响应数据
        if (code == CURLE_OK && http_code == 200 && response_data.data)
        {
            cJSON *resp_root = cJSON_Parse(response_data.data);
            if (resp_root)
            {
                // 获取choices数组
                cJSON *choices = cJSON_GetObjectItem(resp_root, "choices");
                if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0)
                {
                    // 取第一个choice
                    cJSON *first = cJSON_GetArrayItem(choices, 0);
                    // 获取message对象
                    cJSON *message = cJSON_GetObjectItem(first, "message");
                    // 获取content字段
                    cJSON *content = cJSON_GetObjectItem(message, "content");
                    if (cJSON_IsString(content))
                    {
                        // 复制回复内容（后续会释放resp_root）
                        reply = strdup(content->valuestring);
                    }
                }
                cJSON_Delete(resp_root);
            }
            free(response_data.data);
        }
        else
        {
            char err_buf[512];
            if (code != CURLE_OK)
            {
                snprintf(err_buf, sizeof(err_buf), "请求失败：%s", curl_easy_strerror(code));
            }
            else if (http_code != 200)
            {
                snprintf(err_buf, sizeof(err_buf), "服务器返回错误码：%ld", http_code);
                // 解析服务器返回的错误信息
                if (response_data.data)
                {
                    cJSON *err_root = cJSON_Parse(response_data.data);
                    if (err_root)
                    {
                        cJSON *error = cJSON_GetObjectItem(err_root, "root");
                        if (error)
                        {
                            cJSON *message = cJSON_GetObjectItem(error, "message");
                            if (cJSON_IsString(message))
                            {
                                snprintf(err_buf, sizeof(err_buf), "服务器错误：%s", message->valuestring);
                            }
                        }
                        cJSON_Delete(err_root);
                    }
                    free(response_data.data);
                }
            }
            else
            {
                strcpy(err_buf, "未知错误");
            }
            reply = strdup(err_buf);
        }
    }

    if (!reply)
    {
        reply = strdup("AI服务暂时不可用，请稍后再试。");
    }
    // 传递给UI线程
    ai_result_t *res = malloc(sizeof(ai_result_t));
    res->msg_area = current_msg_area; // 保存当前消息区域指针，确定显示位置
    res->reply = reply;               // AI回复的内容
    lv_async_call(display_ai_reply, res);

    free(user_msg);
    return NULL;
}