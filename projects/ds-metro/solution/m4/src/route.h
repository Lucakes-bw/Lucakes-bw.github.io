#ifndef ROUTE_H
#define ROUTE_H

#include "metro.h"

#define TRANSFER_MINUTES 5                      // 换乘一次额外花的时间（走通道、等车），单位是分钟

// 一条路线：依次经过的车站，以及每一段坐的线路
typedef struct {
    int* stations;                              // stations[0] 是起点，stations[n-1] 是终点
    int* lines;                                 // lines[i]（i >= 1）：从 stations[i-1] 到 stations[i] 坐的线路；lines[0] 不使用
    int n;                                      // 车站的个数，坐了 n-1 站
} Route;

// 下面三个函数都要求 from != to（main.c 已经检查过）。
// 找到路线时填写 *r 并返回 0，r 中的数组用 malloc 分配，由调用者用 routeFree 释放；
// 从 from 无法到达 to 时返回 -1，不分配内存。
// （起始代码中还没有实现的函数返回 -2，main.c 会提示「还没有实现」。）
int routeFewestStops(const Metro* m, int from, int to, Route* r);      // 里程碑 2：站数最少
int routeFastest(const Metro* m, int from, int to, Route* r);          // 里程碑 3：时间最短
int routeFewestTransfers(const Metro* m, int from, int to, Route* r);  // 里程碑 4：换乘最少，次数相同时时间最短

// 里程碑 3：从 from 出发，到每个车站的最短时间（含换乘），写到 minutes[0..nstations-1] 中。
// minutes[from] = 0，无法到达的车站是 -1。返回 0。
int routeAllTimes(const Metro* m, int from, int* minutes);

void routeFree(Route* r);

#endif
