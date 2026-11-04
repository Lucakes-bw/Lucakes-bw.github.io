// 回溯：全排列、子集、N 皇后
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- 全排列 ---------- */

static int perm[16];
static bool used[16];
static long long permCount;

static void permute(int n, int depth, bool print)
{
    if (depth == n) {                       // 每个位置都选好了：得到一个排列
        ++permCount;
        if (print) {
            for (int i = 0; i < n; ++i) printf("%d", perm[i]);
            printf(" ");
        }
        return;
    }
    for (int x = 1; x <= n; ++x) {
        if (used[x]) continue;              // 已经用过的数不能再选
        used[x] = true;                     // 做选择
        perm[depth] = x;
        permute(n, depth + 1, print);
        used[x] = false;                    // 撤销选择
    }
}

/* ---------- 子集 ---------- */

static int chosen[16];
static long long subsetCount;

static void subsets(const int* a, int n, int start, int k, bool print)
{
    ++subsetCount;                          // 每个结点都是一个子集（包括空集）
    if (print) {
        printf("{");
        for (int i = 0; i < k; ++i) printf(i ? ",%d" : "%d", chosen[i]);
        printf("} ");
    }
    for (int i = start; i < n; ++i) {       // 只从 start 往后选，避免 {1,2} 和 {2,1} 重复
        chosen[k] = a[i];
        subsets(a, n, i + 1, k + 1, print);
    }
}

/* ---------- N 皇后 ---------- */

static int queenCol[32];                    // queenCol[r]：第 r 行的皇后在第几列
static long long nodes;                     // 搜索树中访问的结点数

// 写法一：先把 n 个皇后都放好（每行任选一列），最后再检查是否互相攻击
static bool validAll(int n)
{
    for (int r1 = 0; r1 < n; ++r1)
        for (int r2 = r1 + 1; r2 < n; ++r2) {
            int c1 = queenCol[r1], c2 = queenCol[r2];
            if (c1 == c2 || abs(c1 - c2) == r2 - r1) return false;
        }
    return true;
}

static long long queensNoPrune(int n, int row)
{
    ++nodes;
    if (row == n) return validAll(n) ? 1 : 0;
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        queenCol[row] = c;
        total += queensNoPrune(n, row + 1);
    }
    return total;
}

// 写法二：每放一个皇后就检查它和前面的皇后是否冲突，冲突就不往下走（剪枝）
static bool safe(int row, int c)
{
    for (int r = 0; r < row; ++r) {
        if (queenCol[r] == c || abs(queenCol[r] - c) == row - r) return false;
    }
    return true;
}

static long long queensPrune(int n, int row)
{
    ++nodes;
    if (row == n) return 1;
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        if (!safe(row, c)) continue;        // 剪枝
        queenCol[row] = c;
        total += queensPrune(n, row + 1);
    }
    return total;
}

// 写法三：用三个布尔数组记录哪些列、哪些对角线已经被占用，检查变成 O(1)
static bool colUsed[32], diag1[64], diag2[64];   // 主对角线 r-c+n，副对角线 r+c

static long long queensFast(int n, int row)
{
    ++nodes;
    if (row == n) return 1;
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        if (colUsed[c] || diag1[row - c + n] || diag2[row + c]) continue;
        colUsed[c] = diag1[row - c + n] = diag2[row + c] = true;
        total += queensFast(n, row + 1);
        colUsed[c] = diag1[row - c + n] = diag2[row + c] = false;
    }
    return total;
}

static void printBoard(int n)
{
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) printf(queenCol[r] == c ? "Q " : ". ");
        printf("\n");
    }
}

static bool firstSolution(int n, int row)
{
    if (row == n) return true;
    for (int c = 0; c < n; ++c) {
        if (!safe(row, c)) continue;
        queenCol[row] = c;
        if (firstSolution(n, row + 1)) return true;
    }
    return false;
}

int main(void)
{
    printf("== 全排列\n");
    permute(3, 0, true);
    printf("\n");
    for (int n = 1; n <= 10; ++n) {
        permCount = 0;
        permute(n, 0, false);
        printf("n=%d：%lld 个排列\n", n, permCount);
    }

    printf("== 子集\n");
    int a[] = {1, 2, 3};
    subsets(a, 3, 0, 0, true);
    printf("\n");

    printf("== 8 皇后的第一个解\n");
    firstSolution(8, 0);
    printBoard(8);

    printf("== N 皇后：三种写法\n");
    printf("%3s %10s | %14s %9s | %12s %9s | %12s %9s\n", "n", "解的个数", "不剪枝的结点", "时间", "剪枝的结点", "时间",
           "布尔数组结点", "时间");
    for (int n = 4; n <= 14; ++n) {
        long long s1 = -1, n1 = 0;
        double t1 = 0;
        if (n <= 8) {
            nodes = 0;
            double t = nowSeconds();
            s1 = queensNoPrune(n, 0);
            t1 = nowSeconds() - t;
            n1 = nodes;
        }
        nodes = 0;
        double t = nowSeconds();
        long long s2 = queensPrune(n, 0);
        double t2 = nowSeconds() - t;
        long long n2 = nodes;
        nodes = 0;
        t = nowSeconds();
        long long s3 = queensFast(n, 0);
        double t3 = nowSeconds() - t;
        long long n3 = nodes;
        CHECK(s2 == s3 && (s1 < 0 || s1 == s2));
        if (s1 >= 0) printf("%3d %10lld | %14lld %8.3fs | %12lld %8.3fs | %12lld %8.3fs\n", n, s2, n1, t1, n2, t2, n3, t3);
        else printf("%3d %10lld | %14s %9s | %12lld %8.3fs | %12lld %8.3fs\n", n, s2, "-", "-", n2, t2, n3, t3);
    }
    return 0;
}
