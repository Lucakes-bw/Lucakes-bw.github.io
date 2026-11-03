#ifndef METRO_H
#define METRO_H

#include <stddef.h>
#include "hashmap.h"

// 一条边：从当前车站出发，沿着某条线路到相邻的车站
typedef struct {
    int to;                                     // 相邻车站的编号
    int line;                                   // 线路的编号
    int minutes;                                // 运行时间（分钟）
} Edge;

typedef struct {
    char* name;
    Edge* edges;                                // 邻接表，按添加的顺序保存
    int nedges, capEdges;
} Station;

typedef struct {
    char* name;
    int* stations;                              // 按顺序排列的车站编号
    int* minutes;                               // minutes[i]：从第 i-1 站到第 i 站的时间，minutes[0] = 0
    int nstations, capStations;
} Line;

typedef struct {
    Station* stations;                          // 车站的编号就是它在这个数组中的下标
    int nstations, capStations;
    Line* lines;                                // 线路的编号就是它在这个数组中的下标
    int nlines, capLines;
    int nsegments;                              // 区间（相邻两站之间的一段）的个数
    HashMap stationIndex;                       // 站名 -> 车站编号
    HashMap lineIndex;                          // 线路名 -> 线路编号
} Metro;

// 从数据文件读取线路，建立地铁图。
// 成功返回 0；失败返回 -1，把错误信息写到 err 中，并释放已经分配的内存。
int metroLoad(Metro* m, const char* path, char* err, size_t errSize);
void metroFree(Metro* m);

int metroFindStation(const Metro* m, const char* name);   // 找不到时返回 -1
int metroFindLine(const Metro* m, const char* name);      // 找不到时返回 -1

#endif
