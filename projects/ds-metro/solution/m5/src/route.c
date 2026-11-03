#include <limits.h>
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

/* ---------- 状态图：每个（车站，线路）是一个顶点 ---------- */

typedef struct {
    int* first;                                 // 车站 s 的状态是 first[s] .. first[s+1]-1
    int* station;                               // station[v]：状态 v 在哪个车站
    int* line;                                  // line[v]：状态 v 在哪条线路上
    int n;                                      // 状态的个数
} States;

static void statesBuild(States* g, const Metro* m)
{
    int total = 0;                              // 状态最多和邻接表中的边一样多
    for (int s = 0; s < m->nstations; ++s) total += m->stations[s].nedges;
    g->first = xmalloc(sizeof(int) * (m->nstations + 1));
    g->station = xmalloc(sizeof(int) * total);
    g->line = xmalloc(sizeof(int) * total);
    g->n = 0;
    for (int s = 0; s < m->nstations; ++s) {
        g->first[s] = g->n;
        const Station* st = &m->stations[s];
        for (int i = 0; i < st->nedges; ++i) {  // 同一条线路的边在邻接表中是挨着的
            if (i > 0 && st->edges[i].line == st->edges[i - 1].line) continue;
            g->station[g->n] = s;
            g->line[g->n] = st->edges[i].line;
            ++g->n;
        }
    }
    g->first[m->nstations] = g->n;
}

static void statesFree(States* g)
{
    free(g->first);
    free(g->station);
    free(g->line);
}

static int stateOf(const States* g, int s, int line)
{
    for (int v = g->first[s]; v < g->first[s + 1]; ++v) {
        if (g->line[v] == line) return v;
    }
    return -1;                                  // 不会发生：边的两端都在这条线路上
}

/* ---------- 里程碑 3、4：Dijkstra ---------- */

// 到一个状态的代价。按时间找时只比较 minutes；按换乘找时先比较 transfers，再比较 minutes
typedef struct {
    int transfers;
    int minutes;
} Cost;

enum { BY_TIME, BY_TRANSFERS };

static int costLess(Cost a, Cost b, int mode)
{
    if (mode == BY_TRANSFERS && a.transfers != b.transfers) return a.transfers < b.transfers;
    return a.minutes < b.minutes;
}

// 二叉堆（最小堆），元素是（代价，状态）。同一个状态可能入堆多次，出堆时跳过过时的
typedef struct {
    Cost cost;
    int v;
} HeapItem;

typedef struct {
    HeapItem* a;
    int n, cap;
    int mode;
} Heap;

static void heapPush(Heap* h, Cost c, int v)
{
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 16;
        HeapItem* p = realloc(h->a, sizeof(HeapItem) * h->cap);
        if (p == NULL) exit(1);
        h->a = p;
    }
    int i = h->n++;
    while (i > 0) {                             // 上浮
        int parent = (i - 1) / 2;
        if (!costLess(c, h->a[parent].cost, h->mode)) break;
        h->a[i] = h->a[parent];
        i = parent;
    }
    h->a[i] = (HeapItem){c, v};
}

static HeapItem heapPop(Heap* h)
{
    HeapItem top = h->a[0], last = h->a[--h->n];
    int i = 0;
    for (;;) {                                  // 下沉
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && costLess(h->a[c + 1].cost, h->a[c].cost, h->mode)) ++c;
        if (!costLess(h->a[c].cost, last.cost, h->mode)) break;
        h->a[i] = h->a[c];
        i = c;
    }
    if (h->n > 0) h->a[i] = last;
    return top;
}

