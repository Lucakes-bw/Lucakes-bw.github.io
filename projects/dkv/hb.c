/* hb：心跳故障检测器
 *
 *   hb ADDR INTERVAL_MS TIMEOUT_MS SECONDS
 *
 * 每 INTERVAL_MS 向 ADDR（kvd 的 UDP 端口）发一个心跳包；超过 TIMEOUT_MS 没有收到任何回应，
 * 就认为对方「挂了」（怀疑）；之后又收到回应，说明刚才是误判。
 * 每次怀疑和恢复都打印墙上时间，方便和杀进程的时刻对照。
 */
#define _GNU_SOURCE
#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static int fd;
static struct sockaddr_in peer;
static _Atomic int64_t lastPong;
static _Atomic long pongs;
static int64_t sentAt[1 << 20];
static int64_t rtt[1 << 20];
static long nrtt;

static void *receiver(void *arg)
{
    (void)arg;
    char b[32];
    for (;;) {
        ssize_t n = recv(fd, b, sizeof b - 1, 0);
        if (n <= 0) continue;
        b[n] = 0;
        long seq = atol(b);
        int64_t now = nowUs();
        atomic_store(&lastPong, now);
        atomic_fetch_add(&pongs, 1);
        if (seq >= 0 && seq < (1 << 20) && nrtt < (1 << 20)) rtt[nrtt++] = now - sentAt[seq];
    }
    return NULL;
}

static int cmp(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: hb ADDR INTERVAL_MS TIMEOUT_MS SECONDS\n"); return 2; }
    char host[64];
    int port;
    parseAddr(argv[1], host, &port);
    int interval = atoi(argv[2]), timeout = atoi(argv[3]);
    double secs = atof(argv[4]);
    setvbuf(stdout, NULL, _IOLBF, 0);
    fd = udpSocket(0);
    peer.sin_family = AF_INET;
    peer.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &peer.sin_addr);

    pthread_t t;
    pthread_create(&t, NULL, receiver, NULL);
    int64_t start = nowUs(), end = start + (int64_t)(secs * 1e6), nextSend = start;
    atomic_store(&lastPong, start);
    long seq = 0, suspicions = 0, falseAlarms = 0;
    int suspected = 0;
    int64_t suspectSince = 0, suspectedTotal = 0;
    while (nowUs() < end) {
        int64_t now = nowUs();
        if (now >= nextSend) {
            char b[32];
            int n = snprintf(b, sizeof b, "%ld", seq);
            if (seq < (1 << 20)) sentAt[seq] = now;
            seq++;
            sendto(fd, b, (size_t)n, 0, (struct sockaddr *)&peer, sizeof peer);
            nextSend += (int64_t)interval * 1000;
        }
        int64_t lp = atomic_load(&lastPong);
        if (!suspected && now - lp > (int64_t)timeout * 1000) {
            suspected = 1;
            suspicions++;
            suspectSince = now;
            printf("%.3f SUSPECT: no reply for %d ms (last reply at %.3f)\n", wallUs() / 1e6, timeout,
                   (wallUs() - (now - lp)) / 1e6);
        } else if (suspected && lp > suspectSince) {
            suspected = 0;
            falseAlarms++;
            suspectedTotal += now - suspectSince;
            printf("%.3f ALIVE again: it was a false alarm, suspected for %.0f ms\n", wallUs() / 1e6, (now - suspectSince) / 1000.0);
        }
        sleepMs(1);
    }
    if (suspected) suspectedTotal += nowUs() - suspectSince;
    long got = atomic_load(&pongs);
    qsort(rtt, (size_t)nrtt, sizeof *rtt, cmp);
    printf("sent %ld, replies %ld (%.2f%% lost), suspicions %ld, false alarms %ld, still suspected %d, suspected time %.0f ms\n",
           seq, got, seq ? 100.0 * (double)(seq - got) / (double)seq : 0, suspicions, falseAlarms, suspected,
           suspectedTotal / 1000.0);
    if (nrtt)
        printf("rtt p50 %.3f p99 %.3f max %.3f ms\n", rtt[nrtt / 2] / 1000.0, rtt[(long)((double)(nrtt - 1) * 0.99)] / 1000.0,
               rtt[nrtt - 1] / 1000.0);
    return 0;
}
