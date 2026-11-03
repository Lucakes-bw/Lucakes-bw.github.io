#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "route.h"

// 里程碑 2～4：三种最短路线。每个函数的要求见对应里程碑的文章。
// 还没有实现的函数返回 -2，main.c 会提示「还没有实现」；实现以后把 return -2 改掉。

// 里程碑 2：从 from 开始 BFS，按邻接表的顺序访问相邻车站，第一次发现一个车站时记录它的前驱和线路。
int routeFewestStops(const Metro* m, int from, int to, Route* r)
{
    // TODO
    (void)m; (void)from; (void)to; (void)r;
    return -2;
}

// 里程碑 3：在「车站 + 线路」组成的状态图上做 Dijkstra，换乘一次加 TRANSFER_MINUTES 分钟。
int routeFastest(const Metro* m, int from, int to, Route* r)
{
    // TODO
    (void)m; (void)from; (void)to; (void)r;
    return -2;
}

// 里程碑 3：和 routeFastest 用同一个 Dijkstra，只是不用还原路线。
int routeAllTimes(const Metro* m, int from, int* minutes)
{
    // TODO
    (void)m; (void)from; (void)minutes;
    return -2;
}

// 里程碑 4：同一张状态图，权重换成（换乘次数，用时）两个数，先比较换乘次数，再比较用时。
int routeFewestTransfers(const Metro* m, int from, int to, Route* r)
{
    // TODO
    (void)m; (void)from; (void)to; (void)r;
    return -2;
}

void routeFree(Route* r)
{
    free(r->stations);
    free(r->lines);
    r->stations = r->lines = NULL;
    r->n = 0;
}
