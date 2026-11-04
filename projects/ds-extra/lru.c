// LRU 缓存：哈希表 + 双向链表，get / put 都是 O(1)；和暴力实现对比；不同淘汰策略的命中率
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- LRU 缓存 ---------- */

typedef struct Entry {
    int key, value;
    struct Entry *prev, *next;              // 双向链表：越靠近头部，越是最近用过的
    struct Entry* hnext;                    // 哈希表同一个桶中的下一个
} Entry;

typedef struct {
    int capacity, size;
    Entry head, tail;                       // 两个哨兵：head.next 是最近用过的，tail.prev 是最久没用的
    Entry** buckets;
    int nbuckets;
} LRUCache;

static unsigned hashInt(int key) { return (unsigned)key * 2654435761u; }

static void lruInit(LRUCache* c, int capacity)
{
    c->capacity = capacity;
    c->size = 0;
    c->head.next = &c->tail;
    c->tail.prev = &c->head;
    c->nbuckets = 1;
    while (c->nbuckets < capacity * 2) c->nbuckets <<= 1;   // 容量固定，桶数一次分配好，不需要扩容
    c->buckets = calloc((size_t)c->nbuckets, sizeof(Entry*));
    if (!c->buckets) exit(1);
}

static Entry* hfind(LRUCache* c, int key)
{
    for (Entry* e = c->buckets[hashInt(key) & (unsigned)(c->nbuckets - 1)]; e; e = e->hnext)
        if (e->key == key) return e;
    return NULL;
}

static void hremove(LRUCache* c, Entry* target)
{
    Entry** link = &c->buckets[hashInt(target->key) & (unsigned)(c->nbuckets - 1)];
    while (*link != target) link = &(*link)->hnext;
    *link = target->hnext;
}

static void unlink(Entry* e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
}

static void pushFront(LRUCache* c, Entry* e)    // 放到链表头部：最近用过
{
    e->next = c->head.next;
    e->prev = &c->head;
    c->head.next->prev = e;
    c->head.next = e;
}

// 查找；找到时把它移到头部。返回是否命中
static bool lruGet(LRUCache* c, int key, int* value)
{
    Entry* e = hfind(c, key);
    if (!e) return false;
    unlink(e);
    pushFront(c, e);
    *value = e->value;
    return true;
}

static void lruPut(LRUCache* c, int key, int value)
{
    Entry* e = hfind(c, key);
    if (e) {                                // 已经在缓存中：更新值，移到头部
        e->value = value;
        unlink(e);
        pushFront(c, e);
        return;
    }
    if (c->size == c->capacity) {           // 满了：淘汰链表尾部，也就是最久没用过的
        Entry* old = c->tail.prev;
        unlink(old);
        hremove(c, old);
        free(old);
        --c->size;
    }
    e = malloc(sizeof(Entry));
    if (!e) exit(1);
    e->key = key;
    e->value = value;
    unsigned b = hashInt(key) & (unsigned)(c->nbuckets - 1);
    e->hnext = c->buckets[b];
    c->buckets[b] = e;
    pushFront(c, e);
    ++c->size;
}

static void lruFree(LRUCache* c)
{
    for (Entry* e = c->head.next; e != &c->tail;) {
        Entry* n = e->next;
        free(e);
        e = n;
    }
    free(c->buckets);
}

static void lruPrint(const LRUCache* c)
{
    printf("[");
    for (Entry* e = c->head.next; e != &c->tail; e = e->next) printf(e == c->head.next ? "%d" : " %d", e->key);
    printf("]（左边是最近用过的）\n");
}

/* ---------- 暴力实现：数组 + 时间戳，淘汰时扫描全部 ---------- */

typedef struct {
    int key[64], value[64], stamp[64], n, cap, clock;
} Brute;

static int bfind(Brute* b, int key)
{
    for (int i = 0; i < b->n; ++i)
        if (b->key[i] == key) return i;
    return -1;
}
static bool bruteGet(Brute* b, int key, int* value)
{
    int i = bfind(b, key);
    if (i < 0) return false;
    b->stamp[i] = ++b->clock;
    *value = b->value[i];
    return true;
}
static void brutePut(Brute* b, int key, int value)
{
    int i = bfind(b, key);
    if (i < 0) {
        if (b->n < b->cap) {
            i = b->n++;
        } else {
            i = 0;
            for (int k = 1; k < b->n; ++k)
                if (b->stamp[k] < b->stamp[i]) i = k;   // 时间戳最小的就是最久没用的
        }
        b->key[i] = key;
    }
    b->value[i] = value;
    b->stamp[i] = ++b->clock;
}

/* ---------- 命中率：LRU、FIFO、随机淘汰 ---------- */

