// 用 Redis 给 MySQL 做缓存（旁路缓存）：命中率和速度；缓存击穿、穿透、雪崩；先改数据库再删缓存时的竞争
//   cache aside        有缓存和没有缓存，各读 20 万次
//   cache stampede     热点 key 过期的瞬间，50 个线程同时去查数据库；加互斥锁以后
//   cache penetration  查询不存在的 id
//   cache avalanche    1 万个 key 同时过期，和过期时间加上随机值
//   cache race         读线程把旧值写回缓存（人为拉长时间窗口）
#include <mysql/mysql.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#define PRODUCTS 100000
static atomic_long g_dbQueries;        // 一共查了多少次数据库
static int g_slowQuery;                // 为真时每次查询多花 50 毫秒，模拟复杂的查询

static MYSQL* connectMysql(void)
{
    MYSQL* m = mysql_init(NULL);
    const char* h = getenv("MYSQL_HOST");
    if (!mysql_real_connect(m, h ? h : "127.0.0.1", "root", "lab", "rlab", 3306, NULL, 0)) {
        fprintf(stderr, "连接 MySQL 失败：%s\n", mysql_error(m));
        exit(1);
    }
    return m;
}

// 从数据库读一个商品：名字、价格、评论数、平均评分。不存在返回 0
static int dbLoad(MYSQL* m, int id, char* out, size_t n)
{
    char sql[512];
    snprintf(sql, sizeof sql,
             "SELECT p.name, p.price, COUNT(r.id), IFNULL(ROUND(AVG(r.stars), 2), 0)%s FROM product p "
             "LEFT JOIN review r ON r.product_id = p.id WHERE p.id = %d GROUP BY p.id",
             g_slowQuery ? ", SLEEP(0.05)" : "", id);
    atomic_fetch_add(&g_dbQueries, 1);
    CHECK(mysql_query(m, sql) == 0);
    MYSQL_RES* res = mysql_store_result(m);
    MYSQL_ROW row = mysql_fetch_row(res);
    int found = row != NULL;
    if (found) snprintf(out, n, "%s|%s|%s|%s", row[0], row[1], row[2], row[3]);
    mysql_free_result(res);
    return found;
}

// 旁路缓存：先查缓存，未命中再查数据库，并写入缓存。emptyTtl > 0 时，不存在的 id 也缓存一个空值
static int getProduct(redisContext* c, MYSQL* m, int id, int ttl, int emptyTtl, char* out, size_t n, int* hit)
{
    redisReply* r = cmd(c, "GET product:%d", id);
    if (r->type == REDIS_REPLY_STRING) {
        *hit = 1;
        int found = r->len > 0;                  // 空字符串表示「数据库中没有」
        snprintf(out, n, "%s", r->str);
        freeReplyObject(r);
        return found;
    }
    freeReplyObject(r);
    *hit = 0;
    int found = dbLoad(m, id, out, n);
    if (found) freeReplyObject(cmd(c, "SET product:%d %s EX %d", id, out, ttl));
    else if (emptyTtl > 0) freeReplyObject(cmd(c, "SET product:%d %s EX %d", id, "", emptyTtl));
    return found;
}

static void setupDb(void)
{
    MYSQL* m = connectMysql();
    MYSQL_RES* res;
    CHECK(mysql_query(m, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'rlab' AND table_name = 'review'") == 0);
    res = mysql_store_result(m);
    int exists = atoi(mysql_fetch_row(res)[0]);
    mysql_free_result(res);
    if (exists) {
        mysql_close(m);
        return;
    }
    printf("建表：%d 个商品，每个商品 10 条评论\n", PRODUCTS);
    CHECK(mysql_query(m, "CREATE TABLE product (id INT PRIMARY KEY, name VARCHAR(64) NOT NULL, price INT NOT NULL)") == 0);
    CHECK(mysql_query(m, "CREATE TABLE review (id INT AUTO_INCREMENT PRIMARY KEY, product_id INT NOT NULL, stars TINYINT NOT NULL, "
                         "KEY (product_id))") == 0);
    char sql[256];
    CHECK(mysql_query(m, "START TRANSACTION") == 0);
    for (int i = 0; i < PRODUCTS; ++i) {
        snprintf(sql, sizeof sql, "INSERT INTO product VALUES (%d, 'product-%d', %d)", i, i, randInt(100, 99999));
        CHECK(mysql_query(m, sql) == 0);
        snprintf(sql, sizeof sql, "INSERT INTO review (product_id, stars) VALUES (%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),"
                                  "(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d)",
                 i, randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5), i,
                 randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5), i, randInt(1, 5));
        CHECK(mysql_query(m, sql) == 0);
    }
    CHECK(mysql_query(m, "COMMIT") == 0);
    mysql_close(m);
}

