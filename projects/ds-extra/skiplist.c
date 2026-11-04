// 跳表：查找、插入、删除；层数的分布；和普通有序链表、二分查找比较
#include <stdbool.h>
#include <string.h>

#include "common.h"

#define MAX_LEVEL 32

typedef struct SkipNode {
    int key;
    int level;                              // 这个节点有几层
    struct SkipNode* next[];                // next[i]：第 i 层的下一个节点（柔性数组）
} SkipNode;

typedef struct {
    SkipNode* head;                         // 头节点有 MAX_LEVEL 层，不存数据
    int level;                              // 当前最高的层数
    int size;
    long long comparisons;                  // 查找时比较的次数，用来看路径有多长
} SkipList;

static SkipNode* newNode(int key, int level)
{
    SkipNode* n = malloc(sizeof(SkipNode) + sizeof(SkipNode*) * (size_t)level);
    if (n == NULL) exit(1);
    n->key = key;
    n->level = level;
    for (int i = 0; i < level; ++i) n->next[i] = NULL;
    return n;
}

static void slInit(SkipList* s)
{
    s->head = newNode(0, MAX_LEVEL);
    s->level = 1;
    s->size = 0;
    s->comparisons = 0;
}

// 抛硬币：每一层有 1/2 的概率再往上长一层
static int randomLevel(void)
{
    int level = 1;
    while (level < MAX_LEVEL && (nextRand() & 1)) ++level;
    return level;
}

static bool slFind(SkipList* s, int key)
{
    SkipNode* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {           // 从最高层往下
        while (x->next[i] && (++s->comparisons, x->next[i]->key < key)) x = x->next[i];   // 在这一层往右走
    }
    x = x->next[0];
    return x && x->key == key;
}

static bool slInsert(SkipList* s, int key)
{
    SkipNode* update[MAX_LEVEL];                        // update[i]：第 i 层中，新节点前面的那个节点
    SkipNode* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    if (x->next[0] && x->next[0]->key == key) return false;   // 已经存在
    int level = randomLevel();
    if (level > s->level) {                             // 比现在的都高：多出的层前面是头节点
        for (int i = s->level; i < level; ++i) update[i] = s->head;
        s->level = level;
    }
    SkipNode* n = newNode(key, level);
    for (int i = 0; i < level; ++i) {                   // 每一层都像普通链表一样插入
        n->next[i] = update[i]->next[i];
        update[i]->next[i] = n;
    }
    ++s->size;
    return true;
}

static bool slDelete(SkipList* s, int key)
{
    SkipNode* update[MAX_LEVEL];
    SkipNode* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    x = x->next[0];
    if (!x || x->key != key) return false;
    for (int i = 0; i < x->level; ++i) update[i]->next[i] = x->next[i];
    free(x);
    while (s->level > 1 && s->head->next[s->level - 1] == NULL) --s->level;   // 最高层空了就降下来
    --s->size;
    return true;
}

static void slFree(SkipList* s)
{
    SkipNode* x = s->head;
    while (x) {
        SkipNode* nx = x->next[0];
        free(x);
        x = nx;
    }
}

static void slPrint(const SkipList* s)              // 每一层一行，同一个节点上下对齐
{
    int keys[64], n = 0;
    for (SkipNode* x = s->head->next[0]; x; x = x->next[0]) keys[n++] = x->key;
    for (int i = s->level - 1; i >= 0; --i) {
        printf("第 %d 层：头", i);
        SkipNode* x = s->head->next[i];
        for (int k = 0; k < n; ++k) {
            if (x && x->key == keys[k]) {
                printf(" → %2d", keys[k]);
                x = x->next[i];
            } else {
                printf(" ─────");
            }
        }
        printf("\n");
    }
}

static int byInt(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

int main(void)
{
    printf("== 插入 3 7 9 12 17 19 21 25 26\n");
    SkipList s;
    slInit(&s);
    int demo[] = {3, 7, 9, 12, 17, 19, 21, 25, 26};
    for (int i = 0; i < 9; ++i) slInsert(&s, demo[i]);
    slPrint(&s);
    slFree(&s);

    // 随机操作，和一个布尔数组对比
    slInit(&s);
    bool present[5000] = {false};
    for (int step = 0; step < 300000; ++step) {
        int k = randInt(0, 4999), op = randInt(0, 2);
        if (op == 0) {
            CHECK(slInsert(&s, k) == !present[k]);
            present[k] = true;
        } else if (op == 1) {
            CHECK(slDelete(&s, k) == present[k]);
            present[k] = false;
        } else {
            CHECK(slFind(&s, k) == present[k]);
        }
    }
    printf("随机插入、删除、查找 30 万次，和布尔数组的结果一致\n");
    slFree(&s);

    printf("== 层数的分布（n = 1000000）\n");
    int n = 1000000;
    slInit(&s);
    int* keys = malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; ++i) keys[i] = randInt(0, 1 << 30);
    double t0 = nowSeconds();
    for (int i = 0; i < n; ++i) slInsert(&s, keys[i]);
    double tIns = nowSeconds() - t0;
    long long perLevel[MAX_LEVEL] = {0}, pointers = 0;
    for (SkipNode* x = s.head->next[0]; x; x = x->next[0]) {
        for (int i = 0; i < x->level; ++i) ++perLevel[i];
        pointers += x->level;
    }
    for (int i = 0; i < s.level; ++i) printf("第 %2d 层：%7lld 个节点\n", i, perLevel[i]);
    printf("共 %d 个节点，最高 %d 层，平均每个节点 %.3f 个指针\n", s.size, s.level, (double)pointers / s.size);

    s.comparisons = 0;
    t0 = nowSeconds();
    for (int i = 0; i < n; ++i) CHECK(slFind(&s, keys[i]));
    double tFind = nowSeconds() - t0;
    printf("插入 %.3f 秒，查找 %d 次 %.3f 秒，平均每次比较 %.1f 次（log2 n ≈ 19.9）\n", tIns, n, tFind,
           (double)s.comparisons / n);

    // 对比：排好序的数组上二分查找
    int* sorted = malloc(sizeof(int) * (size_t)n);
    memcpy(sorted, keys, sizeof(int) * (size_t)n);
    qsort(sorted, (size_t)n, sizeof(int), byInt);
    t0 = nowSeconds();
    long long found = 0;
    for (int i = 0; i < n; ++i) found += bsearch(&keys[i], sorted, (size_t)n, sizeof(int), byInt) != NULL;
    printf("排好序的数组上二分查找 %d 次：%.3f 秒\n", n, nowSeconds() - t0);
    CHECK(found == n);
    slFree(&s);

    // 只有第 0 层（普通的有序链表）时查找有多慢：n = 20000
    int m = 20000;
    slInit(&s);
    for (int i = 0; i < m; ++i) slInsert(&s, keys[i]);
    s.comparisons = 0;
    for (int i = 0; i < m; ++i) slFind(&s, keys[i]);
    double withLevels = (double)s.comparisons / m;
    long long linear = 0;
    for (int i = 0; i < m; ++i)
        for (SkipNode* x = s.head->next[0]; x && (++linear, x->key < keys[i]); x = x->next[0]) {}
    printf("n=%d：跳表平均比较 %.1f 次，只用第 0 层（有序链表）平均比较 %.1f 次\n", m, withLevels, (double)linear / m);
    slFree(&s);
    free(keys);
    free(sorted);
    return 0;
}
