/* kvd：dkv 的节点进程。一个内存中的键值表，加上主从复制。
 *
 * 客户端命令（文本协议，一行一条）：
 *   GET k                 -> OK v | NF
 *   PUT k v               -> OK | ERR ...          只有主节点接受
 *   INCR k [cid seq]      -> OK n | ERR ...        带上 cid、seq 且开了 -dedup 时，重复的请求直接返回上次的结果
 *   VPUT k v ver          -> OK ver                版本号更大才覆盖（法定人数和时钟实验用，不走复制）
 *   VGET k tag            -> OK tag ver v
 *   DUMP                  -> 每行 "k v"，最后 END
 *   STAT                  -> OK role=... epoch=... log=... keys=...
 *   PROMOTE               -> 自己变成主节点，任期 +1
 * 节点之间：
 *   HELLO epoch           -> LAST n | STALE e
 *   REPL epoch idx k v    -> ACK idx | NEED n | STALE e
 * 同一个端口的 UDP 上回显心跳包。
 */
#define _GNU_SOURCE
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { BACKUP, PRIMARY };
#define CAP (1 << 20)            /* 最多装 CAP*3/4 个键 */
#define MAXPEER 8

typedef struct { char key[32]; char val[64]; uint64_t ver; int used; } Slot;
typedef struct { char key[32]; char val[64]; uint64_t epoch; } Entry;   /* 复制日志中的一条：PUT key val */

static Slot *tab;
static long nkeys;
static Entry *logv;                   /* logv[i-1] 是第 i 条 */
static uint64_t logLen, logCap;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv;             /* 新日志、新的确认、角色变化 */
static int role = BACKUP;
static uint64_t epoch = 1;

static char *peers[MAXPEER];
static uint64_t match[MAXPEER];       /* 每个从节点确认到第几条 */
static int npeer;

static int port = 7000, acksNeeded = 0, fence = 0, dedup = 0, autoMs = 0, ackTimeoutMs = 2000;
static char watchAddr[64];
static char myId[16] = "n?";

/* 去重表：每个客户端最后一个请求的序号和结果 */
#define NCLI 4096
static struct { uint32_t cid; uint64_t seq; long result; } dd[NCLI];

static struct { long puts, incrs, dupHits, replApplied, ackTimeouts, stepDowns, promotions; } st;

static void logmsg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logmsg(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%.3f %s %s\n", wallUs() / 1e6, myId, buf);
}