static void runAsideThreads(int* ids);

/* ---------- 1. 有缓存和没有缓存 ---------- */

static int zipfId(const double* cdf, double sum)
{
    double u = (double)(nextRand() >> 11) / 9007199254740992.0 * sum;
    int lo = 0, hi = PRODUCTS - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cdf[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    return (int)((unsigned)lo * 2654435761u % PRODUCTS);
}

static void runAside(void)
{
    int n = 200000;
    double* cdf = malloc(sizeof(double) * PRODUCTS);
    double sum = 0;
    for (int r = 0; r < PRODUCTS; ++r) cdf[r] = (sum += 1.0 / (r + 1));
    int* ids = malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; ++i) ids[i] = zipfId(cdf, sum);

    MYSQL* m = connectMysql();
    redisContext* c = connectRedis();
    char buf[256], buf2[256];
    for (int i = 0; i < PRODUCTS; ++i) dbLoad(m, i, buf, sizeof buf);   // 预热：数据都在 InnoDB 的缓冲池中
    atomic_store(&g_dbQueries, 0);

    double t0 = nowSeconds();
    for (int i = 0; i < n; ++i) CHECK(dbLoad(m, ids[i], buf, sizeof buf));
    double tDb = nowSeconds() - t0;
    printf("只用 MySQL：      %d 次读 %.2f 秒，平均 %5.1f 微秒，查数据库 %ld 次\n", n, tDb, tDb / n * 1e6,
           atomic_load(&g_dbQueries));

    freeReplyObject(cmd(c, "FLUSHALL"));
    atomic_store(&g_dbQueries, 0);
    int hits = 0, hit;
    t0 = nowSeconds();
    for (int i = 0; i < n; ++i) {
        CHECK(getProduct(c, m, ids[i], 3600, 0, buf, sizeof buf, &hit));
        hits += hit;
    }
    double tCache = nowSeconds() - t0;
    printf("Redis + MySQL：   %d 次读 %.2f 秒，平均 %5.1f 微秒，查数据库 %ld 次，命中率 %.1f%%\n", n, tCache,
           tCache / n * 1e6, atomic_load(&g_dbQueries), hits * 100.0 / n);

    // 只统计命中的那些读：缓存中的值和数据库中的一致
    for (int i = 0; i < 1000; ++i) {
        CHECK(getProduct(c, m, ids[i], 3600, 0, buf, sizeof buf, &hit));
        CHECK(dbLoad(m, ids[i], buf2, sizeof buf2) && strcmp(buf, buf2) == 0);
    }
    printf("抽查 1000 个：缓存中的值和数据库一致\n");
    runAsideThreads(ids);
    free(ids);
    free(cdf);
    redisFree(c);
    mysql_close(m);
}

// 多个线程同时读，看总吞吐量：数据库的 CPU 成为瓶颈时，缓存的作用才明显
static int* g_ids;
static int g_useCache;
static void* asideReader(void* arg)
{
    int from = *(int*)arg;
    redisContext* c = connectRedis();
    MYSQL* m = connectMysql();
    char buf[256];
    int hit;
    for (int i = from; i < from + 25000; ++i) {
        if (g_useCache) CHECK(getProduct(c, m, g_ids[i], 3600, 0, buf, sizeof buf, &hit));
        else CHECK(dbLoad(m, g_ids[i], buf, sizeof buf));
    }
    redisFree(c);
    mysql_close(m);
    return NULL;
}

static void runAsideThreads(int* ids)
{
    g_ids = ids;
    for (g_useCache = 0; g_useCache <= 1; ++g_useCache) {
        redisContext* c = connectRedis();
        freeReplyObject(cmd(c, "FLUSHALL"));
        redisFree(c);
        atomic_store(&g_dbQueries, 0);
        pthread_t t[8];
        int from[8];
        double t0 = nowSeconds();
        for (int i = 0; i < 8; ++i) {
            from[i] = i * 25000;
            pthread_create(&t[i], NULL, asideReader, &from[i]);
        }
        for (int i = 0; i < 8; ++i) pthread_join(t[i], NULL);
        double sec = nowSeconds() - t0;
        printf("8 个线程%s：20 万次读 %.2f 秒，%6.0f 次/秒，查数据库 %ld 次\n", g_useCache ? "，Redis + MySQL" : "，只用 MySQL  ",
               sec, 200000 / sec, atomic_load(&g_dbQueries));
    }
}

