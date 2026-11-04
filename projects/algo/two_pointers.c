// 双指针与滑动窗口：有序数组的两数之和、盛水最多的容器、无重复字符的最长子串、最小覆盖子串
#include <stdbool.h>
#include <string.h>

#include "common.h"

// 有序数组中找两个数，和为 target：左右两个指针向中间走
static bool twoSumSorted(const int* a, int n, int target, int* i, int* j)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int s = a[lo] + a[hi];
        if (s == target) {
            *i = lo;
            *j = hi;
            return true;
        }
        if (s < target) ++lo;               // 和太小：左边的数换大一点
        else --hi;                          // 和太大：右边的数换小一点
    }
    return false;
}

static long long maxAreaBrute(const int* h, int n)
{
    long long best = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            long long area = (long long)(j - i) * (h[i] < h[j] ? h[i] : h[j]);
            if (area > best) best = area;
        }
    return best;
}

// 每次移动较矮的那一边：移动较高的那一边，宽度变小，高度不会超过较矮的一边，面积不可能变大
static long long maxArea(const int* h, int n, bool print)
{
    int lo = 0, hi = n - 1;
    long long best = 0;
    while (lo < hi) {
        long long area = (long long)(hi - lo) * (h[lo] < h[hi] ? h[lo] : h[hi]);
        if (print) printf("lo=%d hi=%d 面积=%lld\n", lo, hi, area);
        if (area > best) best = area;
        if (h[lo] < h[hi]) ++lo;
        else --hi;
    }
    return best;
}

// 无重复字符的最长子串：窗口 [left, i]，last[c] 是字符 c 上一次出现的位置
static int longestUnique(const char* s, bool print)
{
    int last[256];
    for (int c = 0; c < 256; ++c) last[c] = -1;
    int left = 0, best = 0, bestLeft = 0;
    for (int i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (last[c] >= left) left = last[c] + 1;   // c 在窗口中出现过：左边界跳到它的后面
        last[c] = i;
        if (i - left + 1 > best) {
            best = i - left + 1;
            bestLeft = left;
        }
    }
    if (print) printf("\"%s\"：%d（\"%.*s\"）\n", s, best, best, s + bestLeft);
    return best;
}

static int longestUniqueBrute(const char* s)
{
    int n = (int)strlen(s), best = 0;
    for (int i = 0; i < n; ++i) {
        bool seen[256] = {false};
        for (int j = i; j < n; ++j) {
            if (seen[(unsigned char)s[j]]) break;
            seen[(unsigned char)s[j]] = true;
            if (j - i + 1 > best) best = j - i + 1;
        }
    }
    return best;
}

// 最小覆盖子串：s 中包含 t 的所有字符（含重复）的最短子串
static void minWindow(const char* s, const char* t)
{
    int need[256] = {0}, missing = 0;
    for (int i = 0; t[i]; ++i) {            // need[c]：窗口中还缺几个 c（可以是负数，表示多了）
        ++need[(unsigned char)t[i]];
        ++missing;                          // 一共还缺几个字符
    }
    int left = 0, bestLen = 1 << 30, bestLeft = 0;
    for (int right = 0; s[right]; ++right) {
        if (need[(unsigned char)s[right]]-- > 0) --missing;      // 这个字符还缺，现在补上了一个
        while (missing == 0) {                                   // 窗口已经覆盖了 t：尽量收缩左边
            if (right - left + 1 < bestLen) {
                bestLen = right - left + 1;
                bestLeft = left;
            }
            if (++need[(unsigned char)s[left]] > 0) ++missing;   // 移走的字符是必需的，又缺了
            ++left;
        }
    }
    if (bestLen == 1 << 30) printf("s=\"%s\" t=\"%s\"：没有\n", s, t);
    else printf("s=\"%s\" t=\"%s\"：\"%.*s\"\n", s, t, bestLen, s + bestLeft);
}

int main(void)
{
    printf("== 有序数组的两数之和\n");
    int a[] = {2, 7, 11, 15, 19, 24};
    int i, j;
    if (twoSumSorted(a, 6, 26, &i, &j)) printf("和为 26：a[%d]=%d + a[%d]=%d\n", i, a[i], j, a[j]);

    printf("== 盛水最多的容器\n");
    int h[] = {1, 8, 6, 2, 5, 4, 8, 3, 7};
    printf("最大面积 = %lld\n", maxArea(h, 9, true));
    for (int n = 10000; n <= 160000; n *= 4) {
        int* x = malloc(sizeof(int) * (size_t)n);
        for (int k = 0; k < n; ++k) x[k] = randInt(0, 10000);
        double t = nowSeconds();
        long long r1 = maxAreaBrute(x, n);
        double t1 = nowSeconds() - t;
        t = nowSeconds();
        long long r2 = maxArea(x, n, false);
        double t2 = nowSeconds() - t;
        CHECK(r1 == r2);
        printf("n=%6d：暴力 %.3f 秒，双指针 %.6f 秒\n", n, t1, t2);
        free(x);
    }

    printf("== 无重复字符的最长子串\n");
    longestUnique("abcabcbb", true);
    longestUnique("pwwkew", true);
    longestUnique("abba", true);
    for (int t = 0; t < 10000; ++t) {
        char s[40];
        int n = randInt(0, 39);
        for (int k = 0; k < n; ++k) s[k] = (char)('a' + randInt(0, 5));
        s[n] = '\0';
        CHECK(longestUnique(s, false) == longestUniqueBrute(s));
    }
    printf("随机 10000 个字符串：和暴力枚举一致\n");

    printf("== 最小覆盖子串\n");
    minWindow("ADOBECODEBANC", "ABC");
    minWindow("a", "aa");
    minWindow("aaflslflsldkalskaaa", "aaa");
    return 0;
}