static uint32_t hashStr(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static Slot *lookup(const char *k, int create)
{
    for (uint32_t i = hashStr(k) & (CAP - 1);; i = (i + 1) & (CAP - 1)) {
        Slot *s = &tab[i];
        if (!s->used) {
            if (!create || nkeys >= CAP * 3 / 4) return NULL;
            s->used = 1;
            snprintf(s->key, sizeof s->key, "%s", k);
            nkeys++;
            return s;
        }
        if (strcmp(s->key, k) == 0) return s;
    }
}

static void setKey(const char *k, const char *v)
{
    Slot *s = lookup(k, 1);
    if (!s) { fprintf(stderr, "table full\n"); exit(1); }   /* 实验程序：满了直接退出，免得悄悄丢数据 */
    snprintf(s->val, sizeof s->val, "%s", v);
}

static uint64_t appendLog(const char *k, const char *v)
{
    if (logLen == logCap) {
        logCap = logCap ? logCap * 2 : 1 << 16;
        logv = realloc(logv, logCap * sizeof *logv);
    }
    Entry *e = &logv[logLen++];
    snprintf(e->key, sizeof e->key, "%s", k);
    snprintf(e->val, sizeof e->val, "%s", v);
    e->epoch = epoch;
    pthread_cond_broadcast(&cv);
    return logLen;
}

/* 持锁调用：等到足够多的从节点确认了第 idx 条。异步复制（acksNeeded == 0）直接返回 */
static int waitCommit(uint64_t idx)
{
    if (acksNeeded == 0) return 0;
    struct timespec dl;
    clock_gettime(CLOCK_MONOTONIC, &dl);
    dl.tv_sec += ackTimeoutMs / 1000;
    dl.tv_nsec += (long)(ackTimeoutMs % 1000) * 1000000;
    if (dl.tv_nsec >= 1000000000) { dl.tv_sec++; dl.tv_nsec -= 1000000000; }
    for (;;) {
        if (role != PRIMARY) return -2;
        int n = 0;
        for (int i = 0; i < npeer; i++) n += match[i] >= idx;
        if (n >= acksNeeded) return 0;
        if (pthread_cond_timedwait(&cv, &mu, &dl) == ETIMEDOUT) { st.ackTimeouts++; return -1; }
    }
}

static const char *commitErr(int r) { return r == -2 ? "ERR notprimary\n" : "ERR timeout\n"; }

/* 从节点收到更高的任期：跟随它 */
static void adoptEpoch(uint64_t e)
{
    if (e <= epoch) return;
    epoch = e;
    if (role == PRIMARY) { role = BACKUP; st.stepDowns++; logmsg("step down, epoch %lu", (unsigned long)e); }
    pthread_cond_broadcast(&cv);
}

/* ---------- 复制：主节点对每个从节点一个线程 ---------- */

static void *replicator(void *arg)
{
    int p = (int)(long)arg;
    char line[256];
    for (;;) {
        pthread_mutex_lock(&mu);
        while (role != PRIMARY) pthread_cond_wait(&cv, &mu);
        uint64_t myEpoch = epoch;
        pthread_mutex_unlock(&mu);

        int fd = tcpConnect(peers[p], 500);
        if (fd < 0) { sleepMs(100); continue; }
        LineReader r;
        lrInit(&r, fd);
        snprintf(line, sizeof line, "HELLO %lu\n", (unsigned long)myEpoch);
        uint64_t next = 0, e = 0, n = 0;
        if (writeAll(fd, line, strlen(line)) < 0 || lrRead(&r, line, sizeof line, 1000) < 0) goto drop;
        if (sscanf(line, "STALE %lu", &e) == 1) goto stale;
        if (sscanf(line, "LAST %lu", &n) != 1) goto drop;
        pthread_mutex_lock(&mu);
        next = (n < logLen ? n : logLen) + 1;
        pthread_mutex_unlock(&mu);

        for (;;) {
            static __thread char out[256 * 160];
            size_t len = 0;
            uint64_t first = next, last;
            pthread_mutex_lock(&mu);
            if (role != PRIMARY || epoch != myEpoch) { pthread_mutex_unlock(&mu); break; }
            if (logLen < next) {
                struct timespec dl;
                clock_gettime(CLOCK_MONOTONIC, &dl);
                dl.tv_nsec += 100000000;
                if (dl.tv_nsec >= 1000000000) { dl.tv_sec++; dl.tv_nsec -= 1000000000; }
                pthread_cond_timedwait(&cv, &mu, &dl);
                pthread_mutex_unlock(&mu);
                continue;
            }
            last = logLen < next + 255 ? logLen : next + 255;      /* 一批最多 256 条 */
            for (uint64_t i = first; i <= last; i++) {
                Entry *en = &logv[i - 1];
                len += (size_t)snprintf(out + len, sizeof out - len, "REPL %lu %lu %s %s\n",
                                        (unsigned long)myEpoch, (unsigned long)i, en->key, en->val);
            }
            pthread_mutex_unlock(&mu);
            if (writeAll(fd, out, len) < 0) goto drop;
            for (uint64_t i = first; i <= last; i++) {
                if (lrRead(&r, line, sizeof line, 1000) < 0) goto drop;
                uint64_t a;
                if (sscanf(line, "ACK %lu", &a) == 1) {
                    pthread_mutex_lock(&mu);
                    if (a > match[p]) match[p] = a;
                    pthread_cond_broadcast(&cv);
                    pthread_mutex_unlock(&mu);
                    next = a + 1;
                } else if (sscanf(line, "NEED %lu", &a) == 1) {
                    next = a;
                    goto drop;                              /* 重新连，从 a 开始发 */
                } else if (sscanf(line, "STALE %lu", &e) == 1) {
                    goto stale;
                } else goto drop;
            }
        }
        close(fd);
        continue;
    stale:
        pthread_mutex_lock(&mu);
        if (fence) adoptEpoch(e);
        pthread_mutex_unlock(&mu);
    drop:
        close(fd);
        sleepMs(50);
    }
    return NULL;
}

/* ---------- 自动故障转移：盯着主节点，太久没有心跳回应就自己当主节点 ---------- */

static void *watcher(void *arg)
{
    (void)arg;
    char host[64];
    int wport;
    parseAddr(watchAddr, host, &wport);
    int fd = udpSocket(0);
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons((uint16_t)wport) };
    inet_pton(AF_INET, host, &sa.sin_addr);
    struct timeval tv = { 0, 20000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int64_t lastPong = nowUs();
    for (;;) {
        sendto(fd, "ping", 4, 0, (struct sockaddr *)&sa, sizeof sa);
        char b[16];
        int64_t until = nowUs() + 20000;
        while (nowUs() < until)
            if (recv(fd, b, sizeof b, 0) > 0) lastPong = nowUs();
        if (nowUs() - lastPong > (int64_t)autoMs * 1000) {
            pthread_mutex_lock(&mu);
            if (role == BACKUP) {
                epoch++;
                role = PRIMARY;
                st.promotions++;
                logmsg("primary silent for %d ms: promote myself, epoch %lu", autoMs, (unsigned long)epoch);
                pthread_cond_broadcast(&cv);
            }
            pthread_mutex_unlock(&mu);
            return NULL;
        }
    }
}

static void *udpEcho(void *arg)
{
    (void)arg;
    int fd = udpSocket(port);
    char b[64];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, b, sizeof b, 0, (struct sockaddr *)&from, &fl);
        if (n > 0) sendto(fd, b, (size_t)n, 0, (struct sockaddr *)&from, fl);
    }
    return NULL;
}

