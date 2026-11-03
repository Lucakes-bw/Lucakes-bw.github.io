// 前缀搜索的单元测试：只测试 nameIndexBuild 和 nameIndexPrefix，自己构造 Metro，不读取数据文件。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "search.h"

static int failures = 0;
static char details[4096];                      // 当前测试的失败原因，测试结束后统一输出

#define CHECK(cond, ...) do {                                                   \
        if (!(cond)) {                                                          \
            ++failures;                                                         \
            size_t len = strlen(details);                                       \
            if (len + 512 < sizeof details) {                                   \
                len += snprintf(details + len, sizeof details - len, "      "); \
                len += snprintf(details + len, sizeof details - len, __VA_ARGS__); \
                snprintf(details + len, sizeof details - len, "\n");            \
            }                                                                   \
        }                                                                       \
    } while (0)

// 只填写车站的名字：nameIndexBuild 和 nameIndexPrefix 只会用到它们
static Metro makeMetro(const char* const* names, int n)
{
    Metro m;
    memset(&m, 0, sizeof m);
    m.stations = calloc(n ? n : 1, sizeof(Station));
    if (m.stations == NULL) exit(1);
    for (int i = 0; i < n; ++i) {
        m.stations[i].name = malloc(strlen(names[i]) + 1);
        if (m.stations[i].name == NULL) exit(1);
        strcpy(m.stations[i].name, names[i]);
    }
    m.nstations = m.capStations = n;
    return m;
}

static void freeMetro(Metro* m)
{
    for (int i = 0; i < m->nstations; ++i) free(m->stations[i].name);
    free(m->stations);
}

// order 是不是 0..n-1 的一个排列，并且按站名从小到大排列
static void checkSorted(const NameIndex* ix, const Metro* m)
{
    CHECK(ix->n == m->nstations, "ix->n 应该等于车站的个数 %d，实际是 %d", m->nstations, ix->n);
    if (ix->n != m->nstations || ix->order == NULL) return;
    char* seen = calloc(ix->n ? ix->n : 1, 1);
    if (seen == NULL) exit(1);
    int bad = 0;
    for (int i = 0; i < ix->n && !bad; ++i) {
        int s = ix->order[i];
        if (s < 0 || s >= ix->n || seen[s]) {
            CHECK(0, "order[%d] = %d：不是车站编号，或者重复出现了", i, s);
            bad = 1;
        } else {
            seen[s] = 1;
        }
    }
    for (int i = 1; i < ix->n && !bad; ++i) {
        const char* a = m->stations[ix->order[i - 1]].name;
        const char* b = m->stations[ix->order[i]].name;
        if (strcmp(a, b) > 0) {
            CHECK(0, "order[%d] 是 \"%s\"，order[%d] 是 \"%s\"，顺序反了", i - 1, a, i, b);
            bad = 1;
        }
    }
    free(seen);
}

static void testSort(void)
{
    const char* names[] = {"西山", "果园", "动物园", "人民广场", "市政府", "东门", "科技园", "机场",
                           "北站", "体育馆", "文化宫", "南湖", "大学城", "南站", "植物园", "老街",
                           "会展中心", "东湖", "西站", "花园路", "博物馆", "港口"};
    Metro m = makeMetro(names, 22);
    NameIndex ix;
    nameIndexBuild(&ix, &m);
    checkSorted(&ix, &m);
    nameIndexFree(&ix);
    freeMetro(&m);

    Metro empty = makeMetro(NULL, 0);           // 没有车站也不能出错
    nameIndexBuild(&ix, &empty);
    CHECK(ix.n == 0, "没有车站时 ix->n 应该是 0，实际是 %d", ix.n);
    int first = -1;
    CHECK(nameIndexPrefix(&ix, &empty, "东", &first) == 0, "没有车站时，任何前缀都应该返回 0");
    nameIndexFree(&ix);
    freeMetro(&empty);
}

