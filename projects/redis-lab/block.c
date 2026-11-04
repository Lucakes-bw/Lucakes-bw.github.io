// 慢命令会挡住所有客户端：一个线程每毫秒 PING 一次，另一个连接执行 KEYS、SCAN、DEL 大 key，看 PING 等了多久
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#define NKEYS 1000000

static atomic_int g_running;
static double g_maxLat;          // 这一段时间中 PING 的最大延迟
static int g_slow, g_pings;      // 超过 10 毫秒的次数，总次数

static void* monitor(void* arg)
{
    (void)arg;
    redisContext* c = connectRedis();
    while (atomic_load(&g_running)) {
        double a = nowSeconds();
        freeReplyObject(cmd(c, "PING"));
        double lat = nowSeconds() - a;
        if (lat > g_maxLat) g_maxLat = lat;
        if (lat > 0.010) ++g_slow;
        ++g_pings;
        usleep(1000);
    }
    redisFree(c);
    return NULL;
}

static pthread_t g_tid;
static void startMonitor(void)
{
    g_maxLat = 0;
    g_slow = g_pings = 0;
    atomic_store(&g_running, 1);
    pthread_create(&g_tid, NULL, monitor, NULL);
    usleep(200000);
}
static void stopMonitor(const char* what, double t)
{
    usleep(200000);
    atomic_store(&g_running, 0);
    pthread_join(g_tid, NULL);
    printf("%-34s 用时 %8.1f 毫秒；同时 PING %4d 次，最慢 %7.1f 毫秒，超过 10 毫秒的 %d 次\n", what, t * 1000, g_pings,
           g_maxLat * 1000, g_slow);
}

static void drain(redisContext* c, int n)
{
    for (int i = 0; i < n; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        CHECK(r->type != REDIS_REPLY_ERROR);
        freeReplyObject(r);
    }
}

static void makeBigHash(redisContext* c, const char* key, int fields)
{
    for (int i = 0; i < fields; i += 1000) {
        redisAppendCommand(c, "HSET %s f%d v%d", key, i, i);
        for (int k = 1; k < 1000; ++k) redisAppendCommand(c, "HSET %s f%d v%d", key, i + k, i + k);
        drain(c, 1000);
    }
}

int main(void)
{
    redisContext* c = connectRedis();
    freeReplyObject(cmd(c, "FLUSHALL"));
    for (int i = 0; i < NKEYS; i += 10000) {
        for (int k = 0; k < 10000; ++k) redisAppendCommand(c, "SET key:%d %d", i + k, i + k);
        drain(c, 10000);
    }
    printf("准备了 %d 个 key\n", NKEYS);

    startMonitor();
    stopMonitor("（空闲，对照）", 0);

    startMonitor();
    double t0 = nowSeconds();
    redisReply* r = cmd(c, "KEYS *");
    double t = nowSeconds() - t0;
    CHECK(r->type == REDIS_REPLY_ARRAY && r->elements == NKEYS);
    freeReplyObject(r);
    stopMonitor("KEYS *", t);

    // SCAN：每次只遍历一小部分，游标为 0 时结束
    startMonitor();
    t0 = nowSeconds();
    long long cursor = 0, seen = 0, calls = 0;
    do {
        r = cmd(c, "SCAN %lld COUNT 1000", cursor);
        cursor = atoll(r->element[0]->str);
        seen += (long long)r->element[1]->elements;
        ++calls;
        freeReplyObject(r);
    } while (cursor != 0);
    t = nowSeconds() - t0;
    char buf[64];
    snprintf(buf, sizeof buf, "SCAN COUNT 1000（%lld 次调用）", calls);
    stopMonitor(buf, t);
    printf("  SCAN 一共返回 %lld 个 key（可能有重复，这里没有）\n", seen);

    // 大 key：一个有 100 万个字段的哈希
    makeBigHash(c, "big", NKEYS);
    startMonitor();
    t0 = nowSeconds();
    freeReplyObject(cmd(c, "DEL big"));
    stopMonitor("DEL 100 万个字段的哈希", nowSeconds() - t0);

    makeBigHash(c, "big", NKEYS);
    startMonitor();
    t0 = nowSeconds();
    freeReplyObject(cmd(c, "UNLINK big"));
    stopMonitor("UNLINK 100 万个字段的哈希", nowSeconds() - t0);

    r = cmd(c, "SLOWLOG GET 3");
    printf("SLOWLOG 中最近的慢命令：\n");
    for (size_t i = 0; i < r->elements; ++i) {
        redisReply* e = r->element[i];
        printf("  %-8s %8lld 微秒\n", e->element[3]->element[0]->str, e->element[2]->integer);
    }
    freeReplyObject(r);
    redisFree(c);
    return 0;
}