/* ---------- 2. 缓存击穿：热点 key 过期 ---------- */

static atomic_int g_go;
static int g_useLock;

// 带互斥锁的读：未命中时只有拿到锁的线程查数据库，其他线程稍等再读缓存
static void getHot(redisContext* c, MYSQL* m, char* buf, size_t n)
{
    for (;;) {
        redisReply* r = cmd(c, "GET product:0");
        int hit = r->type == REDIS_REPLY_STRING;
        freeReplyObject(r);
        if (hit) return;
        if (!g_useLock) {
            dbLoad(m, 0, buf, n);
            freeReplyObject(cmd(c, "SET product:0 %s EX 2", buf));
            return;
        }
        r = cmd(c, "SET lock:product:0 1 NX PX 3000");   // 锁带过期时间：拿到锁的线程崩溃了，锁也会自动释放
        int got = r->type == REDIS_REPLY_STATUS;
        freeReplyObject(r);
        if (got) {
            dbLoad(m, 0, buf, n);
            freeReplyObject(cmd(c, "SET product:0 %s EX 2", buf));
            freeReplyObject(cmd(c, "DEL lock:product:0"));
            return;
        }
        usleep(10000);                                  // 别人正在查，10 毫秒后再看缓存
    }
}

static void* hotReader(void* arg)
{
    double seconds = *(double*)arg;
    redisContext* c = connectRedis();
    MYSQL* m = connectMysql();
    char buf[256];
    while (!atomic_load(&g_go)) usleep(100);
    double t0 = nowSeconds();
    while (nowSeconds() - t0 < seconds) getHot(c, m, buf, sizeof buf);
    redisFree(c);
    mysql_close(m);
    return NULL;
}

static void runStampede(void)
{
    g_slowQuery = 1;                   // 每次查询 50 毫秒
    for (g_useLock = 0; g_useLock <= 1; ++g_useLock) {
        redisContext* c = connectRedis();
        freeReplyObject(cmd(c, "FLUSHALL"));
        redisFree(c);
        atomic_store(&g_dbQueries, 0);
        atomic_store(&g_go, 0);
        double seconds = 7;            // 缓存 2 秒过期，7 秒内会过期 3 次左右
        pthread_t t[50];
        for (int i = 0; i < 50; ++i) pthread_create(&t[i], NULL, hotReader, &seconds);
        usleep(300000);
        atomic_store(&g_go, 1);
        for (int i = 0; i < 50; ++i) pthread_join(t[i], NULL);
        printf("50 个线程读同一个热点 key 7 秒（缓存 2 秒过期，查一次数据库 50 毫秒）%s：查数据库 %ld 次\n",
               g_useLock ? "，加互斥锁" : "，不加锁  ", atomic_load(&g_dbQueries));
    }
    g_slowQuery = 0;
}

/* ---------- 3. 缓存穿透：查询不存在的数据 ---------- */

static void runPenetration(void)
{
    redisContext* c = connectRedis();
    MYSQL* m = connectMysql();
    char buf[256];
    int hit;
    for (int emptyTtl = 0; emptyTtl <= 60; emptyTtl += 60) {
        freeReplyObject(cmd(c, "FLUSHALL"));
        atomic_store(&g_dbQueries, 0);
        for (int i = 0; i < 100000; ++i) CHECK(!getProduct(c, m, PRODUCTS + randInt(0, 99), 3600, emptyTtl, buf, sizeof buf, &hit));
        printf("反复查询 100 个不存在的 id，共 10 万次%s：查数据库 %ld 次\n", emptyTtl ? "，缓存空值 60 秒" : "，不缓存空值   ",
               atomic_load(&g_dbQueries));
    }
    redisFree(c);
    mysql_close(m);
}

/* ---------- 4. 缓存雪崩：大量 key 同时过期 ---------- */

static atomic_long g_perSlot[40];      // 每 0.25 秒一格，统计查数据库的次数
static double g_start;

