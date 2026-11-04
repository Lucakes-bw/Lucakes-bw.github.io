// 内存淘汰：内存设上限，按齐夫分布访问，未命中时写入。比较各种淘汰策略的命中率，以及和精确 LRU 的差距
#include <math.h>
#include <string.h>

#include "common.h"

#define KEYS 100000            // 不同的 key 有多少个
#define ACCESSES 500000        // 访问次数
static char g_value[101];      // 每个值 100 字节

static int g_trace[ACCESSES];
static int g_failed;            // 写入失败的次数

static void makeZipfTrace(void)
{
    double* cdf = malloc(sizeof(double) * KEYS);
    double sum = 0;
    for (int r = 0; r < KEYS; ++r) cdf[r] = (sum += 1.0 / (r + 1));
    for (int i = 0; i < ACCESSES; ++i) {
        double u = (double)(nextRand() >> 11) / 9007199254740992.0 * sum;
        int lo = 0, hi = KEYS - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cdf[mid] < u) lo = mid + 1;
            else hi = mid;
        }
        g_trace[i] = (int)((unsigned)lo * 2654435761u % KEYS);   // 打乱：热门的 key 不要都是小编号
    }
    free(cdf);
}

static long long infoField(redisContext* c, const char* section, const char* name)
{
    redisReply* r = cmd(c, "INFO %s", section);
    char pat[64];
    snprintf(pat, sizeof pat, "%s:", name);
    char* p = strstr(r->str, pat);
    long long v = p ? atoll(p + strlen(pat)) : -1;
    freeReplyObject(r);
    return v;
}

// 精确的 LRU：数组实现的双向链表，key 就是下标
static double exactLRU(int capacity)
{
    int *prev = malloc(sizeof(int) * (KEYS + 1)), *next = malloc(sizeof(int) * (KEYS + 1));
    char* in = calloc(KEYS, 1);
    int head = KEYS;                       // 哨兵：next[head] 是最近用过的，prev[head] 是最久没用的
    prev[head] = next[head] = head;
    int size = 0, hits = 0;
    for (int i = 0; i < ACCESSES; ++i) {
        int k = g_trace[i];
        if (in[k]) {
            ++hits;
            next[prev[k]] = next[k];
            prev[next[k]] = prev[k];
        } else {
            if (size == capacity) {
                int old = prev[head];
                next[prev[old]] = head;
                prev[head] = prev[old];
                in[old] = 0;
                --size;
            }
            in[k] = 1;
            ++size;
        }
        next[k] = next[head];
        prev[k] = head;
        prev[next[head]] = k;
        next[head] = k;
    }
    free(prev);
    free(next);
    free(in);
    return (double)hits / ACCESSES;
}

// 在 Redis 上按访问序列执行：GET，未命中就 SET。返回命中率，并输出结束时缓存了多少个 key
static double runRedis(redisContext* c, const char* policy, int samples, long long maxmem, long long* keysAtEnd)
{
    freeReplyObject(cmd(c, "FLUSHALL"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory 0"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory-policy %s", policy));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory-samples %d", samples));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory %lld", maxmem));
    int hits = 0;
    g_failed = 0;
    for (int i = 0; i < ACCESSES; ++i) {
        redisReply* r = cmd(c, "GET k:%d", g_trace[i]);
        if (r->type == REDIS_REPLY_STRING) ++hits;
        else {
            redisReply* w = redisCommand(c, "SET k:%d %s", g_trace[i], g_value);
            CHECK(w);
            if (w->type == REDIS_REPLY_ERROR) ++g_failed;   // 有淘汰策略时，写入正常情况下不会失败
            freeReplyObject(w);
        }
        freeReplyObject(r);
    }
    redisReply* r = cmd(c, "DBSIZE");
    *keysAtEnd = r->integer;
    freeReplyObject(r);
    return (double)hits / ACCESSES;
}

int main(void)
{
    memset(g_value, 'x', 100);
    makeZipfTrace();
    redisContext* c = connectRedis();

    // 先看 1 万个这样的 key 占多少内存，据此把上限设成「大约能放 1 万个」
    freeReplyObject(cmd(c, "CONFIG SET maxmemory 0"));
    freeReplyObject(cmd(c, "FLUSHALL"));
    long long base = infoField(c, "memory", "used_memory");
    for (int i = 0; i < 10000; ++i) freeReplyObject(cmd(c, "SET k:%d %s", i, g_value));
    long long per = (infoField(c, "memory", "used_memory") - base) / 10000;
    long long maxmem = base + per * 10000;
    printf("每个 key 大约 %lld 字节，空库 %.2f MB，maxmemory 设为 %.2f MB\n", per, base / 1048576.0, maxmem / 1048576.0);

    // noeviction：写满以后再写就报错
    freeReplyObject(cmd(c, "FLUSHALL"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory-policy noeviction"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory %lld", maxmem));
    for (int i = 0;; ++i) {
        redisReply* r = redisCommand(c, "SET k:%d %s", i, g_value);
        if (r->type == REDIS_REPLY_ERROR) {
            printf("noeviction：写到第 %d 个 key 时报错：%s\n", i + 1, r->str);
            freeReplyObject(r);
            break;
        }
        freeReplyObject(r);
    }

    printf("== 齐夫分布，%d 个不同的 key，%d 次访问\n", KEYS, ACCESSES);
    struct { const char* policy; int samples; } runs[] = {
        {"allkeys-random", 5}, {"allkeys-lru", 5}, {"allkeys-lru", 10}, {"allkeys-lfu", 5}};
    long long keysAtEnd;
    for (int i = 0; i < 4; ++i) {
        double rate = runRedis(c, runs[i].policy, runs[i].samples, maxmem, &keysAtEnd);
        printf("  %-15s samples=%-2d 命中率 %5.1f%%  缓存了 %lld 个 key（同样容量的精确 LRU：%5.1f%%），淘汰了 %lld 次，写入失败 %d 次\n",
               runs[i].policy, runs[i].samples, rate * 100, keysAtEnd, exactLRU((int)keysAtEnd) * 100,
               infoField(c, "stats", "evicted_keys"), g_failed);
        freeReplyObject(cmd(c, "CONFIG RESETSTAT"));
    }

    freeReplyObject(cmd(c, "CONFIG SET maxmemory 0"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory-policy noeviction"));
    freeReplyObject(cmd(c, "CONFIG SET maxmemory-samples 5"));
    freeReplyObject(cmd(c, "FLUSHALL"));
    redisFree(c);
    return 0;
}
