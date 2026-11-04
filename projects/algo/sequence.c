// 序列上的动态规划：最长递增子序列（O(n²) 和 O(n log n)）、最长公共子序列、编辑距离
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- 最长递增子序列 ---------- */

// dp[i] = 以 a[i] 结尾的最长递增子序列的长度
static int lisQuadratic(const int* a, int n, int* dp)
{
    int best = 0;
    for (int i = 0; i < n; ++i) {
        dp[i] = 1;
        for (int j = 0; j < i; ++j)
            if (a[j] < a[i] && dp[j] + 1 > dp[i]) dp[i] = dp[j] + 1;
        if (dp[i] > best) best = dp[i];
    }
    return best;
}

// tails[k] = 所有长度为 k+1 的递增子序列中，最小的结尾。tails 是递增的，可以二分
static int lisFast(const int* a, int n, int* tails, bool print)
{
    int len = 0;
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = len;               // 找第一个 >= a[i] 的位置
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (tails[mid] < a[i]) lo = mid + 1;
            else hi = mid;
        }
        tails[lo] = a[i];                   // 替换它，或者接在最后
        if (lo == len) ++len;
        if (print) {
            printf("a[%d]=%-3d tails = [", i, a[i]);
            for (int k = 0; k < len; ++k) printf(k ? " %d" : "%d", tails[k]);
            printf("]\n");
        }
    }
    return len;
}

/* ---------- 最长公共子序列 ---------- */

static int lcs(const char* s, const char* t, int dp[][16], bool print)
{
    int m = (int)strlen(s), n = (int)strlen(t);
    for (int i = 0; i <= m; ++i)
        for (int j = 0; j <= n; ++j) {
            if (i == 0 || j == 0) dp[i][j] = 0;
            else if (s[i - 1] == t[j - 1]) dp[i][j] = dp[i - 1][j - 1] + 1;
            else dp[i][j] = dp[i - 1][j] > dp[i][j - 1] ? dp[i - 1][j] : dp[i][j - 1];
        }
    if (print) {
        printf("     ");
        for (int j = 0; j < n; ++j) printf("%3c", t[j]);
        printf("\n");
        for (int i = 0; i <= m; ++i) {
            printf("  %c ", i ? s[i - 1] : ' ');
            for (int j = 0; j <= n; ++j) printf("%3d", dp[i][j]);
            printf("\n");
        }
        // 还原一个最长公共子序列
        char out[16];
        int k = dp[m][n];
        out[k] = '\0';
        for (int i = m, j = n; i > 0 && j > 0;) {
            if (s[i - 1] == t[j - 1]) {
                out[--k] = s[i - 1];
                --i;
                --j;
            } else if (dp[i - 1][j] >= dp[i][j - 1]) {
                --i;
            } else {
                --j;
            }
        }
        printf("一个最长公共子序列：%s\n", out);
    }
    return dp[m][n];
}

/* ---------- 编辑距离 ---------- */

static int min3(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }

static int editDistance(const char* s, const char* t, bool print)
{
    int m = (int)strlen(s), n = (int)strlen(t);
    int dp[16][16];
    for (int i = 0; i <= m; ++i) dp[i][0] = i;   // s 的前 i 个字符变成空串：删 i 次
    for (int j = 0; j <= n; ++j) dp[0][j] = j;   // 空串变成 t 的前 j 个字符：插 j 次
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j) {
            if (s[i - 1] == t[j - 1]) dp[i][j] = dp[i - 1][j - 1];
            else dp[i][j] = 1 + min3(dp[i - 1][j - 1],   // 替换
                                     dp[i - 1][j],       // 删除 s[i-1]
                                     dp[i][j - 1]);      // 插入 t[j-1]
        }
    if (print) {
        // 还原操作序列（倒着走，再倒着输出）
        char ops[64][48];
        int k = 0;
        for (int i = m, j = n; i > 0 || j > 0;) {
            if (i > 0 && j > 0 && s[i - 1] == t[j - 1] && dp[i][j] == dp[i - 1][j - 1]) {
                --i;
                --j;
            } else if (i > 0 && j > 0 && dp[i][j] == dp[i - 1][j - 1] + 1) {
                snprintf(ops[k++], sizeof ops[0], "把 %c 替换成 %c", s[i - 1], t[j - 1]);
                --i;
                --j;
            } else if (i > 0 && dp[i][j] == dp[i - 1][j] + 1) {
                snprintf(ops[k++], sizeof ops[0], "删除 %c", s[i - 1]);
                --i;
            } else {
                snprintf(ops[k++], sizeof ops[0], "插入 %c", t[j - 1]);
                --j;
            }
        }
        printf("%s → %s：%d 步：", s, t, dp[m][n]);
        for (int x = k - 1; x >= 0; --x) printf("%s%s", ops[x], x ? "，" : "\n");
    }
    return dp[m][n];
}

int main(void)
{
    printf("== 最长递增子序列\n");
    int a[] = {10, 9, 2, 5, 3, 7, 101, 18};
    int dp[8], tails[8];
    printf("O(n²)：%d，dp = [", lisQuadratic(a, 8, dp));
    for (int i = 0; i < 8; ++i) printf(i ? " %d" : "%d", dp[i]);
    printf("]\n");
    printf("O(n log n)：%d\n", lisFast(a, 8, tails, true));

    for (int n = 10000; n <= 160000; n *= 4) {
        int* x = malloc(sizeof(int) * (size_t)n);
        int* buf = malloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; ++i) x[i] = randInt(0, 1000000000);
        double t = nowSeconds();
        int r1 = lisQuadratic(x, n, buf);
        double t1 = nowSeconds() - t;
        t = nowSeconds();
        int r2 = lisFast(x, n, buf, false);
        double t2 = nowSeconds() - t;
        CHECK(r1 == r2);
        printf("n=%6d：长度 %d，O(n²) %.3f 秒，O(n log n) %.6f 秒\n", n, r1, t1, t2);
        free(x);
        free(buf);
    }

    printf("== 最长公共子序列\n");
    static int table[16][16];
    printf("长度 = %d\n", lcs("ABCBDAB", "BDCABA", table, true));

    printf("== 编辑距离\n");
    editDistance("horse", "ros", true);
    editDistance("intention", "execution", true);
    editDistance("kitten", "sitting", true);
    return 0;
}
