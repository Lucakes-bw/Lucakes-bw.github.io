// 动态规划入门：斐波那契的三种写法、零钱兑换、最小路径和
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- 斐波那契 ---------- */

static long long calls;

static long long fibNaive(int n)            // 直接按定义递归
{
    ++calls;
    if (n <= 1) return n;
    return fibNaive(n - 1) + fibNaive(n - 2);
}

static long long memo[100];
static long long fibMemo(int n)             // 记忆化搜索：算过的结果记下来
{
    ++calls;
    if (n <= 1) return n;
    if (memo[n] != 0) return memo[n];
    return memo[n] = fibMemo(n - 1) + fibMemo(n - 2);
}

static long long fibIter(int n)             // 递推：从小到大算，只保留最后两个
{
    long long a = 0, b = 1;                 // a = F(i)，b = F(i+1)
    for (int i = 0; i < n; ++i) {
        long long t = a + b;
        a = b;
        b = t;
    }
    return a;
}

/* ---------- 零钱兑换 ---------- */

// 贪心：每次拿面值最大、又不超过剩余金额的硬币
static int coinGreedy(const int* coins, int k, int amount)   // coins 从大到小排列
{
    int count = 0;
    for (int i = 0; i < k; ++i) {
        while (amount >= coins[i]) {
            amount -= coins[i];
            ++count;
        }
    }
    return amount == 0 ? count : -1;
}

// 动态规划：dp[x] = 凑出金额 x 最少要几枚硬币
static int coinDP(const int* coins, int k, int amount, int* dp, bool print)
{
    dp[0] = 0;
    for (int x = 1; x <= amount; ++x) {
        dp[x] = INT_MAX;
        for (int i = 0; i < k; ++i) {
            if (coins[i] <= x && dp[x - coins[i]] != INT_MAX && dp[x - coins[i]] + 1 < dp[x]) {
                dp[x] = dp[x - coins[i]] + 1;
            }
        }
    }
    if (print) {
        printf("x  ");
        for (int x = 0; x <= amount; ++x) printf("%3d", x);
        printf("\ndp ");
        for (int x = 0; x <= amount; ++x) printf("%3d", dp[x] == INT_MAX ? -1 : dp[x]);
        printf("\n");
    }
    return dp[amount] == INT_MAX ? -1 : dp[amount];
}

/* ---------- 最小路径和 ---------- */

static int minPathSum(int g[][4], int rows, int cols, int dp[][4])
{
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) dp[r][c] = g[0][0];
            else if (r == 0) dp[r][c] = dp[r][c - 1] + g[r][c];      // 第一行只能从左边来
            else if (c == 0) dp[r][c] = dp[r - 1][c] + g[r][c];      // 第一列只能从上边来
            else dp[r][c] = (dp[r - 1][c] < dp[r][c - 1] ? dp[r - 1][c] : dp[r][c - 1]) + g[r][c];
        }
    return dp[rows - 1][cols - 1];
}

int main(void)
{
    printf("== 斐波那契：直接递归\n");
    for (int n = 10; n <= 45; n += 5) {
        calls = 0;
        double t = nowSeconds();
        long long v = fibNaive(n);
        double dt = nowSeconds() - t;
        CHECK(v == fibIter(n));
        printf("F(%d) = %lld，调用 %lld 次，%.3f 秒\n", n, v, calls, dt);
    }
    printf("== 斐波那契：记忆化\n");
    for (int n = 10; n <= 90; n += 40) {
        memset(memo, 0, sizeof memo);
        calls = 0;
        long long v = fibMemo(n);
        CHECK(v == fibIter(n));
        printf("F(%d) = %lld，调用 %lld 次\n", n, v, calls);
    }
    calls = 0;
    fibNaive(5);
    printf("F(5) 直接递归的调用次数：%lld\n", calls);

    printf("== 零钱兑换：硬币 {1, 3, 4}，金额 6\n");
    int coins[] = {4, 3, 1};
    int dp[1001];
    printf("贪心：%d 枚\n", coinGreedy(coins, 3, 6));
    printf("动态规划：%d 枚\n", coinDP(coins, 3, 6, dp, true));
    // 随机检查：贪心在 {1,3,4} 上错多少次
    int wrong = 0;
    for (int x = 1; x <= 1000; ++x) {
        coinDP(coins, 3, x, dp, false);
        if (coinGreedy(coins, 3, x) != dp[x]) ++wrong;
    }
    printf("金额 1~1000 中，贪心不是最优的：%d 个\n", wrong);
    int rmb[] = {100, 50, 20, 10, 5, 1};
    wrong = 0;
    for (int x = 1; x <= 1000; ++x) {
        coinDP(rmb, 6, x, dp, false);
        if (coinGreedy(rmb, 6, x) != dp[x]) ++wrong;
    }
    printf("人民币面值 {1,5,10,20,50,100}，金额 1~1000 中，贪心不是最优的：%d 个\n", wrong);

    printf("== 最小路径和\n");
    int g[3][4] = {{1, 3, 1, 2}, {1, 5, 1, 3}, {4, 2, 1, 1}};
    int pd[3][4];
    int best = minPathSum(g, 3, 4, pd);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) printf("%3d", pd[r][c]);
        printf("\n");
    }
    printf("最小路径和 = %d\n", best);
    return 0;
}