static void* avalancheReader(void* arg)
{
    (void)arg;
    redisContext* c = connectRedis();
    MYSQL* m = connectMysql();
    char buf[256];
    int hit;
    while (nowSeconds() - g_start < 10) {
        getProduct(c, m, randInt(0, 9999), 3600, 0, buf, sizeof buf, &hit);
        int slot = (int)((nowSeconds() - g_start) / 0.25);
        if (!hit && slot < 40) atomic_fetch_add(&g_perSlot[slot], 1);   // 未命中：这次读查了数据库
    }
    redisFree(c);
    mysql_close(m);
    return NULL;
}

static void runAvalanche(void)
{
    for (int jitter = 0; jitter <= 1; ++jitter) {
        redisContext* c = connectRedis();
        MYSQL* m = connectMysql();
        freeReplyObject(cmd(c, "FLUSHALL"));
        char buf[256];
        for (int i = 0; i < 10000; ++i) {      // 预热：1 万个商品一起放进缓存，过期时间 5 秒，或者 3～7 秒随机
            dbLoad(m, i, buf, sizeof buf);
            int ttl = jitter ? randInt(3000, 7000) : 5000;
            freeReplyObject(cmd(c, "SET product:%d %s PX %d", i, buf, ttl));
        }
        redisFree(c);
        mysql_close(m);
        for (int i = 0; i < 40; ++i) atomic_store(&g_perSlot[i], 0);
        g_start = nowSeconds();
        pthread_t t[8];
        for (int i = 0; i < 8; ++i) pthread_create(&t[i], NULL, avalancheReader, NULL);
        for (int i = 0; i < 8; ++i) pthread_join(t[i], NULL);
        printf("%s：每 0.25 秒查数据库的次数\n ", jitter ? "过期时间 3～7 秒随机" : "过期时间都是 5 秒");
        long total = 0, peak = 0;
        for (int i = 0; i < 40; ++i) {
            long v = atomic_load(&g_perSlot[i]);
            printf(" %ld", v);
            total += v;
            if (v > peak) peak = v;
        }
        printf("\n  一共 %ld 次，最多的 0.25 秒中 %ld 次\n", total, peak);
    }
}

/* ---------- 5. 先更新数据库，再删除缓存：仍然可能读到旧值 ---------- */

static void runRace(void)
{
    redisContext* c = connectRedis();
    MYSQL* m = connectMysql();
    char buf[256];
    freeReplyObject(cmd(c, "DEL product:1"));
    CHECK(mysql_query(m, "UPDATE product SET price = 100 WHERE id = 1") == 0);

    // 读线程：缓存未命中，从数据库读到 price = 100；在写回缓存之前「卡住」了 200 毫秒（比如 GC、调度）
    dbLoad(m, 1, buf, sizeof buf);
    printf("读请求：缓存未命中，从数据库读到 %s，准备写回缓存……\n", buf);
    // 这 200 毫秒中，写请求更新了数据库，并删除了缓存
    CHECK(mysql_query(m, "UPDATE product SET price = 200 WHERE id = 1") == 0);
    freeReplyObject(cmd(c, "DEL product:1"));
    printf("写请求：数据库改成 200，删除缓存\n");
    // 读请求恢复，把旧值写进了缓存
    freeReplyObject(cmd(c, "SET product:1 %s EX 3600", buf));
    redisReply* r = cmd(c, "GET product:1");
    char now[256];
    dbLoad(m, 1, now, sizeof now);
    printf("读请求：写回缓存。现在缓存中是 %s，数据库中是 %s\n", r->str, now);
    freeReplyObject(r);
    // 延迟双删：写请求过一会儿再删一次
    usleep(500000);
    freeReplyObject(cmd(c, "DEL product:1"));
    int hit;
    getProduct(c, m, 1, 3600, 0, buf, sizeof buf, &hit);
    printf("写请求 500 毫秒后再删一次缓存（延迟双删）。再读：%s，%s\n", buf, hit ? "缓存命中" : "缓存未命中，从数据库读");
    redisFree(c);
    mysql_close(m);
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "aside";
    setupDb();
    if (strcmp(mode, "aside") == 0) runAside();
    else if (strcmp(mode, "stampede") == 0) runStampede();
    else if (strcmp(mode, "penetration") == 0) runPenetration();
    else if (strcmp(mode, "avalanche") == 0) runAvalanche();
    else if (strcmp(mode, "race") == 0) runRace();
    return 0;
}
