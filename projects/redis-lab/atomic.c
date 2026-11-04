// 原子操作：8 个线程各加 1 万次。INCR 由服务器一次完成；「GET 再 SET」分成两步，中间会被别的线程插进来
#include <pthread.h>
#include <string.h>

#include "common.h"

#define THREADS 8
#define TIMES 10000
static int g_useIncr;

static void* worker(void* arg)
{
    (void)arg;
    redisContext* c = connectRedis();
    for (int i = 0; i < TIMES; ++i) {
        if (g_useIncr) {
            freeReplyObject(cmd(c, "INCR counter"));
        } else {
            redisReply* r = cmd(c, "GET counter");   // 读出来
            long long v = r->type == REDIS_REPLY_STRING ? atoll(r->str) : 0;
            freeReplyObject(r);
            freeReplyObject(cmd(c, "SET counter %lld", v + 1));   // 加一以后写回去
        }
    }
    redisFree(c);
    return NULL;
}

int main(void)
{
    redisContext* c = connectRedis();
    for (g_useIncr = 0; g_useIncr <= 1; ++g_useIncr) {
        freeReplyObject(cmd(c, "DEL counter"));
        pthread_t t[THREADS];
        for (int i = 0; i < THREADS; ++i) pthread_create(&t[i], NULL, worker, NULL);
        for (int i = 0; i < THREADS; ++i) pthread_join(t[i], NULL);
        redisReply* r = cmd(c, "GET counter");
        printf("%s：%d 个线程各加 %d 次，counter = %s（应该是 %d）\n", g_useIncr ? "INCR       " : "GET 再 SET ", THREADS,
               TIMES, r->str, THREADS * TIMES);
        freeReplyObject(r);
    }
    redisFree(c);
    return 0;
}