// FIFO 和随机淘汰：用一个数组存缓存中的 key，用一个标记数组判断是否在缓存中
static double simulate(const int* trace, int len, int capacity, int universe, int policy)
{
    if (policy == 0) {                      // LRU
        LRUCache c;
        lruInit(&c, capacity);
        int hits = 0, v;
        for (int i = 0; i < len; ++i) {
            if (lruGet(&c, trace[i], &v)) ++hits;
            else lruPut(&c, trace[i], 0);
        }
        lruFree(&c);
        return (double)hits / len;
    }
    int* slots = malloc(sizeof(int) * (size_t)capacity);
    char* in = calloc((size_t)universe, 1);
    int size = 0, next = 0, hits = 0;
    for (int i = 0; i < len; ++i) {
        int k = trace[i];
        if (in[k]) {
            ++hits;
            continue;
        }
        if (size < capacity) {
            slots[size++] = k;
        } else {
            int victim = policy == 1 ? next : randInt(0, capacity - 1);   // FIFO 轮流淘汰最早进来的；随机淘汰任意一个
            next = (next + 1) % capacity;
            in[slots[victim]] = 0;
            slots[victim] = k;
        }
        in[k] = 1;
    }
    free(slots);
    free(in);
    return (double)hits / len;
}

int main(void)
{
    printf("== 容量为 3 的 LRU 缓存\n");
    LRUCache c;
    lruInit(&c, 3);
    int v;
    lruPut(&c, 1, 10), lruPrint(&c);
    lruPut(&c, 2, 20), lruPrint(&c);
    lruPut(&c, 3, 30), lruPrint(&c);
    printf("get(1) %s，", lruGet(&c, 1, &v) ? "命中" : "未命中"), lruPrint(&c);
    printf("put(4)：满了，淘汰最久没用的 2，"), lruPut(&c, 4, 40), lruPrint(&c);
    printf("get(2) %s\n", lruGet(&c, 2, &v) ? "命中" : "未命中");
    lruFree(&c);

    // 随机操作，和暴力实现对比
    for (int t = 0; t < 200; ++t) {
        int cap = randInt(1, 16);
        LRUCache x;
        lruInit(&x, cap);
        Brute b = {.cap = cap};
        for (int step = 0; step < 2000; ++step) {
            int key = randInt(0, 30), val = randInt(0, 1000), v1 = -1, v2 = -1;
            if (randInt(0, 1)) {
                lruPut(&x, key, val);
                brutePut(&b, key, val);
            } else {
                bool h1 = lruGet(&x, key, &v1), h2 = bruteGet(&b, key, &v2);
                CHECK(h1 == h2 && (!h1 || v1 == v2));
            }
        }
        lruFree(&x);
    }
    printf("随机 200 组、每组 2000 次操作，和暴力实现的结果一致\n");

    // 速度：100 万个 key、容量 10 万，1000 万次访问
    int universe = 1000000, len = 10000000;
    int* trace = malloc(sizeof(int) * (size_t)len);
    // 齐夫分布：第 r 个 key 被访问的概率和 1/r 成正比（用累积分布 + 二分查找抽样）
    double* cdf = malloc(sizeof(double) * (size_t)universe);
    double sum = 0;
    for (int r = 0; r < universe; ++r) cdf[r] = (sum += 1.0 / (r + 1));
    for (int i = 0; i < len; ++i) {
        double u = (double)(nextRand() >> 11) / 9007199254740992.0 * sum;
        int lo = 0, hi = universe - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cdf[mid] < u) lo = mid + 1;
            else hi = mid;
        }
        trace[i] = lo;
    }
    double t0 = nowSeconds();
    double rate = simulate(trace, len, 100000, universe, 0);
    printf("齐夫分布的 %d 次访问、容量 10 万：LRU 用时 %.3f 秒（每次 %.0f 纳秒），命中率 %.1f%%\n", len,
           nowSeconds() - t0, (nowSeconds() - t0) / len * 1e9, rate * 100);

    printf("== 命中率（齐夫分布，100 万个不同的 key）\n");
    printf("%8s %8s %8s %8s\n", "容量", "LRU", "FIFO", "随机");
    for (int cap = 1000; cap <= 100000; cap *= 10) {
        printf("%8d %7.1f%% %7.1f%% %7.1f%%\n", cap, simulate(trace, len, cap, universe, 0) * 100,
               simulate(trace, len, cap, universe, 1) * 100, simulate(trace, len, cap, universe, 2) * 100);
    }

    printf("== 命中率（循环扫描：0,1,2,…,1099 重复访问，容量 1000）\n");
    int scanLen = 1100 * 100;
    int* scan = malloc(sizeof(int) * (size_t)scanLen);
    for (int i = 0; i < scanLen; ++i) scan[i] = i % 1100;
    printf("LRU %.1f%%，FIFO %.1f%%，随机 %.1f%%\n", simulate(scan, scanLen, 1000, 1100, 0) * 100,
           simulate(scan, scanLen, 1000, 1100, 1) * 100, simulate(scan, scanLen, 1000, 1100, 2) * 100);
    free(scan);
    free(trace);
    free(cdf);
    return 0;
}
