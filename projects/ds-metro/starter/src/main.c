// 命令行界面：读取命令，调用 metro.c 中的函数，按固定的格式输出结果。
// 这个文件已经写好了，一般不需要修改。
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "metro.h"
#include "route.h"
#include "search.h"

// 把车站 s 所在的线路编号写到 out 中，返回线路的条数。
// 邻接表按添加的顺序保存，同一条线路的边是连在一起的，所以只要和前一个比较就能去重。
static int stationLines(const Metro* m, int s, int* out)
{
    const Station* st = &m->stations[s];
    int n = 0;
    for (int i = 0; i < st->nedges; ++i) {
        if (n == 0 || out[n - 1] != st->edges[i].line) out[n++] = st->edges[i].line;
    }
    return n;
}

static void cmdStats(const Metro* m, const char* arg)
{
    (void)arg;
    int* lines = malloc(sizeof(int) * (m->nlines + 1));
    if (lines == NULL) exit(1);
    int transfers = 0;
    for (int s = 0; s < m->nstations; ++s) {
        if (stationLines(m, s, lines) >= 2) ++transfers;
    }
    free(lines);
    printf("线路 %d 条，车站 %d 个，区间 %d 个，换乘站 %d 个\n",
           m->nlines, m->nstations, m->nsegments, transfers);
}

static void cmdLine(const Metro* m, const char* arg)
{
    int id = metroFindLine(m, arg);
    if (id < 0) { printf("找不到线路：%s\n", arg); return; }
    const Line* ln = &m->lines[id];
    int total = 0;
    for (int i = 0; i < ln->nstations; ++i) total += ln->minutes[i];
    printf("%s：共 %d 站，全程 %d 分钟\n", ln->name, ln->nstations, total);
    for (int i = 0; i < ln->nstations; ++i) {
        if (i > 0) printf(" -%d- ", ln->minutes[i]);
        printf("%s", m->stations[ln->stations[i]].name);
    }
    printf("\n");
}

static void cmdStation(const Metro* m, const char* arg)
{
    int s = metroFindStation(m, arg);
    if (s < 0) { printf("找不到车站：%s\n", arg); return; }
    const Station* st = &m->stations[s];
    int* lines = malloc(sizeof(int) * (m->nlines + 1));
    if (lines == NULL) exit(1);
    int n = stationLines(m, s, lines);
    printf("%s：", st->name);
    for (int i = 0; i < n; ++i) printf("%s%s", i ? "、" : "", m->lines[lines[i]].name);
    printf("%s\n", n >= 2 ? "（换乘站）" : "");
    for (int i = 0; i < n; ++i) {
        printf("  %s：", m->lines[lines[i]].name);
        int first = 1;
        for (int k = 0; k < st->nedges; ++k) {
            const Edge* e = &st->edges[k];
            if (e->line != lines[i]) continue;
            printf("%s%s %d 分钟", first ? "" : "、", m->stations[e->to].name, e->minutes);
            first = 0;
        }
        printf("\n");
    }
    free(lines);
}

static NameIndex names;                         // 里程碑 5：按站名排序的索引，main 中建立

// 把参数拆成两个站名，查出编号。出错时输出原因，返回 -1
static int twoStations(const Metro* m, const char* arg, const char* usage, int* from, int* to)
{
    char a[256], b[256], extra;
    if (sscanf(arg, "%255s %255s %c", a, b, &extra) != 2) { printf("用法：%s\n", usage); return -1; }
    *from = metroFindStation(m, a);
    *to = metroFindStation(m, b);
    if (*from < 0) { printf("找不到车站：%s\n", a); return -1; }
    if (*to < 0) { printf("找不到车站：%s\n", b); return -1; }
    if (*from == *to) { printf("起点和终点是同一个车站：%s\n", a); return -1; }
    return 0;
}

// 从 a 沿着线路 line 到相邻车站 b 的运行时间；a 和 b 在这条线路上不相邻时返回 -1
static int hopMinutes(const Metro* m, int a, int b, int line)
{
    const Station* st = &m->stations[a];
    for (int i = 0; i < st->nedges; ++i) {
        if (st->edges[i].to == b && st->edges[i].line == line) return st->edges[i].minutes;
    }
    return -1;
}

