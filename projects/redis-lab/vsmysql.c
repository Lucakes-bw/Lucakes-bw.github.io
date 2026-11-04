// 同样是「按主键读一个值」，Redis 和 MySQL 各要多久：逐个请求，统计平均值和分位数
#include <mysql/mysql.h>
#include <string.h>

#include "common.h"

#define N 100000

static int cmpDouble(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

static void report(const char* name, double* lat, int n, double total)
{
    qsort(lat, (size_t)n, sizeof(double), cmpDouble);
    printf("%-22s %6.2f 秒  %7.0f 次/秒  平均 %5.1f 微秒  中位数 %5.1f  P99 %6.1f\n", name, total, n / total,
           total / n * 1e6, lat[n / 2] * 1e6, lat[n * 99 / 100] * 1e6);
}

static double lat[N];

int main(void)
{
    // ---------- Redis ----------
    redisContext* r = connectRedis();
    freeReplyObject(cmd(r, "FLUSHALL"));
    for (int i = 0; i < N; ++i) redisAppendCommand(r, "SET user:%d name-%d", i, i);   // 批量写入，见下一篇的 Pipeline
    for (int i = 0; i < N; ++i) {
        redisReply* rep;
        CHECK(redisGetReply(r, (void**)&rep) == REDIS_OK);
        freeReplyObject(rep);
    }
    double t0 = nowSeconds();
    for (int i = 0; i < N; ++i) {
        int id = randInt(0, N - 1);
        double a = nowSeconds();
        redisReply* rep = cmd(r, "GET user:%d", id);
        lat[i] = nowSeconds() - a;
        char want[32];
        snprintf(want, sizeof want, "name-%d", id);
        CHECK(rep->type == REDIS_REPLY_STRING && strcmp(rep->str, want) == 0);
        freeReplyObject(rep);
    }
    report("Redis GET", lat, N, nowSeconds() - t0);

    t0 = nowSeconds();
    for (int i = 0; i < N; ++i) {
        double a = nowSeconds();
        freeReplyObject(cmd(r, "PING"));
        lat[i] = nowSeconds() - a;
    }
    report("Redis PING", lat, N, nowSeconds() - t0);
    redisFree(r);

    // ---------- MySQL ----------
    MYSQL* m = mysql_init(NULL);
    const char* mh = getenv("MYSQL_HOST");
    if (!mysql_real_connect(m, mh ? mh : "127.0.0.1", "root", "lab", NULL, 3306, NULL, 0)) {
        fprintf(stderr, "连接 MySQL 失败：%s\n", mysql_error(m));
        return 1;
    }
    mysql_query(m, "DROP DATABASE IF EXISTS rlab");
    CHECK(mysql_query(m, "CREATE DATABASE rlab") == 0);
    CHECK(mysql_query(m, "CREATE TABLE rlab.user (id INT PRIMARY KEY, name VARCHAR(32) NOT NULL)") == 0);
    CHECK(mysql_query(m, "START TRANSACTION") == 0);
    char sql[128];
    for (int i = 0; i < N; ++i) {
        snprintf(sql, sizeof sql, "INSERT INTO rlab.user VALUES (%d, 'name-%d')", i, i);
        CHECK(mysql_query(m, sql) == 0);
    }
    CHECK(mysql_query(m, "COMMIT") == 0);
    for (int i = 0; i < N; ++i) {        // 先读一遍，让数据都在缓冲池中，比较的是「内存中的数据」
        snprintf(sql, sizeof sql, "SELECT name FROM rlab.user WHERE id = %d", i);
        CHECK(mysql_query(m, sql) == 0);
        mysql_free_result(mysql_store_result(m));
    }

    t0 = nowSeconds();
    for (int i = 0; i < N; ++i) {
        int id = randInt(0, N - 1);
        double a = nowSeconds();
        snprintf(sql, sizeof sql, "SELECT name FROM rlab.user WHERE id = %d", id);
        CHECK(mysql_query(m, sql) == 0);
        MYSQL_RES* res = mysql_store_result(m);
        MYSQL_ROW row = mysql_fetch_row(res);
        lat[i] = nowSeconds() - a;
        char want[32];
        snprintf(want, sizeof want, "name-%d", id);
        CHECK(row && strcmp(row[0], want) == 0);
        mysql_free_result(res);
    }
    report("MySQL 主键查询", lat, N, nowSeconds() - t0);

    t0 = nowSeconds();
    for (int i = 0; i < N; ++i) {
        double a = nowSeconds();
        CHECK(mysql_query(m, "SELECT 1") == 0);
        mysql_free_result(mysql_store_result(m));
        lat[i] = nowSeconds() - a;
    }
    report("MySQL SELECT 1", lat, N, nowSeconds() - t0);
    mysql_close(m);
    return 0;
}
