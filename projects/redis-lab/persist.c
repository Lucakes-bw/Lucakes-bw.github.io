// 持久化：不同配置下写入的吞吐量；进程被 kill -9 以后，已经确认的写入还在不在
//   persist bench         逐个 SET 5 万次，输出吞吐量
//   persist write         不停地 INCR counter，直到连接断开，输出最后一次得到确认的值
//   persist read          输出 counter 现在的值
//   persist fork          写入约 400 MB 数据，看 BGSAVE 时 fork 的耗时和写时复制的内存
#include <string.h>
#include <unistd.h>

#include "common.h"

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

static void drain(redisContext* c, int n)
{
    for (int i = 0; i < n; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        CHECK(r->type != REDIS_REPLY_ERROR);
        freeReplyObject(r);
    }
}

// BGSAVE 期间：rewrite 为真时把所有 key 重写一遍；否则只是每毫秒 PING 一次。输出 fork 耗时、PING 的最大延迟、写时复制的内存
static void bgsave(redisContext* c, int nkeys, const char* value, int rewrite)
{
    while (infoField(c, "persistence", "rdb_bgsave_in_progress") != 0) usleep(100000);
    freeReplyObject(cmd(c, "BGSAVE"));
    double t0 = nowSeconds(), maxLat = 0;
    int written = 0;
    while (infoField(c, "persistence", "rdb_bgsave_in_progress") != 0 || nowSeconds() - t0 < 0.1) {
        double a = nowSeconds();
        if (rewrite && written < nkeys) {
            for (int k = 0; k < 1000; ++k) redisAppendCommand(c, "SET key:%d %s", written + k, value);
            drain(c, 1000);
            written += 1000;
        } else {
            freeReplyObject(cmd(c, "PING"));
            usleep(1000);
        }
        double lat = nowSeconds() - a;
        if (lat > maxLat) maxLat = lat;
    }
    double total = nowSeconds() - t0;
    printf("  %s：fork %.1f 毫秒，BGSAVE 共 %.2f 秒，期间%s最慢一次 %.1f 毫秒，写时复制 %.1f MB\n",
           rewrite ? "同时重写全部 key" : "期间没有写入    ", infoField(c, "stats", "latest_fork_usec") / 1000.0, total,
           rewrite ? "每批 1000 个 SET " : "PING ", maxLat * 1000, infoField(c, "persistence", "rdb_last_cow_size") / 1048576.0);
    if (rewrite) printf("  BGSAVE 期间重写了 %d 个 key\n", written);
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "bench";
    redisContext* c = connectRedis();
    if (strcmp(mode, "bench") == 0) {
        int n = 50000;
        double t0 = nowSeconds();
        for (int i = 0; i < n; ++i) freeReplyObject(cmd(c, "SET key:%d %d", i, i));
        double t = nowSeconds() - t0;
        printf("逐个 SET %d 次：%.2f 秒，%.0f 次/秒，平均 %.1f 微秒\n", n, t, n / t, t / n * 1e6);
    } else if (strcmp(mode, "write") == 0) {
        long long acked = 0;
        for (;;) {
            redisReply* r = redisCommand(c, "INCR counter");
            if (!r) break;                  // 连接断开：服务器被杀掉了
            CHECK(r->type == REDIS_REPLY_INTEGER);
            acked = r->integer;             // 服务器已经回复：客户端认为这次写入成功了
            freeReplyObject(r);
        }
        printf("最后一次得到确认的值：%lld\n", acked);
    } else if (strcmp(mode, "fork") == 0) {
        int nkeys = 1500000;
        char value[201];
        memset(value, 'v', 200);
        value[200] = '\0';
        freeReplyObject(cmd(c, "FLUSHALL"));
        for (int i = 0; i < nkeys; i += 10000) {
            for (int k = 0; k < 10000; ++k) redisAppendCommand(c, "SET key:%d %s", i + k, value);
            drain(c, 10000);
        }
        printf("%d 个 key，used_memory %.0f MB\n", nkeys, infoField(c, "memory", "used_memory") / 1048576.0);
        bgsave(c, nkeys, value, 0);
        bgsave(c, nkeys, value, 1);
        freeReplyObject(cmd(c, "FLUSHALL"));
    } else {
        redisReply* r = cmd(c, "GET counter");
        printf("重启以后 counter = %s\n", r->type == REDIS_REPLY_STRING ? r->str : "（不存在）");
        freeReplyObject(r);
    }
    redisFree(c);
    return 0;
}
