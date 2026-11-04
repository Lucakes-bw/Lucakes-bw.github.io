// 背包问题：0-1 背包的二维表、一维滚动数组（为什么要倒序）、完全背包、分割等和子集
#include <stdbool.h>
#include <string.h>

#include "common.h"

#define MAXW 64

static int max2(int a, int b) { return a > b ? a : b; }

// 0-1 背包：dp[i][w] = 只考虑前 i 件物品、容量为 w 时的最大价值
static int knapsack2D(const int* wt, const int* val, int n, int W, int dp[][MAXW], bool print)
{
    for (int w = 0; w <= W; ++w) dp[0][w] = 0;
    for (int i = 1; i <= n; ++i)
        for (int w = 0; w <= W; ++w) {
            dp[i][w] = dp[i - 1][w];                                         // 不选第 i 件
            if (wt[i - 1] <= w) dp[i][w] = max2(dp[i][w], dp[i - 1][w - wt[i - 1]] + val[i - 1]);   // 选第 i 件
        }
    if (print) {
        printf("%-14s", "物品\\容量");
        for (int w = 0; w <= W; ++w) printf("%3d", w);
        printf("\n");
        for (int i = 0; i <= n; ++i) {
            if (i == 0) printf("%-12s", "（无）");
            else printf("%d：重%d 值%-4d ", i, wt[i - 1], val[i - 1]);
            for (int w = 0; w <= W; ++w) printf("%3d", dp[i][w]);
            printf("\n");
        }
    }
    return dp[n][W];
}

// 选了哪些：从 dp[n][W] 往回走
static void traceBack(const int* wt, int n, int W, int dp[][MAXW])
{
    printf("选了：");
    for (int i = n, w = W; i > 0; --i) {
        if (dp[i][w] != dp[i - 1][w]) {     // 和不选第 i 件不同，说明选了
            printf("物品%d ", i);
            w -= wt[i - 1];
        }
    }
    printf("\n");
}

// 一维：容量倒序遍历
static int knapsack1D(const int* wt, const int* val, int n, int W, bool reverse)
{
    int dp[MAXW] = {0};
    for (int i = 0; i < n; ++i) {
        if (reverse) {
            for (int w = W; w >= wt[i]; --w) dp[w] = max2(dp[w], dp[w - wt[i]] + val[i]);
        } else {                            // 正序：dp[w - wt[i]] 可能已经是「选过第 i 件」的值了
            for (int w = wt[i]; w <= W; ++w) dp[w] = max2(dp[w], dp[w - wt[i]] + val[i]);
        }
    }
    return dp[W];
}

// 暴力：枚举所有 2^n 种选法
static int knapsackBrute(const int* wt, const int* val, int n, int W)
{
    int best = 0;
    for (int mask = 0; mask < (1 << n); ++mask) {
        int w = 0, v = 0;
        for (int i = 0; i < n; ++i)
            if (mask >> i & 1) {
                w += wt[i];
                v += val[i];
            }
        if (w <= W && v > best) best = v;
    }
    return best;
}

// 分割等和子集：能否选出一些数，和恰好是总和的一半
static bool canPartition(const int* a, int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += a[i];
    if (sum % 2) return false;
    int target = sum / 2;
    bool ok[10001] = {true};                // ok[s]：能否凑出和 s
    for (int i = 0; i < n; ++i)
        for (int s = target; s >= a[i]; --s) ok[s] = ok[s] || ok[s - a[i]];
    return ok[target];
}

int main(void)
{
    int wt[] = {1, 3, 4, 5}, val[] = {15, 20, 30, 55};
    int n = 4, W = 8;
    static int dp[16][MAXW];
    printf("== 0-1 背包：容量 %d\n", W);
    int best = knapsack2D(wt, val, n, W, dp, true);
    printf("最大价值 = %d\n", best);
    traceBack(wt, n, W, dp);
    printf("一维（倒序）= %d，一维（正序）= %d\n", knapsack1D(wt, val, n, W, true), knapsack1D(wt, val, n, W, false));

    // 随机检查：二维、一维倒序都和暴力枚举一致；一维正序会出错
    int wrongForward = 0;
    for (int t = 0; t < 10000; ++t) {
        int k = randInt(1, 12), cap = randInt(1, 40);
        int w2[12], v2[12];
        for (int i = 0; i < k; ++i) {
            w2[i] = randInt(1, 15);
            v2[i] = randInt(1, 100);
        }
        int brute = knapsackBrute(w2, v2, k, cap);
        CHECK(knapsack2D(w2, v2, k, cap, dp, false) == brute);
        CHECK(knapsack1D(w2, v2, k, cap, true) == brute);
        if (knapsack1D(w2, v2, k, cap, false) != brute) ++wrongForward;
    }
    printf("随机 10000 组：二维、一维倒序都和暴力枚举一致；一维正序错了 %d 组\n", wrongForward);

    printf("== 分割等和子集\n");
    int a1[] = {1, 5, 11, 5}, a2[] = {1, 2, 3, 5};
    printf("{1,5,11,5}：%s\n", canPartition(a1, 4) ? "可以" : "不可以");
    printf("{1,2,3,5}：%s\n", canPartition(a2, 4) ? "可以" : "不可以");
    return 0;
}
