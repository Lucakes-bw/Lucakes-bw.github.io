// 命令行界面：读取命令，调用 metro.c 中的函数，按固定的格式输出结果。
// 这个文件已经写好了，一般不需要修改。
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "metro.h"

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

static void cmdHelp(const Metro* m, const char* arg);

typedef struct {
    const char* name;
    const char* usage;                          // NULL 表示不需要参数
    const char* help;
    void (*run)(const Metro* m, const char* arg);
} Command;

static const Command commands[] = {
    {"stats",   NULL,             "线路、车站、区间、换乘站的数量", cmdStats},
    {"line",    "line <线路名>",   "按顺序列出一条线路的所有车站",   cmdLine},
    {"station", "station <站名>",  "车站所在的线路和相邻的车站",     cmdStation},
    {"help",    NULL,             "显示这个帮助",                  cmdHelp},
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
    metroFree(&m);
    return 0;
}
