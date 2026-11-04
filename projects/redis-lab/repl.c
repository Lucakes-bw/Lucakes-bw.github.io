// 主从复制与哨兵
//   repl write [wait]     向主节点不停地 INCR counter，直到连接断开；带 wait 时每次写完都 WAIT 1 个从节点确认
//   repl read             输出 counter 的值
//   repl failover 秒数    通过哨兵找到主节点，不停地 INCR；主节点挂掉后重新向哨兵询问，统计写入中断了多久
#include <string.h>
#include <unistd.h>

#include "common.h"

// 向哨兵（REDIS_HOST 中逗号分隔的多个地址，端口 26379）询问主节点的地址
static int askSentinel(char* host, size_t n, int* port)
{
    char list[256];
    snprintf(list, sizeof list, "%s", getenv("REDIS_HOST"));
    for (char* h = strtok(list, ","); h; h = strtok(NULL, ",")) {
        struct timeval tv = {0, 200000};
        redisContext* s = redisConnectWithTimeout(h, 26379, tv);
        if (!s || s->err) {
            if (s) redisFree(s);
            continue;
        }
        redisReply* r = redisCommand(s, "SENTINEL get-master-addr-by-name mymaster");
        int ok = r && r->type == REDIS_REPLY_ARRAY && r->elements == 2;
        if (ok) {
            snprintf(host, n, "%s", r->element[0]->str);
            *port = atoi(r->element[1]->str);
        }
        if (r) freeReplyObject(r);
        redisFree(s);
        if (ok) return 1;
    }
    return 0;
}

static redisContext* connectTo(const char* host, int port)
{
    struct timeval tv = {0, 200000};
    redisContext* c = redisConnectWithTimeout(host, port, tv);
    if (!c || c->err) {
        if (c) redisFree(c);
        return NULL;
    }
    redisSetTimeout(c, tv);   // 读写也设超时：主节点被暂停时不要一直等
    return c;
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "read";
    if (strcmp(mode, "write") == 0) {
        int wait = argc > 2 && strcmp(argv[2], "wait") == 0;
        redisContext* c = connectRedis();
        long long acked = 0, n = 0, timeouts = 0;
        double t0 = nowSeconds();
        for (;;) {
            redisReply* r = redisCommand(c, "INCR counter");
            if (!r) break;
            CHECK(r->type == REDIS_REPLY_INTEGER);
            long long v = r->integer;
            freeReplyObject(r);
            if (wait) {             // 等待至少 1 个从节点确认收到，最多等 1000 毫秒
                r = redisCommand(c, "WAIT 1 1000");
                if (!r) break;
                CHECK(r->type == REDIS_REPLY_INTEGER);
                long long replicas = r->integer;
                freeReplyObject(r);
                if (replicas < 1) {   // 超时，没有从节点确认：不能算写入成功，应用应该报错或重试
                    ++timeouts;
                    continue;
                }
            }
            acked = v;
            ++n;
        }
        printf("最后一次得到确认的值：%lld（%.0f 次/秒）", acked, n / (nowSeconds() - t0));
        if (wait) printf("，WAIT 超时 %lld 次", timeouts);
        printf("\n");
        redisFree(c);
    } else if (strcmp(mode, "read") == 0) {
        redisContext* c = connectRedis();
        redisReply* r = cmd(c, "GET counter");
        printf("counter = %s\n", r->type == REDIS_REPLY_STRING ? r->str : "（不存在）");
        freeReplyObject(r);
        redisFree(c);
    } else if (strcmp(mode, "failover") == 0) {
        double seconds = argc > 2 ? atof(argv[2]) : 30;
        char host[64] = "", lastHost[64] = "";
        int port = 0;
        redisContext* c = NULL;
        long long acked = 0;
        double t0 = nowSeconds(), downSince = -1, lastPrint = 0;
        while (nowSeconds() - t0 < seconds) {
            if (!c) {
                if (!askSentinel(host, sizeof host, &port) || !(c = connectTo(host, port))) {
                    usleep(100000);
                    continue;
                }
                if (strcmp(host, lastHost) != 0) {
                    printf("%6.2fs 哨兵说主节点是 %s:%d\n", nowSeconds() - t0, host, port);
                    snprintf(lastHost, sizeof lastHost, "%s", host);
                }
            }
            redisReply* r = redisCommand(c, "INCR counter");
            if (!r || r->type != REDIS_REPLY_INTEGER) {   // 连接断开、超时，或者对方已经变成只读的从节点
                if (downSince < 0) {
                    downSince = nowSeconds();
                    printf("%6.2fs 写入失败：%s，最后一次确认的值 %lld\n", downSince - t0,
                           r ? r->str : c->errstr, acked);
                }
                if (r) freeReplyObject(r);
                redisFree(c);
                c = NULL;
                usleep(100000);
                continue;
            }
            acked = r->integer;
            freeReplyObject(r);
            if (downSince >= 0) {
                printf("%6.2fs 恢复写入，中断了 %.2f 秒；新的主节点上 counter = %lld\n", nowSeconds() - t0,
                       nowSeconds() - downSince, acked);
                downSince = -1;
            }
            if (nowSeconds() - lastPrint > 5) {
                lastPrint = nowSeconds();
                printf("%6.2fs counter = %lld\n", lastPrint - t0, acked);
            }
        }
        if (c) redisFree(c);
    }
    return 0;
}
