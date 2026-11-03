#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "route.h"

static void* xmalloc(size_t size)
{
    void* p = malloc(size ? size : 1);
    if (p == NULL) exit(1);
    return p;
}

// 用前驱数组还原路线：prev[v] 是 v 的上一站，via[v] 是从上一站到 v 坐的线路
static void buildRoute(int from, int to, const int* prev, const int* via, Route* r)
{
    int n = 1;
    for (int v = to; v != from; v = prev[v]) ++n;
    r->stations = xmalloc(sizeof(int) * n);
    r->lines = xmalloc(sizeof(int) * n);
    r->n = n;
    r->lines[0] = -1;
    for (int v = to, i = n - 1; i >= 0; v = prev[v], --i) {
        r->stations[i] = v;
        if (i > 0) r->lines[i] = via[v];
    }
}

/* ---------- 里程碑 2：站数最少（BFS） ---------- */

int routeFewestStops(const Metro* m, int from, int to, Route* r)
{
    int n = m->nstations;
    int* prev = xmalloc(sizeof(int) * n);
    int* via = xmalloc(sizeof(int) * n);
    int* queue = xmalloc(sizeof(int) * n);      // 每个车站最多入队一次，不需要循环队列
    for (int i = 0; i < n; ++i) prev[i] = -1;
    prev[from] = from;                          // 前驱不是 -1 就表示已经发现了
    int head = 0, tail = 0;
    queue[tail++] = from;
    while (head < tail && prev[to] < 0) {
        int u = queue[head++];
        const Station* st = &m->stations[u];
        for (int i = 0; i < st->nedges; ++i) {
            const Edge* e = &st->edges[i];
            if (prev[e->to] >= 0) continue;
            prev[e->to] = u;
            via[e->to] = e->line;
            queue[tail++] = e->to;
        }
    }
    int ret = -1;
    if (prev[to] >= 0) { buildRoute(from, to, prev, via, r); ret = 0; }
    free(prev);
    free(via);
    free(queue);
    return ret;
}

// 里程碑 3：在「车站 + 线路」组成的状态图上做 Dijkstra，换乘一次加 TRANSFER_MINUTES 分钟。
int routeFastest(const Metro* m, int from, int to, Route* r)
{
    // TODO
    (void)m; (void)from; (void)to; (void)r;
    return -2;
}

// 里程碑 3：和 routeFastest 用同一个 Dijkstra，只是不用还原路线。
int routeAllTimes(const Metro* m, int from, int* minutes)
{
    // TODO
    (void)m; (void)from; (void)minutes;
    return -2;
}

// 里程碑 4：同一张状态图，权重换成（换乘次数，用时）两个数，先比较换乘次数，再比较用时。
int routeFewestTransfers(const Metro* m, int from, int to, Route* r)
{
    // TODO
    (void)m; (void)from; (void)to; (void)r;
    return -2;
}

void routeFree(Route* r)
{
    free(r->stations);
    free(r->lines);
    r->stations = r->lines = NULL;
    r->n = 0;
}