/* ---------- 客户端连接 ---------- */

static void *serve(void *arg)
{
    int fd = (int)(long)arg;
    LineReader *r = malloc(sizeof *r);
    lrInit(r, fd);
    char line[512], out[256], k[32], v[64];
    for (;;) {
        if (lrRead(r, line, sizeof line, -1) < 0) break;
        unsigned long a, b;
        out[0] = 0;
        pthread_mutex_lock(&mu);
        if (sscanf(line, "GET %31s", k) == 1) {
            Slot *s = lookup(k, 0);
            snprintf(out, sizeof out, s ? "OK %s\n" : "NF\n", s ? s->val : "");
        } else if (sscanf(line, "PUT %31s %63s", k, v) == 2) {
            if (role != PRIMARY) snprintf(out, sizeof out, "ERR notprimary\n");
            else {
                setKey(k, v);
                st.puts++;
                int rc = waitCommit(appendLog(k, v));
                snprintf(out, sizeof out, "%s", rc == 0 ? "OK\n" : commitErr(rc));
            }
        } else if (strncmp(line, "INCR ", 5) == 0) {
            unsigned long cid = 0, seq = 0;
            int n = sscanf(line, "INCR %31s %lu %lu", k, &cid, &seq);
            if (role != PRIMARY) snprintf(out, sizeof out, "ERR notprimary\n");
            else if (dedup && n == 3 && dd[cid % NCLI].cid == cid && seq <= dd[cid % NCLI].seq) {
                st.dupHits++;                                  /* 重试：这个请求已经执行过 */
                snprintf(out, sizeof out, "OK %ld\n", dd[cid % NCLI].result);
            } else {
                Slot *s = lookup(k, 1);
                long val = s ? atol(s->val) + 1 : 1;
                snprintf(v, sizeof v, "%ld", val);
                setKey(k, v);
                st.incrs++;
                if (dedup && n == 3) { dd[cid % NCLI].cid = (uint32_t)cid; dd[cid % NCLI].seq = seq; dd[cid % NCLI].result = val; }
                int rc = waitCommit(appendLog(k, v));         /* 日志里记的是结果 PUT k n，重放不会重复加 */
                if (rc) snprintf(out, sizeof out, "%s", commitErr(rc)); else snprintf(out, sizeof out, "OK %ld\n", val);
            }
        } else if (sscanf(line, "VPUT %31s %63s %lu", k, v, &a) == 3) {
            Slot *s = lookup(k, 1);
            if (s && (a > s->ver || (a == s->ver && strcmp(v, s->val) > 0))) {
                s->ver = a;
                snprintf(s->val, sizeof s->val, "%s", v);
            }
            snprintf(out, sizeof out, "OK %lu\n", a);
        } else if (sscanf(line, "VGET %31s %lu", k, &a) == 2) {
            Slot *s = lookup(k, 0);
            snprintf(out, sizeof out, "OK %lu %lu %s\n", a, s ? (unsigned long)s->ver : 0UL, s ? s->val : "-");
        } else if (sscanf(line, "REPL %lu %lu %31s %63s", &a, &b, k, v) == 4) {
            if (fence && a < epoch) snprintf(out, sizeof out, "STALE %lu\n", (unsigned long)epoch);
            else if (fence && a == epoch && role == PRIMARY) snprintf(out, sizeof out, "STALE %lu\n", (unsigned long)epoch);
            else if (b > logLen + 1) snprintf(out, sizeof out, "NEED %lu\n", (unsigned long)logLen + 1);
            else {
                adoptEpoch(a);
                logLen = b - 1;                                /* 截断到 b-1，再追加（状态表不回滚！） */
                appendLog(k, v);
                logv[logLen - 1].epoch = a;
                setKey(k, v);
                st.replApplied++;
                snprintf(out, sizeof out, "ACK %lu\n", b);
            }
        } else if (sscanf(line, "HELLO %lu", &a) == 1) {
            if (fence && (a < epoch || (a == epoch && role == PRIMARY))) snprintf(out, sizeof out, "STALE %lu\n", (unsigned long)epoch);
            else {
                adoptEpoch(a);
                snprintf(out, sizeof out, "LAST %lu\n", (unsigned long)logLen);
            }
        } else if (strcmp(line, "PROMOTE") == 0) {
            epoch++;
            role = PRIMARY;
            st.promotions++;
            logmsg("promoted by command, epoch %lu", (unsigned long)epoch);
            pthread_cond_broadcast(&cv);
            snprintf(out, sizeof out, "OK %lu\n", (unsigned long)epoch);
        } else if (strcmp(line, "STAT") == 0) {
            uint64_t mn = 0;
            for (int i = 0; i < npeer; i++) mn = (i == 0 || match[i] < mn) ? match[i] : mn;
            snprintf(out, sizeof out, "OK id=%s role=%s epoch=%lu log=%lu keys=%ld puts=%ld incrs=%ld dup=%ld repl=%ld acktimeout=%ld stepdown=%ld promote=%ld minmatch=%lu\n",
                     myId, role == PRIMARY ? "primary" : "backup", (unsigned long)epoch, (unsigned long)logLen, nkeys,
                     st.puts, st.incrs, st.dupHits, st.replApplied, st.ackTimeouts, st.stepDowns, st.promotions, (unsigned long)mn);
        } else if (strcmp(line, "DUMP") == 0) {
            size_t cap = (size_t)nkeys * 100 + 16, len = 0;
            char *big = malloc(cap);
            for (long i = 0; i < CAP; i++)
                if (tab[i].used) len += (size_t)snprintf(big + len, cap - len, "%s %s\n", tab[i].key, tab[i].val);
            len += (size_t)snprintf(big + len, cap - len, "END\n");
            pthread_mutex_unlock(&mu);
            writeAll(fd, big, len);
            free(big);
            continue;
        } else snprintf(out, sizeof out, "ERR bad command\n");
        pthread_mutex_unlock(&mu);
        if (writeAll(fd, out, strlen(out)) < 0) break;
    }
    close(fd);
    free(r);
    return NULL;
}

