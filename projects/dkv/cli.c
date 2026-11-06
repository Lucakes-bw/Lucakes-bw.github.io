/* cli：dkv 的客户端和压测工具
 *
 *   cli lat ADDR N [get|put]                  顺序发 N 个请求，打印延迟分布
 *   cli incr ADDR N TIMEOUT_MS DEDUP CID      N 次加一，超时就重连重试；最后看计数器多加了几次
 *   cli seq ADDR PREFIX SECONDS OUT [TMO_MS [RATE]]  不停地写 PREFIX-i（RATE 为每秒次数，0 表示尽量快），
 *                                             把收到 OK 的写入记到 OUT
 *   cli check ADDR OUT                        OUT 中确认过的写入，ADDR 上还剩多少
 *   cli diff ADDR1 ADDR2                      比较两个节点的全部数据
 *   cli quorum W R SECONDS ADDR...            一个写者、一个读者，统计读到旧值的比例。
 *                                             写发给所有节点、等 W 个确认；读随机挑 R 个节点。
 *                                             环境变量 WRITER_IP 指定写者连接用的本端 IP（用来单独切断写者）
 *   cli lww ROUNDS OFFSET_US wall|lamport ADDR...   两个客户端先后写同一个键，B 的时钟慢 OFFSET
 */
#define _GNU_SOURCE
#include "net.h"

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cmpI64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static void printLat(const char *name, int64_t *v, long n)
{
    if (n == 0) { printf("%s: 0 ops\n", name); return; }
    qsort(v, (size_t)n, sizeof *v, cmpI64);
    double sum = 0;
    for (long i = 0; i < n; i++) sum += (double)v[i];
#define P(q) (v[(long)((double)(n - 1) * (q))] / 1000.0)
    printf("%s: n=%ld mean=%.3f p50=%.3f p90=%.3f p99=%.3f p99.9=%.3f max=%.3f ms\n",
           name, n, sum / (double)n / 1000.0, P(0.5), P(0.9), P(0.99), P(0.999), v[n - 1] / 1000.0);
#undef P
}

/* 发一行，等一行回复 */
static int call(int fd, LineReader *r, const char *req, char *resp, size_t cap, int timeoutMs)
{
    if (writeAll(fd, req, strlen(req)) < 0) return -1;
    return lrRead(r, resp, cap, timeoutMs);
}

/* ---------- lat ---------- */

static int cmdLat(int argc, char **argv)
{
    if (argc < 4) return 2;
    long n = atol(argv[3]);
    int put = argc > 4 && !strcmp(argv[4], "put");
    int fd = tcpConnect(argv[2], 2000);
    if (fd < 0) { perror("connect"); return 1; }
    LineReader r;
    lrInit(&r, fd);
    int64_t *lat = malloc((size_t)n * sizeof *lat);
    char req[128], resp[256];
    long slow = 0;
    int64_t t0 = nowUs();
    for (long i = 0; i < n; i++) {
        if (put) snprintf(req, sizeof req, "PUT k%ld v%ld\n", i % 1000, i);
        else snprintf(req, sizeof req, "GET k%ld\n", i % 1000);
        int64_t s = nowUs();
        if (call(fd, &r, req, resp, sizeof resp, -1) < 0) { fprintf(stderr, "connection lost\n"); return 1; }
        lat[i] = nowUs() - s;
        slow += lat[i] > 100000;
    }
    double secs = (double)(nowUs() - t0) / 1e6;
    printLat(put ? "put" : "get", lat, n);
    printf("total %.2f s, %.0f ops/s, >100ms: %ld\n", secs, (double)n / secs, slow);
    return 0;
}

/* ---------- incr ---------- */

