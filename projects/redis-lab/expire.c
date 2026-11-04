// 过期的 key 什么时候真正被删掉：惰性删除 + 定期删除。写入一批 2 秒后过期的 key，之后不再访问，每 200 毫秒观察一次
#include <string.h>
#include <unistd.h>

#include "common.h"

#define N 200000

static long long infoField(redisContext* c, const char* section, const char* name)
{
    redisReply* r = cmd(c, "INFO %s", section);
    char pat[64];
    snprintf(pat, sizeof pat, "\n%s:", name);
    char* p = strstr(r->str, pat);
    long long v = p ? atoll(p + strlen(pat)) : -1;
    freeReplyObject(r);
    return v;
}

static long long keysInDb(redisContext* c)      // INFO keyspace 中 db0 的 keys=，和 DBSIZE 一样包括已过期但还没删掉的
{
    redisReply* r = cmd(c, "DBSIZE");
    long long v = r->integer;
    freeReplyObject(r);
    return v;
}

int main(void)
{
    redisContext* c = connectRedis();
    freeReplyObject(cmd(c, "FLUSHALL"));
    freeReplyObject(cmd(c, "CONFIG RESETSTAT"));
    for (int i = 0; i < N; ++i) redisAppendCommand(c, "SET tmp:%d %d PX 2000", i, i);   // 2 秒后过期
    for (int i = 0; i < N; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        freeReplyObject(r);
    }
    double t0 = nowSeconds();
    long long mem0 = infoField(c, "memory", "used_memory");
    printf("写入 %d 个 2 秒后过期的 key，之后不再访问它们\n", N);
    printf("%6s %10s %12s %10s\n", "时间", "DBSIZE", "expired_keys", "内存(MB)");
    for (int step = 0; step <= 20; ++step) {
        double t = nowSeconds() - t0;
        printf("%5.1fs %10lld %12lld %10.1f\n", t, keysInDb(c), infoField(c, "stats", "expired_keys"),
               infoField(c, "memory", "used_memory") / 1048576.0);
        usleep(200000);
    }
    printf("开始时内存 %.1f MB\n", mem0 / 1048576.0);

    // 第二个实验：19 万个 1 小时后过期的 key，1 万个 1 秒后过期的 key
    freeReplyObject(cmd(c, "FLUSHALL"));
    freeReplyObject(cmd(c, "CONFIG RESETSTAT"));
    for (int i = 0; i < N; ++i) {
        if (i < 10000) redisAppendCommand(c, "SET short:%d %d PX 1000", i, i);
        else redisAppendCommand(c, "SET long:%d %d EX 3600", i, i);
    }
    for (int i = 0; i < N; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        freeReplyObject(r);
    }
    t0 = nowSeconds();
    printf("\n19 万个 1 小时后过期的 key + 1 万个 1 秒后过期的 key\n");
    printf("%6s %10s %12s\n", "时间", "DBSIZE", "expired_keys");
    for (int step = 0; step <= 10; ++step) {
        printf("%5.1fs %10lld %12lld\n", nowSeconds() - t0, keysInDb(c), infoField(c, "stats", "expired_keys"));
        sleep(1);
    }
    // 还留着的过期 key：访问时才发现它过期了，返回空并删除（惰性删除）
    long long before = infoField(c, "stats", "expired_keys"), left = keysInDb(c) - (N - 10000);
    int found = 0;
    for (int i = 0; i < 10000; ++i) {
        redisReply* r = cmd(c, "GET short:%d", i);
        if (r->type != REDIS_REPLY_NIL) ++found;
        freeReplyObject(r);
    }
    printf("还没删掉的过期 key 有 %lld 个；访问全部 1 万个 short key：返回了值的 %d 个，expired_keys 增加 %lld，DBSIZE 变成 %lld\n",
           left, found, infoField(c, "stats", "expired_keys") - before, keysInDb(c));
    freeReplyObject(cmd(c, "FLUSHALL"));
    redisFree(c);
    return 0;
}