// 检查路线：起点、终点是否正确，每一段是不是真的在那条线路上相邻。有错误时输出原因，返回 -1
static int checkRoute(const Metro* m, const Route* r, int from, int to)
{
    if (r->n < 2 || r->stations == NULL || r->lines == NULL) { printf("路线有误：至少要有 2 个车站\n"); return -1; }
    for (int i = 0; i < r->n; ++i) {
        if (r->stations[i] < 0 || r->stations[i] >= m->nstations) { printf("路线有误：stations[%d] = %d 不是车站编号\n", i, r->stations[i]); return -1; }
        if (i > 0 && (r->lines[i] < 0 || r->lines[i] >= m->nlines)) { printf("路线有误：lines[%d] = %d 不是线路编号\n", i, r->lines[i]); return -1; }
    }
    if (r->stations[0] != from || r->stations[r->n - 1] != to) {
        printf("路线有误：应该从 %s 到 %s，实际是从 %s 到 %s\n", m->stations[from].name, m->stations[to].name,
               m->stations[r->stations[0]].name, m->stations[r->stations[r->n - 1]].name);
        return -1;
    }
    for (int i = 1; i < r->n; ++i) {
        if (hopMinutes(m, r->stations[i - 1], r->stations[i], r->lines[i]) < 0) {
            printf("路线有误：%s 和 %s 在 %s 上不相邻\n", m->stations[r->stations[i - 1]].name,
                   m->stations[r->stations[i]].name, m->lines[r->lines[i]].name);
            return -1;
        }
    }
    return 0;
}

// 输出一条路线：先是总的站数、换乘次数和用时，再按线路分段
static void printRoute(const Metro* m, const Route* r)
{
    int ride = 0, transfers = 0;
    for (int i = 1; i < r->n; ++i) {
        ride += hopMinutes(m, r->stations[i - 1], r->stations[i], r->lines[i]);
        if (i > 1 && r->lines[i] != r->lines[i - 1]) ++transfers;
    }
    printf("%s → %s：坐 %d 站，", m->stations[r->stations[0]].name, m->stations[r->stations[r->n - 1]].name, r->n - 1);
    if (transfers == 0) printf("不用换乘，用时 %d 分钟\n", ride);
    else printf("换乘 %d 次，用时 %d 分钟（乘车 %d 分钟，换乘 %d 分钟）\n",
                transfers, ride + transfers * TRANSFER_MINUTES, ride, transfers * TRANSFER_MINUTES);
    for (int i = 1; i < r->n; ) {                   // 每一段：从第 i-1 站开始，坐同一条线路的连续几站
        int j = i, minutes = 0;
        while (j < r->n && r->lines[j] == r->lines[i]) {
            minutes += hopMinutes(m, r->stations[j - 1], r->stations[j], r->lines[j]);
            ++j;
        }
        printf("  %s：%s → %s，%d 站，%d 分钟\n", m->lines[r->lines[i]].name,
               m->stations[r->stations[i - 1]].name, m->stations[r->stations[j - 1]].name, j - i, minutes);
        i = j;
    }
}

static void runRoute(const Metro* m, const char* arg, const char* usage, const char* fname,
                     int (*find)(const Metro*, int, int, Route*))
{
    int from, to;
    if (twoStations(m, arg, usage, &from, &to) != 0) return;
    Route r = {NULL, NULL, 0};
    int ret = find(m, from, to, &r);
    if (ret == -2) { printf("%s 还没有实现\n", fname); return; }
    if (ret != 0) { printf("从 %s 无法到达 %s\n", m->stations[from].name, m->stations[to].name); return; }
    if (checkRoute(m, &r, from, to) == 0) printRoute(m, &r);
    routeFree(&r);
}

static void cmdStops(const Metro* m, const char* arg)
{
    runRoute(m, arg, "stops <起点> <终点>", "routeFewestStops", routeFewestStops);
}

static void cmdFastest(const Metro* m, const char* arg)
{
    runRoute(m, arg, "fastest <起点> <终点>", "routeFastest", routeFastest);
}

static void cmdTransfers(const Metro* m, const char* arg)
{
    runRoute(m, arg, "transfers <起点> <终点>", "routeFewestTransfers", routeFewestTransfers);
}

static const int* sortKey;                      // qsort 的比较函数不能带参数，只好用一个全局变量
static int byMinutes(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;
    int kx = sortKey[x] < 0 ? 1 << 30 : sortKey[x], ky = sortKey[y] < 0 ? 1 << 30 : sortKey[y];   // 无法到达的排在最后
    if (kx != ky) return kx < ky ? -1 : 1;
    return x - y;
}

static void cmdFrom(const Metro* m, const char* arg)
{
    int s = metroFindStation(m, arg);
    if (s < 0) { printf("找不到车站：%s\n", arg); return; }
    int* minutes = malloc(sizeof(int) * m->nstations);
    int* order = malloc(sizeof(int) * m->nstations);
    if (minutes == NULL || order == NULL) exit(1);
    for (int i = 0; i < m->nstations; ++i) { minutes[i] = -1; order[i] = i; }
    if (routeAllTimes(m, s, minutes) == -2) {
        printf("routeAllTimes 还没有实现\n");
    } else {
        sortKey = minutes;
        qsort(order, m->nstations, sizeof(int), byMinutes);
        printf("从 %s 出发到各站的最短用时（换乘一次 %d 分钟）：\n", m->stations[s].name, TRANSFER_MINUTES);
        for (int i = 0; i < m->nstations; ++i) {
            int t = order[i];
            if (t == s) continue;
            if (minutes[t] < 0) printf("  %s 无法到达\n", m->stations[t].name);
            else printf("  %s %d 分钟\n", m->stations[t].name, minutes[t]);
        }
    }
    free(minutes);
    free(order);
}

