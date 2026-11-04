// Pipeline：一次发出多条命令，再一起读回复。比较不同批量大小下读 10 万个 key 的时间
#include <string.h>

#include "common.h"

static int N = 100000;

// 每批发出 batch 条 GET，再读回 batch 个回复
static double runBatch(redisContext* c, int batch)
{
    double t0 = nowSeconds();
    for (int i = 0; i < N; i += batch) {
        int n = i + batch <= N ? batch : N - i;
        for (int k = 0; k < n; ++k) redisAppendCommand(c, "GET user:%d", i + k);   // 只写入发送缓冲区
        for (int k = 0; k < n; ++k) {                                              // 第一次读时才真正发出去
            redisReply* r;
            CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
            char want[32];
            snprintf(want, sizeof want, "name-%d", i + k);
            CHECK(r->type == REDIS_REPLY_STRING && strcmp(r->str, want) == 0);   // 回复和命令按顺序一一对应
            freeReplyObject(r);
        }
    }
    return nowSeconds() - t0;
}

// MGET：一条命令读多个 key
static double runMget(redisContext* c, int batch)
{
    const char** argv = malloc(sizeof(char*) * (size_t)(batch + 1));
    char(*keys)[24] = malloc(sizeof(*keys) * (size_t)batch);
    double t0 = nowSeconds();
    for (int i = 0; i < N; i += batch) {
        argv[0] = "MGET";
        for (int k = 0; k < batch; ++k) {
            snprintf(keys[k], sizeof keys[k], "user:%d", i + k);
            argv[k + 1] = keys[k];
        }
        redisReply* r = redisCommandArgv(c, batch + 1, argv, NULL);
        CHECK(r && r->type == REDIS_REPLY_ARRAY && (int)r->elements == batch);
        freeReplyObject(r);
    }
    double t = nowSeconds() - t0;
    free(argv);
    free(keys);
    return t;
}

int main(int argc, char** argv)
{
    if (argc > 1) N = atoi(argv[1]);   // 网络延迟大时可以少读一些
    redisContext* c = connectRedis();
    freeReplyObject(cmd(c, "FLUSHALL"));
    for (int i = 0; i < N; ++i) redisAppendCommand(c, "SET user:%d name-%d", i, i);
    for (int i = 0; i < N; ++i) {
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        freeReplyObject(r);
    }
    printf("读 %d 个 key\n", N);
    int sizes[] = {1, 10, 100, 1000, 10000};
    for (int i = 0; i < 5; ++i) {
        double t = runBatch(c, sizes[i]);
        printf("  Pipeline 每批 %5d 条：%6.3f 秒，%8.0f 次/秒\n", sizes[i], t, N / t);
    }
    double t = runMget(c, 100);
    printf("  MGET 每次 100 个 key： %6.3f 秒，%8.0f 个/秒\n", t, N / t);
    redisFree(c);
    return 0;
}
