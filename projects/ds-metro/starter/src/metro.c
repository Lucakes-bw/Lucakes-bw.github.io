#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metro.h"

// 里程碑 1：读取数据文件，建立地铁图。
// 数据格式见 data/metro.txt 开头的注释，需要检查的错误见项目文章中的表格。

// 写入错误信息：lineno > 0 时带上行号。返回 -1，可以直接 return loadError(...)。
// 例如 loadError(err, errSize, 3, "车站 %s 缺少运行时间", "乙") 写入
//   数据错误（第 3 行）：车站 乙 缺少运行时间
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

// 建议按下面的顺序，把工作拆成几个小函数：
//   getOrAddStation：按名字找到车站，不存在就新建一个（同时加到 stationIndex 中），返回编号
//   addLine：新建一条线路（同时加到 lineIndex 中），返回编号
//   appendStation：把车站加到线路的末尾；不是首站时，和上一站之间连一条无向边（两条有向边）
//   parse：逐行读取文件，识别 LINE、END 和车站，检查格式错误
// 数组的容量不够时用 realloc 翻倍，参考《顺序表》。

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

    // TODO：读取并解析文件。失败时不要忘记 fclose 和 metroFree
    int r = loadError(err, errSize, 0, "metroLoad 还没有实现");

    fclose(fp);
    if (r != 0) metroFree(m);
    return r;
}

void metroFree(Metro* m)
{
    // TODO：释放每个车站的名字和邻接表、每条线路的名字和数组，再释放两个哈希表
    hmFree(&m->stationIndex);
    hmFree(&m->lineIndex);
    memset(m, 0, sizeof *m);
}

int metroFindStation(const Metro* m, const char* name)
{
    // TODO：用 stationIndex 查找
    (void)m; (void)name;
    return -1;
}

int metroFindLine(const Metro* m, const char* name)
{
    // TODO：用 lineIndex 查找
    (void)m; (void)name;
    return -1;
}