static int cmdIncr(int argc, char **argv)
{
    if (argc < 7) return 2;
    const char *addr = argv[2];
    long n = atol(argv[3]);
    int tmo = atoi(argv[4]), dedup = atoi(argv[5]);
    unsigned cid = (unsigned)atoi(argv[6]);
    char key[32], req[128], resp[256];
    snprintf(key, sizeof key, "cnt%u", cid);
    int fd = -1;
    LineReader r;
    long retries = 0, reconnects = 0;
    int64_t *lat = malloc((size_t)n * sizeof *lat);
    int64_t t0 = nowUs();
    for (long i = 1; i <= n; i++) {
        if (dedup) snprintf(req, sizeof req, "INCR %s %u %ld\n", key, cid, i);
        else snprintf(req, sizeof req, "INCR %s\n", key);
        int64_t s = nowUs();
        for (;;) {                                      /* 直到收到回复为止 */
            if (fd < 0) {
                fd = tcpConnect(addr, 1000);
                if (fd < 0) { sleepMs(50); continue; }
                lrInit(&r, fd);
                reconnects++;
            }
            if (call(fd, &r, req, resp, sizeof resp, tmo) > 0 && !strncmp(resp, "OK", 2)) break;
            retries++;                                  /* 超时：不知道执行了没有，只能重发 */
            close(fd);
            fd = -1;
        }
        lat[i - 1] = nowUs() - s;
    }
    double secs = (double)(nowUs() - t0) / 1e6;
    if (fd >= 0) close(fd);
    sleepMs(1500);                                      /* 等还在路上的旧请求都到达 */
    fd = tcpConnect(addr, 2000);
    lrInit(&r, fd);
    snprintf(req, sizeof req, "GET %s\n", key);
    call(fd, &r, req, resp, sizeof resp, 5000);
    long final = atol(resp + 3);
    printLat("incr", lat, n);
    printf("increments %ld, retries %ld, %.1f s, counter = %ld, extra = %ld\n", n, retries, secs, final, final - n);
    return 0;
}

/* ---------- seq / check / diff ---------- */

static volatile sig_atomic_t stop;

static int cmdSeq(int argc, char **argv)
{
    if (argc < 6) return 2;
    const char *addr = argv[2], *prefix = argv[3];
    double secs = atof(argv[4]);
    FILE *out = fopen(argv[5], "w");
    int tmo = argc > 6 ? atoi(argv[6]) : 3000;
    double rate = argc > 7 ? atof(argv[7]) : 0;
    int fd = -1;
    LineReader r;
    long acked = 0, errs = 0, cap = 1 << 20;
    int64_t *lat = malloc((size_t)cap * sizeof *lat);
    char req[128], resp[256];
    int64_t t0 = nowUs(), end = t0 + (int64_t)(secs * 1e6), firstErr = 0, lastOk = 0;
    for (long i = 1; nowUs() < end; i++) {
        if (rate > 0) {                               /* 限速：第 i 个请求不早于 t0 + i/rate 发出 */
            int64_t due = t0 + (int64_t)((double)i * 1e6 / rate);
            while (nowUs() < due) sleepMs(1);
        }
        if (fd < 0) {
            fd = tcpConnect(addr, 500);
            if (fd < 0) { sleepMs(100); i--; continue; }
            lrInit(&r, fd);
        }
        snprintf(req, sizeof req, "PUT %s-%ld %ld\n", prefix, i, i);
        int64_t s = nowUs();
        int rc = call(fd, &r, req, resp, sizeof resp, tmo);
        if (rc > 0 && !strcmp(resp, "OK")) {
            if (acked < cap) lat[acked] = nowUs() - s;
            acked++;
            lastOk = wallUs();
            fprintf(out, "%s-%ld %ld %ld\n", prefix, i, i, (long)lastOk);
        } else {
            if (!errs) firstErr = wallUs();
            errs++;
            if (rc < 0) { close(fd); fd = -1; }       /* 超时或断开：换一条连接，写下一个键 */
            else sleepMs(10);                         /* ERR notprimary 等 */
        }
    }
    fclose(out);
    printLat("put", lat, acked < cap ? acked : cap);
    printf("acked %ld, errors %ld, %.0f acked/s, first error at %.3f, last ok at %.3f\n",
           acked, errs, (double)acked / secs, firstErr / 1e6, lastOk / 1e6);
    return 0;
}

typedef struct { char **k, **v; long n, cap; } KV;

static long kvFind(KV *m, const char *k);

