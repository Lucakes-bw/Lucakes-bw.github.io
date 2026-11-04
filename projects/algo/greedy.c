// 贪心：活动选择（三种贪心策略，与暴力枚举对比）、跳跃游戏
#include <stdbool.h>
#include <string.h>

#include "common.h"

typedef struct {
    int start, end;                         // 左闭右开 [start, end)
} Interval;

static int byEnd(const void* a, const void* b) { return ((const Interval*)a)->end - ((const Interval*)b)->end; }
static int byStart(const void* a, const void* b) { return ((const Interval*)a)->start - ((const Interval*)b)->start; }
static int byLength(const void* a, const void* b)
{
    const Interval *x = a, *y = b;
    return (x->end - x->start) - (y->end - y->start);
}

// 按某种顺序排好以后，能放就放
static int greedy(Interval* iv, int n, int (*cmp)(const void*, const void*), bool print)
{
    Interval tmp[32];
    memcpy(tmp, iv, sizeof(Interval) * (size_t)n);
    qsort(tmp, (size_t)n, sizeof(Interval), cmp);
    int count = 0;
    Interval picked[32];
    for (int i = 0; i < n; ++i) {
        bool ok = true;
        for (int k = 0; k < count; ++k)     // 和已经选的都不重叠才选
            if (tmp[i].start < picked[k].end && picked[k].start < tmp[i].end) ok = false;
        if (ok) picked[count++] = tmp[i];
    }
    if (print) {
        for (int k = 0; k < count; ++k) printf("[%d,%d) ", picked[k].start, picked[k].end);
        printf("共 %d 个\n", count);
    }
    return count;
}

// 按结束时间排序后，只需要和上一个选中的比较：O(n log n)
static int earliestEnd(Interval* iv, int n)
{
    qsort(iv, (size_t)n, sizeof(Interval), byEnd);
    int count = 0, lastEnd = -1000000;
    for (int i = 0; i < n; ++i)
        if (iv[i].start >= lastEnd) {
            ++count;
            lastEnd = iv[i].end;
        }
    return count;
}

static int brute(const Interval* iv, int n)  // 枚举所有子集
{
    int best = 0;
    for (int mask = 0; mask < (1 << n); ++mask) {
        int cnt = __builtin_popcount((unsigned)mask);
        if (cnt <= best) continue;
        bool ok = true;
        for (int i = 0; i < n && ok; ++i)
            for (int j = i + 1; j < n && ok; ++j)
                if ((mask >> i & 1) && (mask >> j & 1) && iv[i].start < iv[j].end && iv[j].start < iv[i].end) ok = false;
        if (ok) best = cnt;
    }
    return best;
}

// 跳跃游戏：nums[i] 是在位置 i 最多能跳几步，能否到达最后一个位置
static bool canJump(const int* nums, int n)
{
    int farthest = 0;                       // 目前能到达的最远位置
    for (int i = 0; i < n; ++i) {
        if (i > farthest) return false;     // 到不了 i
        if (i + nums[i] > farthest) farthest = i + nums[i];
    }
    return true;
}

int main(void)
{
    printf("== 三种贪心的反例\n");
    Interval ex1[] = {{0, 10}, {1, 3}, {4, 6}, {7, 9}};          // 最早开始：选了 [0,10)
    Interval ex2[] = {{0, 5}, {4, 7}, {6, 11}};                   // 最短：选了 [4,7)
    printf("最早开始，例一：");
    greedy(ex1, 4, byStart, true);
    printf("最早结束，例一：");
    greedy(ex1, 4, byEnd, true);
    printf("最短，例二：");
    greedy(ex2, 3, byLength, true);
    printf("最早结束，例二：");
    greedy(ex2, 3, byEnd, true);

    printf("== 随机 10000 组（每组 12 个区间），和暴力枚举比较\n");
    int wrongStart = 0, wrongLength = 0, wrongEnd = 0;
    for (int t = 0; t < 10000; ++t) {
        int n = 12;
        Interval iv[12];
        for (int i = 0; i < n; ++i) {
            iv[i].start = randInt(0, 40);
            iv[i].end = iv[i].start + randInt(1, 15);
        }
        int best = brute(iv, n);
        if (greedy(iv, n, byStart, false) != best) ++wrongStart;
        if (greedy(iv, n, byLength, false) != best) ++wrongLength;
        if (earliestEnd(iv, n) != best) ++wrongEnd;
    }
    printf("最早开始：错 %d 组；最短：错 %d 组；最早结束：错 %d 组\n", wrongStart, wrongLength, wrongEnd);

    printf("== 跳跃游戏\n");
    int j1[] = {2, 3, 1, 1, 4}, j2[] = {3, 2, 1, 0, 4};
    printf("[2,3,1,1,4]：%s\n", canJump(j1, 5) ? "能" : "不能");
    printf("[3,2,1,0,4]：%s\n", canJump(j2, 5) ? "能" : "不能");
    return 0;
}