static void testPrefix(void)
{
    const char* names[] = {"BA", "ABD", "东门", "A", "B", "ABC", "东湖", "AB"};
    Metro m = makeMetro(names, 8);
    NameIndex ix;
    nameIndexBuild(&ix, &m);
    checkSorted(&ix, &m);
    if (ix.n != 8 || ix.order == NULL) { nameIndexFree(&ix); freeMetro(&m); return; }
    // 排好序以后是：A AB ABC ABD B BA 东湖 东门
    struct { const char* prefix; int count, first; } cases[] = {
        {"A", 4, 0}, {"AB", 3, 1}, {"ABC", 1, 2}, {"ABCD", 0, -1}, {"B", 2, 4}, {"BA", 1, 5},
        {"东", 2, 6}, {"东门", 1, 7}, {"东门站", 0, -1}, {"C", 0, -1}, {"0", 0, -1}, {"Z", 0, -1},
        {"\xe4", 2, 6},                         // 「东」的 UTF-8 编码是 E4 B8 9C，按字节比较时 \xe4 也是它的前缀
        {"", 8, 0},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        int first = -1;
        int n = nameIndexPrefix(&ix, &m, cases[i].prefix, &first);
        CHECK(n == cases[i].count, "前缀 \"%s\"：应该有 %d 个匹配，实际返回 %d", cases[i].prefix, cases[i].count, n);
        if (n == cases[i].count && n > 0)
            CHECK(first == cases[i].first, "前缀 \"%s\"：第一个匹配应该在 order[%d]（\"%s\"），实际 *first = %d",
                  cases[i].prefix, cases[i].first, m.stations[ix.order[cases[i].first]].name, first);
    }
    nameIndexFree(&ix);
    freeMetro(&m);
}

// [0, 199999] 中，十进制写法以 x 开头的整数有几个
static int countWithPrefix(int x)
{
    if (x == 0) return 1;
    int count = 0;
    for (long lo = x, hi = x; lo <= 199999; lo *= 10, hi = hi * 10 + 9) {
        long a = lo, b = hi < 199999 ? hi : 199999;
        if (a <= b) count += (int)(b - a + 1);
    }
    return count;
}

static void testMany(void)                      // 20 万个车站：检查排序和二分查找的速度
{
    enum { N = 200000 };
    int before = failures;
    char** names = malloc(sizeof(char*) * N);
    char buf[32];
    if (names == NULL) exit(1);
    for (int i = 0; i < N; ++i) {               // 车站0～车站199999，打乱顺序
        snprintf(buf, sizeof buf, "车站%d", (int)((i * 7919L) % N));
        names[i] = malloc(strlen(buf) + 1);
        if (names[i] == NULL) exit(1);
        strcpy(names[i], buf);
    }
    Metro m = makeMetro((const char* const*)names, N);
    for (int i = 0; i < N; ++i) free(names[i]);
    free(names);

    clock_t start = clock();
    NameIndex ix;
    nameIndexBuild(&ix, &m);
    double sortSec = (double)(clock() - start) / CLOCKS_PER_SEC;
    CHECK(sortSec < 3.0, "给 %d 个站名排序用了 %.1f 秒，太慢了：是不是用了 O(n²) 的排序？", N, sortSec);
    checkSorted(&ix, &m);
    if (failures != before) { nameIndexFree(&ix); freeMetro(&m); return; }

    start = clock();
    int wrong = 0, done = 0;
    for (int q = 0; q < N; ++q) {
        int x = (int)((q * 104729L) % N), first = -1;
        snprintf(buf, sizeof buf, "车站%d", x);
        int n = nameIndexPrefix(&ix, &m, buf, &first);
        if (n != countWithPrefix(x) || (n > 0 && strcmp(m.stations[ix.order[first]].name, buf) != 0)) {
            if (wrong++ == 0) CHECK(0, "前缀 \"%s\"：应该有 %d 个匹配，实际返回 %d", buf, countWithPrefix(x), n);
        }
        ++done;
        if (q % 1000 == 0 && (double)(clock() - start) / CLOCKS_PER_SEC > 3.0) break;   // 太慢就提前停下
    }
    if (done < N) CHECK(0, "3 秒内只完成了 %d 次前缀查找，太慢了：是不是从头到尾逐个比较了？", done);
    CHECK(wrong == 0, "%d 次前缀查找中，有 %d 次结果不对", done, wrong);
    nameIndexFree(&ix);
    freeMetro(&m);
}

int main(void)
{
    struct { const char* name; void (*run)(void); } tests[] = {
        {"按站名排序", testSort},
        {"前缀查找的边界情况", testPrefix},
        {"20 万个车站：排序和二分查找的速度", testMany},
    };
    int n = sizeof tests / sizeof tests[0], passed = 0;
    for (int i = 0; i < n; ++i) {
        int before = failures;
        details[0] = '\0';
        tests[i].run();
        if (failures == before) ++passed;
        printf("[%s] 前缀搜索：%s\n%s", failures == before ? "通过" : "失败", tests[i].name, details);
    }
    printf("SEARCH_TEST %d/%d\n", passed, n);
    return failures ? 1 : 0;
}
