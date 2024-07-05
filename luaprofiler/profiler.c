//
// Created by babybus on 2024/2/5.
//

#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#if LUA_VERSION_NUM == 501

static void
luaL_checkversion(lua_State *L) {
    if (lua_pushthread(L) == 0) {
        luaL_error(L, "Must require in main thread");
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "mainthread");
}


void lua_rawsetp(lua_State *L, int idx, const void *p) {

    // 特殊的伪索引不应该调整
    // 示例中检查了 LUA_REGISTRYINDEX，您也可以添加其他伪索引的检查
    if (idx != LUA_REGISTRYINDEX) {
        // 只有当 idx 为负数且不是伪索引时才转换为正数索引
        if (idx < 0) {
            idx += lua_gettop(L) + 1;
        }
    }

    // 以下是设置值的标准Lua C API用法
    lua_pushlightuserdata(L, (void *)p); // 将指针入栈
    lua_insert(L, -2);                   // 将指针移动到待设置值的下方
    lua_rawset(L, idx);

}

static int
lua_rawgetp(lua_State *L, int idx, const void *p) {
    if (idx != LUA_REGISTRYINDEX) {
        // 只有当 idx 为负数且不是伪索引时才转换为正数索引
        if (idx < 0) {
            idx += lua_gettop(L) + 1;
        }
    }
    lua_pushlightuserdata(L, (void *)p);
    lua_rawget(L, idx);
    return lua_type(L, -1);
}

static void
lua_getuservalue(lua_State *L, int idx) {
    lua_getfenv(L, idx);
}


// lua 5.1 has no light c function
#define is_lightcfunction(L, idx) (0)

#else

#define mark_function_env(L,dL,t)

static int
is_lightcfunction(lua_State *L, int idx) {
    if (lua_iscfunction(L, idx)) {
        if (lua_getupvalue(L, idx, 1) == NULL) {
            return 1;
        }
        lua_pop(L, 1);
    }
    return 0;
}

#endif


static int sock = 0;
static int port = 8080;
static int isCheck = 1;

// 创建tcp
static void
create_tcp() {
    struct sockaddr_in serv_addr;

    printf("create_tcp\n");
    // 创建套接字
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nSocket creation error\n");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // 转换IP地址
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported\n");
    }

    // 连接服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed\n");
    }
}

// 假设最大字符串大小为512字符
#define MAX_STRING_SIZE 256

// 拼接字符串并返回指向静态缓冲区的指针
static const char* concat(const char* source, const char* name, int line, int event, struct timeval *tv) {
    static char result[MAX_STRING_SIZE];
    if (source == NULL || name == NULL || tv == NULL) {
        fprintf(stderr, "concat: Invalid arguments\n");
        return NULL;
    }

    if (event == 1) {
        snprintf(result, MAX_STRING_SIZE, "0+%s:%d:%s:%ld.%06ld", source, line, name, tv->tv_sec, tv->tv_usec);
    } else {
        snprintf(result, MAX_STRING_SIZE, "1+%s:%d:%s:%ld.%06ld", source, line, name, tv->tv_sec, tv->tv_usec);
    }

    return result;
}



// 关闭tcp
static int
close_tcp() {
    printf("关闭tcp\n");
    close(sock);
    return 0;
}

// 定义消息队列节点
typedef struct message_node {
    char msg[MAX_STRING_SIZE];
    struct timeval tv;
    struct message_node *next;
} message_node;

// 定义消息队列
typedef struct {
    message_node *head;
    message_node *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int stop;
} message_queue;

static message_queue msg_queue = {0};

// 初始化消息队列
static void
init_message_queue(message_queue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    queue->stop = 0;
}

// 释放消息队列
static void
free_message_queue(message_queue *queue) {
    while (queue->head) {
        message_node *node = queue->head;
        queue->head = node->next;
        free(node);
    }
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

// 将消息放入队列
static void
enqueue_message(message_queue *queue, const char *msg, struct timeval *tv) {
    message_node *node = (message_node *)malloc(sizeof(message_node));
    if (!node) {
        perror("Failed to allocate memory for message node");
        return;
    }
    strncpy(node->msg, msg, MAX_STRING_SIZE - 1);
    node->msg[MAX_STRING_SIZE - 1] = '\0';
    node->tv = *tv; // 复制时间戳
    node->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail) {
        queue->tail->next = node;
        queue->tail = node;
    } else {
        queue->head = queue->tail = node;
    }
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

// 从队列中取出消息并发送
static message_node *
dequeue_message(message_queue *queue) {
    pthread_mutex_lock(&queue->mutex);
    while (!queue->head && !queue->stop) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->stop) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    message_node *node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);
    return node;
}