int main(int argc, char **argv)
{
    char peerList[512] = "";
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *o = argv[i], *v = argv[i + 1];
        if (!strcmp(o, "-port")) port = atoi(v);
        else if (!strcmp(o, "-id")) snprintf(myId, sizeof myId, "%s", v);
        else if (!strcmp(o, "-role")) role = !strcmp(v, "primary") ? PRIMARY : BACKUP;
        else if (!strcmp(o, "-peers")) snprintf(peerList, sizeof peerList, "%s", v);
        else if (!strcmp(o, "-acks")) acksNeeded = atoi(v);
        else if (!strcmp(o, "-fence")) fence = atoi(v);
        else if (!strcmp(o, "-dedup")) dedup = atoi(v);
        else if (!strcmp(o, "-auto")) autoMs = atoi(v);
        else if (!strcmp(o, "-watch")) snprintf(watchAddr, sizeof watchAddr, "%s", v);
        else if (!strcmp(o, "-acktimeout")) ackTimeoutMs = atoi(v);
        else { fprintf(stderr, "unknown option %s\n", o); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&cv, &ca);
    tab = calloc(CAP, sizeof *tab);

    pthread_t t;
    for (char *s = strtok(peerList, ","); s && npeer < MAXPEER; s = strtok(NULL, ",")) {
        peers[npeer] = strdup(s);
        pthread_create(&t, NULL, replicator, (void *)(long)npeer);
        npeer++;
    }
    pthread_create(&t, NULL, udpEcho, NULL);
    if (autoMs > 0 && watchAddr[0]) pthread_create(&t, NULL, watcher, NULL);

    int lfd = tcpListen(port);
    logmsg("listening on %d, role %s, %d peers, acks %d, fence %d, dedup %d",
           port, role == PRIMARY ? "primary" : "backup", npeer, acksNeeded, fence, dedup);
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        int on = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
        pthread_create(&t, NULL, serve, (void *)(long)fd);
        pthread_detach(t);
    }
}
