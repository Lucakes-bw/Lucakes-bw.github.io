// 字符串匹配：朴素算法、KMP，最坏情况下的比较次数和时间
#include <stdbool.h>
#include <string.h>

#include "common.h"

static long long comparisons;

// 朴素：每个起点都从头比较
static int naiveSearch(const char* text, int n, const char* pat, int m)
{
    for (int i = 0; i + m <= n; ++i) {
        int j = 0;
        while (j < m && (++comparisons, text[i + j] == pat[j])) ++j;
        if (j == m) return i;
    }
    return -1;
}

// next[j]：pat[0..j] 这一段，最长的「相等的真前缀和真后缀」的长度
static void buildNext(const char* pat, int m, int* next)
{
    next[0] = 0;
    for (int i = 1, len = 0; i < m; ++i) {
        while (len > 0 && pat[i] != pat[len]) len = next[len - 1];   // 回退到更短的前缀
        if (pat[i] == pat[len]) ++len;
        next[i] = len;
    }
}

static int kmpSearch(const char* text, int n, const char* pat, int m, const int* next)
{
    for (int i = 0, j = 0; i < n; ++i) {    // i 只往前走，从不回退
        while (j > 0 && (++comparisons, text[i] != pat[j])) j = next[j - 1];
        if (j == 0) ++comparisons;
        if (text[i] == pat[j]) ++j;
        if (j == m) return i - m + 1;
    }
    return -1;
}

static void printNext(const char* pat)
{
    int m = (int)strlen(pat), next[64];
    buildNext(pat, m, next);
    printf("%-10s next = [", pat);
    for (int i = 0; i < m; ++i) printf(i ? " %d" : "%d", next[i]);
    printf("]\n");
}

int main(void)
{
    printf("== next 数组\n");
    printNext("ababaca");
    printNext("aabaaab");
    printNext("abcdabd");
    printNext("aaaa");

    printf("== 普通的文本\n");
    const char* text = "BBC ABCDAB ABCDABCDABDE";
    const char* pat = "ABCDABD";
    int next[64];
    buildNext(pat, 7, next);
    comparisons = 0;
    int p1 = naiveSearch(text, (int)strlen(text), pat, 7);
    long long c1 = comparisons;
    comparisons = 0;
    int p2 = kmpSearch(text, (int)strlen(text), pat, 7, next);
    printf("\"%s\" 中找 \"%s\"：位置 %d / %d，朴素比较 %lld 次，KMP 比较 %lld 次\n", text, pat, p1, p2, c1, comparisons);

    printf("== 最坏情况：文本是 n 个 a，模式是 m-1 个 a 加一个 b\n");
    for (int n = 100000; n <= 10000000; n *= 10) {
        int m = 1000;
        char* t = malloc((size_t)n + 1);
        char* q = malloc((size_t)m + 1);
        int* nx = malloc(sizeof(int) * (size_t)m);
        memset(t, 'a', (size_t)n);
        t[n] = '\0';
        memset(q, 'a', (size_t)m - 1);
        q[m - 1] = 'b';
        q[m] = '\0';
        buildNext(q, m, nx);
        comparisons = 0;
        double s = nowSeconds();
        int r1 = naiveSearch(t, n, q, m);
        double t1 = nowSeconds() - s;
        long long c1n = comparisons;
        comparisons = 0;
        s = nowSeconds();
        int r2 = kmpSearch(t, n, q, m, nx);
        double t2 = nowSeconds() - s;
        long long c2n = comparisons;
        s = nowSeconds();
        const char* r3 = strstr(t, q);
        double t3 = nowSeconds() - s;
        CHECK(r1 == -1 && r2 == -1 && r3 == NULL);
        printf("n=%8d m=%d：朴素 %11lld 次 %.3f 秒 | KMP %9lld 次 %.4f 秒 | strstr %.4f 秒\n", n, m, c1n, t1, c2n, t2, t3);
        free(t);
        free(q);
        free(nx);
    }

    // 随机检查：KMP 和朴素算法的结果一致
    for (int k = 0; k < 100000; ++k) {
        char t[64], q[8];
        int n = randInt(1, 63), m = randInt(1, 7);
        for (int i = 0; i < n; ++i) t[i] = (char)('a' + randInt(0, 2));
        for (int i = 0; i < m; ++i) q[i] = (char)('a' + randInt(0, 2));
        t[n] = q[m] = '\0';
        int nx[8];
        buildNext(q, m, nx);
        CHECK(naiveSearch(t, n, q, m) == kmpSearch(t, n, q, m, nx));
    }
    printf("随机 100000 组：KMP 和朴素算法的结果一致\n");
    return 0;
}