// 从车站 from 的所有状态出发（刚上车不算换乘），算出到每个状态的最小代价 dist 和前驱 prev。
// to >= 0 时，第一次从堆中取出车站 to 的状态就停下，返回这个状态；否则算完所有状态，返回 -1。
static int dijkstra(const Metro* m, const States* g, int from, int to, int mode, Cost* dist, int* prev)
{
    int* done = calloc(g->n ? g->n : 1, sizeof(int));
    if (done == NULL) exit(1);
    for (int v = 0; v < g->n; ++v) { dist[v] = (Cost){INT_MAX, INT_MAX}; prev[v] = -1; }
    Heap h = {NULL, 0, 0, mode};
    for (int v = g->first[from]; v < g->first[from + 1]; ++v) {
        dist[v] = (Cost){0, 0};
        heapPush(&h, dist[v], v);
    }
    int found = -1;
    while (h.n > 0) {
        HeapItem it = heapPop(&h);
        int v = it.v;
        if (done[v]) continue;                  // 过时的元素
        done[v] = 1;
        int s = g->station[v];
        if (s == to) { found = v; break; }
        Cost c = dist[v];
        const Station* st = &m->stations[s];
        for (int i = 0; i < st->nedges; ++i) {  // 坐一站：沿着当前线路的边
            const Edge* e = &st->edges[i];
            if (e->line != g->line[v]) continue;
            int w = stateOf(g, e->to, e->line);
            Cost nc = {c.transfers, c.minutes + e->minutes};
            if (!done[w] && costLess(nc, dist[w], mode)) { dist[w] = nc; prev[w] = v; heapPush(&h, nc, w); }
        }
        for (int w = g->first[s]; w < g->first[s + 1]; ++w) {   // 换乘：同一个车站的其他线路
            if (w == v) continue;
            Cost nc = {c.transfers + 1, c.minutes + TRANSFER_MINUTES};
            if (!done[w] && costLess(nc, dist[w], mode)) { dist[w] = nc; prev[w] = v; heapPush(&h, nc, w); }
        }
    }
    free(h.a);
    free(done);
    return found;
}

// 沿着状态的前驱还原路线。换乘那一步车站不变，不算一站
static void buildStateRoute(const States* g, int target, const int* prev, Route* r)
{
    int n = 1;
    for (int v = target; prev[v] >= 0; v = prev[v]) {
        if (g->station[prev[v]] != g->station[v]) ++n;
    }
    r->stations = xmalloc(sizeof(int) * n);
    r->lines = xmalloc(sizeof(int) * n);
    r->n = n;
    r->lines[0] = -1;
    int i = n - 1, v = target;
    r->stations[i] = g->station[v];
    for (; prev[v] >= 0; v = prev[v]) {
        int u = prev[v];
        if (g->station[u] == g->station[v]) continue;
        r->lines[i] = g->line[v];
        r->stations[--i] = g->station[u];
    }
}

static int shortest(const Metro* m, int from, int to, int mode, Route* r)
{
    States g;
    statesBuild(&g, m);
    Cost* dist = xmalloc(sizeof(Cost) * g.n);
    int* prev = xmalloc(sizeof(int) * g.n);
    int target = dijkstra(m, &g, from, to, mode, dist, prev);
    if (target >= 0) buildStateRoute(&g, target, prev, r);
    free(dist);
    free(prev);
    statesFree(&g);
    return target >= 0 ? 0 : -1;
}

int routeFastest(const Metro* m, int from, int to, Route* r)
{
    return shortest(m, from, to, BY_TIME, r);
}

int routeFewestTransfers(const Metro* m, int from, int to, Route* r)
{
    return shortest(m, from, to, BY_TRANSFERS, r);
}

int routeAllTimes(const Metro* m, int from, int* minutes)
{
    States g;
    statesBuild(&g, m);
    Cost* dist = xmalloc(sizeof(Cost) * g.n);
    int* prev = xmalloc(sizeof(int) * g.n);
    dijkstra(m, &g, from, -1, BY_TIME, dist, prev);
    for (int s = 0; s < m->nstations; ++s) {    // 一个车站有几个状态，取其中最小的
        minutes[s] = -1;
        for (int v = g.first[s]; v < g.first[s + 1]; ++v) {
            if (dist[v].minutes != INT_MAX && (minutes[s] < 0 || dist[v].minutes < minutes[s])) minutes[s] = dist[v].minutes;
        }
    }
    minutes[from] = 0;
    free(dist);
    free(prev);
    statesFree(&g);
    return 0;
}

void routeFree(Route* r)
{
    free(r->stations);
    free(r->lines);
    r->stations = r->lines = NULL;
    r->n = 0;
}
