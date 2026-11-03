#include <stdlib.h>
#include <string.h>
#include "search.h"
#include "route.h"

/* ---------- 前缀搜索：排序 + 二分查找 ---------- */

// 归并排序：按站名排列车站编号，tmp 是和 a 一样长的辅助数组
static void mergeSort(int* a, int* tmp, int n, const Metro* m)
{
    if (n < 2) return;
    int mid = n / 2;
    mergeSort(a, tmp, mid, m);
    mergeSort(a + mid, tmp, n - mid, m);
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (strcmp(m->stations[a[i]].name, m->stations[a[j]].name) <= 0) tmp[k++] = a[i++];
        else tmp[k++] = a[j++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, sizeof(int) * n);
}

void nameIndexBuild(NameIndex* ix, const Metro* m)
{
    ix->n = m->nstations;
    ix->order = malloc(sizeof(int) * (ix->n ? ix->n : 1));
    int* tmp = malloc(sizeof(int) * (ix->n ? ix->n : 1));
    if (ix->order == NULL || tmp == NULL) exit(1);
    for (int i = 0; i < ix->n; ++i) ix->order[i] = i;
    mergeSort(ix->order, tmp, ix->n, m);
    free(tmp);
}

void nameIndexFree(NameIndex* ix)
{
    free(ix->order);
    ix->order = NULL;
    ix->n = 0;
}

int nameIndexPrefix(const NameIndex* ix, const Metro* m, const char* prefix, int* first)
{
    size_t len = strlen(prefix);
    // 以 prefix 开头的站名：前 len 个字节和 prefix 相同。按字典序排好以后，它们一定挨在一起，
    // 在「前 len 个字节比 prefix 小」的站名之后，在「前 len 个字节比 prefix 大」的站名之前
    int lo = 0, hi = ix->n;
    while (lo < hi) {                           // 第一个前缀 >= prefix 的位置
        int mid = lo + (hi - lo) / 2;
        if (strncmp(m->stations[ix->order[mid]].name, prefix, len) < 0) lo = mid + 1;
        else hi = mid;
    }
    *first = lo;
    hi = ix->n;
    while (lo < hi) {                           // 第一个前缀 > prefix 的位置
        int mid = lo + (hi - lo) / 2;
        if (strncmp(m->stations[ix->order[mid]].name, prefix, len) <= 0) lo = mid + 1;
        else hi = mid;
    }
    return lo - *first;
}

/* ---------- 排行：Top K ---------- */

typedef struct {
    int station;
    int reach;                                  // 能到达的其他车站的个数
    int total;                                  // 到这些车站的时间总和
} Rank;

static int better(const Rank* a, const Rank* b)  // a 是否应该排在 b 前面
{
    if (a->reach != b->reach) return a->reach > b->reach;
    if (a->total != b->total) return a->total < b->total;
    return a->station < b->station;
}

// 大小为 k 的堆，堆顶是目前选出的 k 个中最差的那个。新来的比堆顶好，就替换堆顶
static void siftDown(Rank* h, int n, int i)
{
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && better(&h[c], &h[c + 1])) ++c;   // 选更差的那个孩子
        if (!better(&h[i], &h[c])) break;
        Rank t = h[i]; h[i] = h[c]; h[c] = t;
        i = c;
    }
}

int rankCentral(const Metro* m, int k, int* out)
{
    if (k > m->nstations) k = m->nstations;
    if (k <= 0) return 0;
    Rank* heap = malloc(sizeof(Rank) * k);
    int* minutes = malloc(sizeof(int) * m->nstations);
    if (heap == NULL || minutes == NULL) exit(1);
    int n = 0;
    for (int s = 0; s < m->nstations; ++s) {
        routeAllTimes(m, s, minutes);
        Rank r = {s, 0, 0};
        for (int t = 0; t < m->nstations; ++t) {
            if (t != s && minutes[t] >= 0) { ++r.reach; r.total += minutes[t]; }
        }
        if (n < k) {                            // 堆还没满：放进末尾，上浮
            int i = n++;
            heap[i] = r;
            while (i > 0 && better(&heap[(i - 1) / 2], &heap[i])) {
                Rank t = heap[i]; heap[i] = heap[(i - 1) / 2]; heap[(i - 1) / 2] = t;
                i = (i - 1) / 2;
            }
        } else if (better(&r, &heap[0])) {      // 比最差的好：替换堆顶，下沉
            heap[0] = r;
            siftDown(heap, n, 0);
        }
    }
    for (int i = n - 1; i >= 0; --i) {          // 依次取出最差的，从后往前放
        out[i] = heap[0].station;
        heap[0] = heap[i];
        siftDown(heap, i, 0);
    }
    free(heap);
    free(minutes);
    return n;
}
