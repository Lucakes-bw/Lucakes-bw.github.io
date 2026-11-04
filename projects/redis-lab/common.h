// 各个实验共用的小工具：计时、检查、随机数、连接 Redis
#ifndef REDIS_LAB_COMMON_H
#define REDIS_LAB_COMMON_H

#include <hiredis/hiredis.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static inline double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: 检查失败：%s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

// 固定种子的随机数（xorshift），每次运行结果相同
static unsigned long long g_rand = 88172645463325252ULL;
static inline unsigned long long nextRand(void)
{
    g_rand ^= g_rand << 13;
    g_rand ^= g_rand >> 7;
    g_rand ^= g_rand << 17;
    return g_rand;
}
static inline int randInt(int lo, int hi) { return lo + (int)(nextRand() % (unsigned long long)(hi - lo + 1)); }

// 服务器地址从环境变量 REDIS_HOST 读取，默认 127.0.0.1
static inline redisContext* connectRedis(void)
{
    const char* host = getenv("REDIS_HOST");
    const char* port = getenv("REDIS_PORT");
    redisContext* c = redisConnect(host ? host : "127.0.0.1", port ? atoi(port) : 6379);
    if (!c || c->err) {
        fprintf(stderr, "连接 Redis 失败：%s\n", c ? c->errstr : "内存不足");
        exit(1);
    }
    return c;
}

// 执行一条命令，出错就退出；返回的回复需要 freeReplyObject
static inline redisReply* cmd(redisContext* c, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    redisReply* r = redisvCommand(c, fmt, ap);
    va_end(ap);
    if (!r) {
        fprintf(stderr, "命令失败：%s\n", c->errstr);
        exit(1);
    }
    if (r->type == REDIS_REPLY_ERROR) {
        fprintf(stderr, "Redis 返回错误：%s\n", r->str);
        exit(1);
    }
    return r;
}

#endif