static void cmdSearch(const Metro* m, const char* arg)
{
    int first = 0;
    int n = nameIndexPrefix(&names, m, arg, &first);
    if (n == -2) { printf("nameIndexPrefix 还没有实现\n"); return; }
    if (n == 0) { printf("没有以 %s 开头的车站\n", arg); return; }
    for (int i = 0; i < n; ++i) printf("%s%s", i ? "、" : "", m->stations[names.order[first + i]].name);
    printf("（%d 个）\n", n);
}

static void cmdTop(const Metro* m, const char* arg)
{
    char* end;
    long k = strtol(arg, &end, 10);
    if (*end != '\0' || k <= 0) { printf("用法：top <个数>\n"); return; }
    if (k > m->nstations) k = m->nstations;
    int* out = malloc(sizeof(int) * k);
    int* minutes = malloc(sizeof(int) * m->nstations);
    if (out == NULL || minutes == NULL) exit(1);
    int n = rankCentral(m, (int)k, out);
    if (n == -2) {
        printf("rankCentral 还没有实现\n");
    } else {
        printf("到其他车站平均用时最短的 %d 个车站：\n", n);
        for (int i = 0; i < n; ++i) {
            routeAllTimes(m, out[i], minutes);
            int reach = 0, total = 0;
            for (int t = 0; t < m->nstations; ++t) {
                if (t != out[i] && minutes[t] >= 0) { ++reach; total += minutes[t]; }
            }
            printf("  %d. %s  平均 %.1f 分钟", i + 1, m->stations[out[i]].name, reach ? (double)total / reach : 0.0);
            if (reach < m->nstations - 1) printf("（只能到达 %d 个车站）", reach);
            printf("\n");
        }
    }
    free(out);
    free(minutes);
}

static void cmdHelp(const Metro* m, const char* arg);

typedef struct {
    const char* name;
    const char* usage;                          // NULL 表示不需要参数
    const char* help;
    void (*run)(const Metro* m, const char* arg);
} Command;

static const Command commands[] = {
    {"stats",     NULL,                      "线路、车站、区间、换乘站的数量",           cmdStats},
    {"line",      "line <线路名>",           "按顺序列出一条线路的所有车站",             cmdLine},
    {"station",   "station <站名>",          "车站所在的线路和相邻的车站",               cmdStation},
    {"stops",     "stops <起点> <终点>",     "站数最少的路线（里程碑 2）",               cmdStops},
    {"fastest",   "fastest <起点> <终点>",   "时间最短的路线（里程碑 3）",               cmdFastest},
    {"from",      "from <站名>",             "到各站的最短用时（里程碑 3）",             cmdFrom},
    {"transfers", "transfers <起点> <终点>", "换乘最少的路线（里程碑 4）",               cmdTransfers},
    {"search",    "search <站名的开头>",     "列出站名以它开头的车站（里程碑 5）",       cmdSearch},
    {"top",       "top <个数>",              "到其他车站平均用时最短的车站（里程碑 5）", cmdTop},
    {"help",      NULL,                      "显示这个帮助",                             cmdHelp},
};
static const int ncommands = sizeof(commands) / sizeof(commands[0]);

static void cmdHelp(const Metro* m, const char* arg)
{
    (void)m; (void)arg;
    for (int i = 0; i < ncommands; ++i) {
        printf("  %s\n      %s\n", commands[i].usage ? commands[i].usage : commands[i].name, commands[i].help);
    }
    printf("  quit\n      退出\n");
}

static char* trim(char* s)
{
    while (*s == ' ' || *s == '\t') ++s;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) --end;
    *end = '\0';
    return s;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "用法：%s <数据文件>\n", argv[0]);
        return 1;
    }
    Metro m;
    char err[256];
    if (metroLoad(&m, argv[1], err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    nameIndexBuild(&names, &m);

    int interactive = isatty(STDIN_FILENO);     // 只在终端中显示提示符，测试时不显示
    char buf[1024];
    for (;;) {
        if (interactive) { printf("metro> "); fflush(stdout); }
        if (fgets(buf, sizeof buf, stdin) == NULL) break;
        char* cmd = trim(buf);
        if (*cmd == '\0' || *cmd == '#') continue;          // 空行和注释
        char* arg = cmd + strcspn(cmd, " \t");
        if (*arg) *arg++ = '\0';
        arg = trim(arg);
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) break;

        int found = 0;
        for (int i = 0; i < ncommands; ++i) {
            if (strcmp(cmd, commands[i].name) != 0) continue;
            found = 1;
            if (commands[i].usage && *arg == '\0') printf("用法：%s\n", commands[i].usage);
            else commands[i].run(&m, arg);
        }
        if (!found) printf("未知命令：%s（输入 help 查看帮助）\n", cmd);
    }
    nameIndexFree(&names);
    metroFree(&m);
    return 0;
}