// 消息线程函数，从队列中取出消息并发送
void *
message_thread_func(void *arg) {
    message_queue *queue = (message_queue *)arg;
    while (1) {
        message_node *node = dequeue_message(queue);
        if (!node) {
            break;
        }
        if (msg_queue.stop == 1) {
            break;
        }

        uint32_t msg_len = strlen(node->msg);
        uint32_t net_msg_len = htonl(msg_len); // 转换为网络字节序
        char buffer[MAX_STRING_SIZE + 4]; // 为消息和长度保留空间

        // 将长度和消息复制到缓冲区
        memcpy(buffer, &net_msg_len, 4);
        memcpy(buffer + 4, node->msg, msg_len);

        if (send(sock, buffer, msg_len + 4, 0) < 0) {
            printf("Send failed\n");
            msg_queue.stop = 1;
            close_tcp();
            isCheck = 0;
        }

        free(node);
    }
    return NULL;
}

// 启动消息线程
static pthread_t message_thread;

static void
start_message_thread() {
    msg_queue.stop = 0;
    pthread_create(&message_thread, NULL, message_thread_func, &msg_queue);
}

// 停止消息线程
static void
stop_message_thread() {
    pthread_mutex_lock(&msg_queue.mutex);
    msg_queue.stop = 1;
    pthread_cond_broadcast(&msg_queue.cond);
    pthread_mutex_unlock(&msg_queue.mutex);
    pthread_join(message_thread, NULL);
}

static void
profiler_hook_time(lua_State *L, lua_Debug *ar) {
    if( isCheck == 0 ){
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL); // 获取当前时间并存储在 tv 中

    if (ar->event == LUA_HOOKCALL) {
        if (lua_getinfo(L, "Sn", ar) != 0) {
            const char *source = ar->source ? ar->source : "[unknown]";
            const char *name = ar->name ? ar->name : "[unknown]";
            int linedefined = ar->linedefined;


            const char *str = concat(source, name, linedefined, 1, &tv);
            if (str != NULL) {
                enqueue_message(&msg_queue, str, &tv);
            } else {
                fprintf(stderr, "profiler_hook_time: concat returned NULL for LUA_HOOKCALL\n");
            }
        }
    } else if (ar->event == LUA_HOOKRET) {
        if (lua_getinfo(L, "Sn", ar) != 0) {
            const char *source = ar->source ? ar->source : "[unknown]";
            const char *name = ar->name ? ar->name : "[unknown]";
            int linedefined = ar->linedefined;

            const char *str = concat(source, name, linedefined, 0, &tv);
            if (str != NULL) {
                enqueue_message(&msg_queue, str, &tv);
            } else {
                fprintf(stderr, "profiler_hook_time: concat returned NULL for LUA_HOOKRET\n");
            }
        }
    }
}

static int
lstart(lua_State *L) {
    lua_State *cL = L;
    int args = 0;
    if (lua_isthread(L, 1)) {
        cL = lua_tothread(L, 1);
        args = 1;
    }
    port = (int)luaL_optinteger(L, args+1, 8080);

    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, cL);
    isCheck = 1;
    create_tcp();
    init_message_queue(&msg_queue);
    start_message_thread();
    lua_sethook(cL, profiler_hook_time, LUA_MASKCALL | LUA_MASKRET, 0);
    return 0;
}

static int
lstop(lua_State *L) {
    isCheck = 0;
    lua_State *cL = L;
    if (lua_isthread(L, 1)) {
        cL = lua_tothread(L, 1);
    }
    stop_message_thread();
    close_tcp();
    free_message_queue(&msg_queue);
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, cL) != LUA_TNIL) {
        lua_pushnil(L);
        lua_rawsetp(L, LUA_REGISTRYINDEX, cL);
        lua_sethook(cL, NULL, 0, 0);
    } else {
        return luaL_error(L, "thread profiler not begin");
    }
    return 0;
}

int luaopen_profiler(lua_State *L) {
    const struct luaL_Reg l[] = {
            { "start", lstart },
            { "stop", lstop },
            { NULL, NULL },
    };

    luaL_register(L, "profiler", l);
    return 1; // 新库压栈
}
