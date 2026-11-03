#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metro.h"

// 写入错误信息：lineno > 0 时带上行号
static int loadError(char* err, size_t errSize, int lineno, const char* fmt, ...)
{
    int n = lineno > 0 ? snprintf(err, errSize, "数据错误（第 %d 行）：", lineno)
                       : snprintf(err, errSize, "数据错误：");
    if (n < 0 || (size_t)n >= errSize) return -1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err + n, errSize - n, fmt, ap);
    va_end(ap);
    return -1;
}

static char* copyString(const char* s)
{
    char* p = malloc(strlen(s) + 1);
    if (p == NULL) exit(1);
    strcpy(p, s);
    return p;
}

// 保证数组 *a 至少能放下 need 个元素，容量不够时翻倍
static void reserve(void** a, int* cap, int need, size_t elemSize)
{
    if (need <= *cap) return;
    int nc = *cap ? *cap * 2 : 4;
    while (nc < need) nc *= 2;
    void* p = realloc(*a, nc * elemSize);
    if (p == NULL) exit(1);
    *a = p;
    *cap = nc;
}

// 按名字找到车站，不存在就新建一个，返回车站编号
static int getOrAddStation(Metro* m, const char* name)
{
    int id = hmGet(&m->stationIndex, name);
    if (id >= 0) return id;
    reserve((void**)&m->stations, &m->capStations, m->nstations + 1, sizeof(Station));
    id = m->nstations++;
    m->stations[id] = (Station){copyString(name), NULL, 0, 0};
    hmPut(&m->stationIndex, name, id);
    return id;
}

static void addArc(Metro* m, int from, int to, int line, int minutes)
{
    Station* st = &m->stations[from];
    reserve((void**)&st->edges, &st->capEdges, st->nedges + 1, sizeof(Edge));
    st->edges[st->nedges++] = (Edge){to, line, minutes};
}

static int addLine(Metro* m, const char* name)
{
    reserve((void**)&m->lines, &m->capLines, m->nlines + 1, sizeof(Line));
    int id = m->nlines++;
    m->lines[id] = (Line){copyString(name), NULL, NULL, 0, 0};
    hmPut(&m->lineIndex, name, id);
    return id;
}

// 把车站 s 加到线路末尾；不是首站时，和上一站之间连一条无向边
static void appendStation(Metro* m, int lineId, int s, int minutes)
{
    Line* ln = &m->lines[lineId];
    if (ln->nstations == ln->capStations) {     // stations 和 minutes 一起扩容
        int nc = ln->capStations ? ln->capStations * 2 : 4;
        int* ps = realloc(ln->stations, nc * sizeof(int));
        if (ps == NULL) exit(1);
        ln->stations = ps;
        int* pm = realloc(ln->minutes, nc * sizeof(int));
        if (pm == NULL) exit(1);
        ln->minutes = pm;
        ln->capStations = nc;
    }
    ln->stations[ln->nstations] = s;
    ln->minutes[ln->nstations] = minutes;
    if (ln->nstations > 0) {
        int prev = ln->stations[ln->nstations - 1];
        addArc(m, prev, s, lineId, minutes);
        addArc(m, s, prev, lineId, minutes);
        ++m->nsegments;
    }
    ++ln->nstations;
}

static int lineHasStation(const Line* ln, int s)
{
    for (int i = 0; i < ln->nstations; ++i) {
        if (ln->stations[i] == s) return 1;
    }
    return 0;
}

static int parseMinutes(const char* s)          // 正整数返回它的值，否则返回 -1
{
    if (*s == '\0') return -1;
    long v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
        if (v > 100000) return -1;
    }
    return v > 0 ? (int)v : -1;
}

static int parse(Metro* m, FILE* fp, char* err, size_t errSize)
{
    char buf[1024];
    int lineno = 0;
    int cur = -1;                               // 正在读取的线路，-1 表示不在 LINE 和 END 之间
    while (fgets(buf, sizeof buf, fp)) {
        ++lineno;
        const char* delim = " \t\r\n";
        char* tok = strtok(buf, delim);
        if (tok == NULL || tok[0] == '#') continue;          // 空行和注释
        char* arg = strtok(NULL, delim);
        char* extra = arg ? strtok(NULL, delim) : NULL;

        if (strcmp(tok, "LINE") == 0) {
            if (cur >= 0) return loadError(err, errSize, lineno, "线路 %s 缺少 END", m->lines[cur].name);
            if (arg == NULL) return loadError(err, errSize, lineno, "LINE 后面缺少线路名");
            if (extra) return loadError(err, errSize, lineno, "多余的内容：%s", extra);
            if (metroFindLine(m, arg) >= 0) return loadError(err, errSize, lineno, "线路重复：%s", arg);
            cur = addLine(m, arg);
        } else if (strcmp(tok, "END") == 0) {
            if (cur < 0) return loadError(err, errSize, lineno, "END 前面没有 LINE");
            if (arg) return loadError(err, errSize, lineno, "多余的内容：%s", arg);
            if (m->lines[cur].nstations < 2)
                return loadError(err, errSize, lineno, "线路 %s 至少要有 2 个车站", m->lines[cur].name);
            cur = -1;
        } else {
            if (cur < 0) return loadError(err, errSize, lineno, "车站 %s 不在任何线路中", tok);
            if (extra) return loadError(err, errSize, lineno, "多余的内容：%s", extra);
            int minutes = 0;
            if (m->lines[cur].nstations == 0) {
                if (arg) return loadError(err, errSize, lineno, "首站 %s 不能有运行时间", tok);
            } else {
                if (arg == NULL) return loadError(err, errSize, lineno, "车站 %s 缺少运行时间", tok);
                minutes = parseMinutes(arg);
                if (minutes < 0) return loadError(err, errSize, lineno, "运行时间必须是正整数：%s", arg);
            }
            int s = getOrAddStation(m, tok);
            if (lineHasStation(&m->lines[cur], s))
                return loadError(err, errSize, lineno, "车站 %s 在线路 %s 中重复", tok, m->lines[cur].name);
            appendStation(m, cur, s, minutes);
        }
    }
    if (cur >= 0) return loadError(err, errSize, 0, "线路 %s 缺少 END", m->lines[cur].name);
    if (m->nlines == 0) return loadError(err, errSize, 0, "没有任何线路");
    return 0;
}

int metroLoad(Metro* m, const char* path, char* err, size_t errSize)
{
    memset(m, 0, sizeof *m);
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        snprintf(err, errSize, "无法打开文件：%s", path);
        return -1;
    }
    hmInit(&m->stationIndex);
    hmInit(&m->lineIndex);
    int r = parse(m, fp, err, errSize);
    fclose(fp);
    if (r != 0) metroFree(m);
    return r;
}

void metroFree(Metro* m)
{
    for (int i = 0; i < m->nstations; ++i) {
        free(m->stations[i].name);
        free(m->stations[i].edges);
    }
    for (int i = 0; i < m->nlines; ++i) {
        free(m->lines[i].name);
        free(m->lines[i].stations);
        free(m->lines[i].minutes);
    }
    free(m->stations);
    free(m->lines);
    hmFree(&m->stationIndex);
    hmFree(&m->lineIndex);
    memset(m, 0, sizeof *m);
}

int metroFindStation(const Metro* m, const char* name)
{
    return hmGet(&m->stationIndex, name);
}

int metroFindLine(const Metro* m, const char* name)
{
    return hmGet(&m->lineIndex, name);
}
