// 哈希表的单元测试：只测试 hashmap.c，不依赖其他文件。
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hashmap.h"

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

static void testBasic(void)
{
    HashMap m;
    hmInit(&m);
    CHECK(m.size == 0, "新建的表 size 应该是 0，实际是 %zu", m.size);
    CHECK(hmGet(&m, "人民广场") == -1, "空表中查找应该返回 -1");

    hmPut(&m, "人民广场", 3);
    hmPut(&m, "果园", 1);
    hmPut(&m, "", 7);                           // 空字符串也是合法的键
    CHECK(hmGet(&m, "人民广场") == 3, "hmGet(\"人民广场\") 应该是 3，实际是 %d", hmGet(&m, "人民广场"));
    CHECK(hmGet(&m, "果园") == 1, "hmGet(\"果园\") 应该是 1，实际是 %d", hmGet(&m, "果园"));
    CHECK(hmGet(&m, "") == 7, "hmGet(\"\") 应该是 7，实际是 %d", hmGet(&m, ""));
    CHECK(hmGet(&m, "人民") == -1, "\"人民\" 不在表中，应该返回 -1");
    CHECK(m.size == 3, "插入 3 个键后 size 应该是 3，实际是 %zu", m.size);

    hmPut(&m, "果园", 10);                      // 已存在：更新值，不增加元素
    CHECK(hmGet(&m, "果园") == 10, "更新后 hmGet(\"果园\") 应该是 10，实际是 %d", hmGet(&m, "果园"));
    CHECK(m.size == 3, "更新已有的键不应该改变 size，实际是 %zu", m.size);
    hmFree(&m);
}

static void testKeyCopied(void)                 // 哈希表必须保存键的副本
{
    HashMap m;
    hmInit(&m);
    char buf[32];
    strcpy(buf, "东门");
    hmPut(&m, buf, 5);
    strcpy(buf, "西门");                        // 修改调用者的字符串
    CHECK(hmGet(&m, "东门") == 5, "修改传入的字符串后找不到 \"东门\"：键需要复制一份保存");
    CHECK(hmGet(&m, "西门") == -1, "\"西门\" 从来没有插入过，却找到了：键需要复制一份保存");
    hmFree(&m);
}

static void testMany(void)                      // 大量的键：检查扩容和速度
{
    enum { N = 200000 };
    HashMap m;
    hmInit(&m);
    char key[32];
    clock_t start = clock();
    int inserted = 0;
    for (int i = 0; i < N; ++i) {
        snprintf(key, sizeof key, "车站%d", i);
        hmPut(&m, key, i);
        ++inserted;
        if (i % 1000 == 0 && (double)(clock() - start) / CLOCKS_PER_SEC > 3.0) break;   // 太慢就提前停下
    }
    if (inserted < N) {
        CHECK(0, "3 秒内只插入了 %d 个键，太慢了：查找是不是退化成了顺序查找？有没有扩容？", inserted);
        hmFree(&m);
        return;
    }
    int wrong = 0;
    for (int i = 0; i < N; ++i) {
        snprintf(key, sizeof key, "车站%d", i);
        if (hmGet(&m, key) != i) ++wrong;
    }
    for (int i = N; i < 2 * N; ++i) {
        snprintf(key, sizeof key, "车站%d", i);
        if (hmGet(&m, key) != -1) ++wrong;
    }
    double sec = (double)(clock() - start) / CLOCKS_PER_SEC;
    CHECK(wrong == 0, "插入 %d 个键后，有 %d 次查找的结果不对", N, wrong);
    CHECK(m.size == N, "插入 %d 个不同的键后 size 应该是 %d，实际是 %zu", N, N, m.size);
    CHECK(m.nbuckets >= m.size / 2, "元素有 %zu 个，桶只有 %zu 个：忘记扩容了？", m.size, m.nbuckets);
    CHECK(sec < 3.0, "插入 %d 个键并查找 %d 次用了 %.1f 秒，太慢了：哈希函数是否均匀？是否扩容？", N, 2 * N, sec);
    hmFree(&m);
}

int main(void)
{
    struct { const char* name; void (*run)(void); } tests[] = {
        {"插入、查找和更新", testBasic},
        {"键要复制一份保存", testKeyCopied},
        {"20 万个键：扩容和速度", testMany},
    };
    int n = sizeof tests / sizeof tests[0], passed = 0;
    for (int i = 0; i < n; ++i) {
        int before = failures;
        details[0] = '\0';
        tests[i].run();
        if (failures == before) ++passed;
        printf("[%s] 哈希表：%s\n%s", failures == before ? "通过" : "失败", tests[i].name, details);
    }
    printf("HASH_TEST %d/%d\n", passed, n);
    return failures ? 1 : 0;
}