static unsigned long hs(const char *s)
{
    unsigned long h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

/* 一个简单的开放寻址表，装 DUMP 的结果 */
static void kvPut(KV *m, const char *k, const char *v)
{
    if (m->n * 2 >= m->cap) {
        KV o = *m;
        m->cap = m->cap ? m->cap * 2 : 1 << 16;
        m->k = calloc((size_t)m->cap, sizeof(char *));
        m->v = calloc((size_t)m->cap, sizeof(char *));
        m->n = 0;
        for (long i = 0; i < o.cap; i++) if (o.k[i]) kvPut(m, o.k[i], o.v[i]);
        free(o.k);
        free(o.v);
    }
    for (unsigned long i = hs(k) % (unsigned long)m->cap;; i = (i + 1) % (unsigned long)m->cap) {
        if (!m->k[i]) { m->k[i] = strdup(k); m->v[i] = strdup(v); m->n++; return; }
        if (!strcmp(m->k[i], k)) { m->v[i] = strdup(v); return; }
    }
}

static long kvFind(KV *m, const char *k)
{
    if (!m->cap) return -1;
    for (unsigned long i = hs(k) % (unsigned long)m->cap; m->k[i]; i = (i + 1) % (unsigned long)m->cap)
        if (!strcmp(m->k[i], k)) return (long)i;
    return -1;
}

static int dump(const char *addr, KV *m)
{
    int fd = tcpConnect(addr, 2000);
    if (fd < 0) return -1;
    LineReader *r = malloc(sizeof *r);
    lrInit(r, fd);
    char line[256], k[128], v[128];
    writeAll(fd, "DUMP\n", 5);
    while (lrRead(r, line, sizeof line, 10000) >= 0 && strcmp(line, "END"))
        if (sscanf(line, "%127s %127s", k, v) == 2) kvPut(m, k, v);
    close(fd);
    free(r);
    return 0;
}

static int cmdCheck(int argc, char **argv)
{
    if (argc < 4) return 2;
    KV m = { 0 };
    if (dump(argv[2], &m) < 0) { fprintf(stderr, "cannot dump %s\n", argv[2]); return 1; }
    FILE *f = fopen(argv[3], "r");
    char k[128], v[128];
    long t, total = 0, missing = 0, tmin = 0, tmax = 0;
    while (fscanf(f, "%127s %127s %ld", k, v, &t) == 3) {
        total++;
        long i = kvFind(&m, k);
        if (i < 0 || strcmp(m.v[i], v)) {
            if (!missing) tmin = t;
            tmax = t;
            missing++;
        }
    }
    printf("acked %ld, missing on %s: %ld", total, argv[2], missing);
    if (missing) printf(" (acked between %.3f and %.3f)", tmin / 1e6, tmax / 1e6);
    printf("\n");
    return 0;
}

static int cmdDiff(int argc, char **argv)
{
    if (argc < 4) return 2;
    KV a = { 0 }, b = { 0 };
    if (dump(argv[2], &a) < 0 || dump(argv[3], &b) < 0) { fprintf(stderr, "dump failed\n"); return 1; }
    long onlyA = 0, onlyB = 0, differ = 0;
    for (long i = 0; i < a.cap; i++) {
        if (!a.k[i]) continue;
        long j = kvFind(&b, a.k[i]);
        if (j < 0) onlyA++;
        else if (strcmp(a.v[i], b.v[j])) differ++;
    }
    for (long i = 0; i < b.cap; i++) if (b.k[i] && kvFind(&a, b.k[i]) < 0) onlyB++;
    printf("%s keys %ld, %s keys %ld, only in first %ld, only in second %ld, different values %ld\n",
           argv[2], a.n, argv[3], b.n, onlyA, onlyB, differ);
    return 0;
}

/* ---------- quorum ---------- */

#define MAXN 8
typedef struct {
    int n;
    int fd[MAXN];
    LineReader *r[MAXN];
    int pending[MAXN];        /* 这条连接上还有几个回复没读 */
} Group;

static char *qAddr[MAXN];
static int qN, qW, qR;
static _Atomic uint64_t lastAcked;
static volatile int qStop;
static char qKey[32];                 /* 每次运行用一个新键，免得被上次的大版本号挡住 */

static void groupOpen(Group *g, const char *src)
{
    g->n = qN;
    for (int i = 0; i < qN; i++) {
        g->fd[i] = tcpConnectFrom(qAddr[i], 1000, src);
        g->r[i] = malloc(sizeof(LineReader));
        lrInit(g->r[i], g->fd[i]);
        g->pending[i] = 0;
    }
}

/* 把 req 发给 mask 中的节点，等到 need 个回复的第一个数字等于 tag。
 * onReply 处理每一个属于本次请求的回复。返回 0 成功，-1 超时 */
static int groupCall(Group *g, unsigned mask, const char *req, uint64_t tag, int need,
                     void (*onReply)(const char *, void *), void *ctx)
{
    for (int i = 0; i < g->n; i++) {
        if (g->fd[i] < 0 || !(mask >> i & 1)) continue;
        if (writeAll(g->fd[i], req, strlen(req)) < 0) { close(g->fd[i]); g->fd[i] = -1; continue; }
        g->pending[i]++;
    }
    int got = 0;
    int64_t deadline = nowUs() + 3000000;
    char line[256];
    while (got < need) {
        struct pollfd p[MAXN];
        int idx[MAXN], np = 0;
        for (int i = 0; i < g->n; i++) {
            if (g->fd[i] < 0 || !g->pending[i]) continue;
            /* 先把缓冲区里已有的行处理掉 */
            while (g->pending[i] && lrHasLine(g->r[i])) {
                lrRead(g->r[i], line, sizeof line, 0);
                g->pending[i]--;
                unsigned long t;
                if (sscanf(line, "OK %lu", &t) == 1 && t == tag) { got++; if (onReply) onReply(line, ctx); }
            }
            if (g->pending[i]) { p[np].fd = g->fd[i]; p[np].events = POLLIN; idx[np++] = i; }
        }
        if (got >= need) break;
        int64_t left = deadline - nowUs();
        if (np == 0 || left <= 0) return -1;
        if (poll(p, (nfds_t)np, (int)(left / 1000) + 1) <= 0) continue;
        for (int j = 0; j < np; j++) {
            if (!(p[j].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            int i = idx[j];
            int rc = lrRead(g->r[i], line, sizeof line, 0);
            if (rc == -1) { close(g->fd[i]); g->fd[i] = -1; continue; }
            if (rc < 0) continue;
            g->pending[i]--;
            unsigned long t;
            if (sscanf(line, "OK %lu", &t) == 1 && t == tag) { got++; if (onReply) onReply(line, ctx); }
        }
    }
    return 0;
}

static struct { long writes, wfail, reads, rfail, stale, staleBy; int64_t *wl, *rl; } q;

static void *qWriter(void *arg)
{
    (void)arg;
    Group g;
    groupOpen(&g, getenv("WRITER_IP"));
    char req[128];
    for (uint64_t v = 1; !qStop; v++) {
        snprintf(req, sizeof req, "VPUT %s v%lu %lu\n", qKey, (unsigned long)v, (unsigned long)v);
        int64_t s = nowUs();
        if (groupCall(&g, ~0u, req, v, qW, NULL, NULL) == 0) {
            q.wl[q.writes++] = nowUs() - s;
            atomic_store(&lastAcked, v);
        } else q.wfail++;
    }
    return NULL;
}

static void readReply(const char *line, void *ctx)
{
    unsigned long tag, ver;
    if (sscanf(line, "OK %lu %lu", &tag, &ver) == 2 && ver > *(uint64_t *)ctx) *(uint64_t *)ctx = ver;
}

static void *qReader(void *arg)
{
    (void)arg;
    Group g;
    groupOpen(&g, NULL);
    char req[128];
    unsigned seed = 7;
    for (uint64_t t = 1; !qStop; t++) {
        unsigned mask = 0;                             /* 随机挑 R 个节点 */
        while (__builtin_popcount(mask) < qR) mask |= 1u << (rand_r(&seed) % (unsigned)qN);
        uint64_t before = atomic_load(&lastAcked), best = 0;
        snprintf(req, sizeof req, "VGET %s %lu\n", qKey, (unsigned long)t);
        int64_t s = nowUs();
        if (groupCall(&g, mask, req, t, qR, readReply, &best) == 0) {
            q.rl[q.reads++] = nowUs() - s;
            if (best < before) { q.stale++; q.staleBy += (long)(before - best); }
        } else q.rfail++;
    }
    return NULL;
}

static int cmdQuorum(int argc, char **argv)
{
    if (argc < 6) return 2;
    snprintf(qKey, sizeof qKey, "q%d", (int)getpid());
    qW = atoi(argv[2]);
    qR = atoi(argv[3]);
    double secs = atof(argv[4]);
    for (int i = 5; i < argc && qN < MAXN; i++) qAddr[qN++] = argv[i];
    q.wl = malloc(sizeof(int64_t) << 22);
    q.rl = malloc(sizeof(int64_t) << 22);
    pthread_t a, b;
    pthread_create(&a, NULL, qWriter, NULL);
    pthread_create(&b, NULL, qReader, NULL);
    sleepMs((int)(secs * 1000));
    qStop = 1;
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("N=%d W=%d R=%d\n", qN, qW, qR);
    printLat("write", q.wl, q.writes);
    printLat("read", q.rl, q.reads);
    printf("writes %ld (failed %ld), reads %ld (failed %ld), stale reads %ld = %.2f%%, avg versions behind %.2f\n",
           q.writes, q.wfail, q.reads, q.rfail, q.stale, q.reads ? 100.0 * (double)q.stale / (double)q.reads : 0,
           q.stale ? (double)q.staleBy / (double)q.stale : 0);
    return 0;
}

/* ---------- lww：时钟偏差让后来的写入丢失 ---------- */

static int cmdLww(int argc, char **argv)
{
    if (argc < 6) return 2;
    long rounds = atol(argv[2]);
    int64_t offset = atol(argv[3]);
    int lamport = !strcmp(argv[4], "lamport");
    for (int i = 5; i < argc && qN < MAXN; i++) qAddr[qN++] = argv[i];
    Group g;
    groupOpen(&g, NULL);
    char req[128];
    uint64_t lA = 0, lB = 0;                           /* 两个客户端各自的逻辑时钟 */
    long lost = 0;
    uint64_t tag = 1;
    char key[32];
    snprintf(key, sizeof key, "x%d", (int)getpid());   /* 每次运行用一个新键 */
    srand(42);
    for (long i = 1; i <= rounds; i++) {
        /* A 写入 */
        uint64_t tsA = lamport ? ((++lA) << 8 | 1) : ((uint64_t)wallUs() << 8 | 1);
        snprintf(req, sizeof req, "VPUT %s a%ld %lu\n", key, i, (unsigned long)tsA);
        if (groupCall(&g, ~0u, req, tsA, qN, NULL, NULL) < 0) return 1;
        /* A 写完后通知 B（这条消息就是因果关系），B 隔 0~20 ms 再写 */
        if (lamport && (tsA >> 8) > lB) lB = tsA >> 8;
        sleepMs(rand() % 21);
        uint64_t tsB = lamport ? ((++lB) << 8 | 2) : ((uint64_t)(wallUs() - offset) << 8 | 2);
        if (lamport && lB > lA) lA = lB;               /* 下一轮 A 也会先收到 B 的消息 */
        snprintf(req, sizeof req, "VPUT %s b%ld %lu\n", key, i, (unsigned long)tsB);
        if (groupCall(&g, ~0u, req, tsB, qN, NULL, NULL) < 0) return 1;
        /* 读回来：应该是 B 写的值 */
        snprintf(req, sizeof req, "VGET %s %lu\n", key, (unsigned long)++tag);
        uint64_t best = 0;
        groupCall(&g, ~0u, req, tag, qN, readReply, &best);
        if (best != tsB) lost++;
    }
    printf("rounds %ld, B's clock behind by %.1f ms, mode %s: B's acked write lost %ld times (%.1f%%)\n",
           rounds, (double)offset / 1000.0, lamport ? "lamport" : "wall", lost, 100.0 * (double)lost / (double)rounds);
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    int rc = 2;
    if (argc >= 2) {
        if (!strcmp(argv[1], "lat")) rc = cmdLat(argc, argv);
        else if (!strcmp(argv[1], "incr")) rc = cmdIncr(argc, argv);
        else if (!strcmp(argv[1], "seq")) rc = cmdSeq(argc, argv);
        else if (!strcmp(argv[1], "check")) rc = cmdCheck(argc, argv);
        else if (!strcmp(argv[1], "diff")) rc = cmdDiff(argc, argv);
        else if (!strcmp(argv[1], "quorum")) rc = cmdQuorum(argc, argv);
        else if (!strcmp(argv[1], "lww")) rc = cmdLww(argc, argv);
    }
    if (rc == 2) fprintf(stderr, "usage: see the comment at the top of cli.c\n");
    return rc;
}
