// 底层编码与内存：同样的数据用不同的方式存，各占多少内存；listpack 为什么只用在小对象上
#include <string.h>

#include "common.h"

#define N 1000000

static void drain(redisContext* c, int n)
{
    for (int i = 0; i < n; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        CHECK(r->type != REDIS_REPLY_ERROR);
        freeReplyObject(r);
    }
}

static long long usedMemory(redisContext* c)
{
    redisReply* r = cmd(c, "INFO memory");
    long long v = atoll(strstr(r->str, "used_memory:") + strlen("used_memory:"));
    freeReplyObject(r);
    return v;
}

static void encodingOf(redisContext* c, const char* key, char* out, size_t n)
{
    redisReply* r = cmd(c, "OBJECT ENCODING %s", key);
    snprintf(out, n, "%s", r->str);
    freeReplyObject(r);
}

// 读出 commandstats 中某个命令的 usec_per_call
static double usecPerCall(redisContext* c, const char* name)
{
    redisReply* r = cmd(c, "INFO commandstats");
    char pat[64];
    snprintf(pat, sizeof pat, "cmdstat_%s:", name);
    char* p = strstr(r->str, pat);
    double v = p ? atof(strstr(p, "usec_per_call=") + strlen("usec_per_call=")) : -1;
    freeReplyObject(r);
    return v;
}

int main(void)
{
    redisContext* c = connectRedis();
    char enc[32];

    // 1. 100 万对「用户 id → 名字」，三种存法
    printf("== 100 万对 id → 名字，三种存法\n");
    freeReplyObject(cmd(c, "FLUSHALL"));
    long long base = usedMemory(c);
    for (int i = 0; i < N; i += 10000) {
        for (int k = 0; k < 10000; ++k) redisAppendCommand(c, "SET user:%d name-%d", i + k, i + k);
        drain(c, 10000);
    }
    encodingOf(c, "user:1", enc, sizeof enc);
    long long strMem = usedMemory(c) - base;
    printf("  每个用户一个字符串 key：     %6.1f MB，平均每对 %5.1f 字节（编码 %s）\n", strMem / 1048576.0,
           (double)strMem / N, enc);

    int buckets[] = {100, 1000};
    for (int b = 0; b < 2; ++b) {
        int per = buckets[b];
        freeReplyObject(cmd(c, "FLUSHALL"));
        base = usedMemory(c);
        for (int i = 0; i < N; i += 10000) {
            for (int k = 0; k < 10000; ++k) {
                int id = i + k;
                redisAppendCommand(c, "HSET user:%d %d name-%d", id / per, id % per, id);   // 第 id/per 个哈希的第 id%per 个字段
            }
            drain(c, 10000);
        }
        encodingOf(c, "user:0", enc, sizeof enc);
        long long m = usedMemory(c) - base;
        printf("  每 %4d 个用户放进一个哈希：  %6.1f MB，平均每对 %5.1f 字节（编码 %s）\n", per, m / 1048576.0,
               (double)m / N, enc);
    }

    // 2. 有序集合：128 个成员以内是 listpack，超过就转成跳表 + 哈希表
    printf("== 有序集合的编码转换\n");
    freeReplyObject(cmd(c, "FLUSHALL"));
    int sizes[] = {128, 129};
    for (int s = 0; s < 2; ++s) {
        char key[16];
        snprintf(key, sizeof key, "z%d", sizes[s]);
        for (int i = 0; i < sizes[s]; ++i) redisAppendCommand(c, "ZADD %s %d member-%d", key, i, i);
        drain(c, sizes[s]);
        encodingOf(c, key, enc, sizeof enc);
        redisReply* r = cmd(c, "MEMORY USAGE %s", key);
        printf("  %d 个成员：%-9s %6lld 字节\n", sizes[s], enc, r->integer);
        freeReplyObject(r);
    }

    // 3. listpack 中查找要顺序扫描：放宽阈值，比较不同大小的哈希中 HGET 的耗时
    printf("== HGET 的服务器端耗时（每次调用的平均微秒数）\n");
    freeReplyObject(cmd(c, "CONFIG SET hash-max-listpack-entries 100000"));
    int fields[] = {10, 100, 512, 1000, 10000};
    for (int f = 0; f < 5; ++f) {
        for (int pass = 0; pass < 2; ++pass) {   // pass 0：listpack；pass 1：哈希表
            freeReplyObject(cmd(c, "DEL h"));
            freeReplyObject(cmd(c, "CONFIG SET hash-max-listpack-entries %d", pass == 0 ? 100000 : 0));
            for (int i = 0; i < fields[f]; ++i) redisAppendCommand(c, "HSET h field-%d value-%d", i, i);
            drain(c, fields[f]);
            encodingOf(c, "h", enc, sizeof enc);
            freeReplyObject(cmd(c, "CONFIG RESETSTAT"));
            for (int i = 0; i < 100000; i += 1000) {
                for (int k = 0; k < 1000; ++k) redisAppendCommand(c, "HGET h field-%d", randInt(0, fields[f] - 1));
                drain(c, 1000);
            }
            double us = usecPerCall(c, "hget");
            redisReply* r = cmd(c, "MEMORY USAGE h");
            printf("  %5d 个字段  %-9s  HGET %6.2f 微秒  内存 %8lld 字节\n", fields[f], enc, us, r->integer);
            freeReplyObject(r);
        }
    }
    freeReplyObject(cmd(c, "CONFIG SET hash-max-listpack-entries 512"));
    freeReplyObject(cmd(c, "FLUSHALL"));
    redisFree(c);
    return 0;
}
