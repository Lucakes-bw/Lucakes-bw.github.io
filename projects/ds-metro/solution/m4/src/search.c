#include <stdlib.h>
#include <string.h>
#include "search.h"
#include "route.h"

// 里程碑 5：前缀搜索和排行。要求见里程碑 5 的文章。
// 还没有实现的函数返回 -2；实现以后把 return -2 改掉。

void nameIndexBuild(NameIndex* ix, const Metro* m)
{
    // TODO：把所有车站的编号放进 order，按站名排序
    (void)m;
    ix->order = NULL;
    ix->n = 0;
}

void nameIndexFree(NameIndex* ix)
{
    free(ix->order);
    ix->order = NULL;
    ix->n = 0;
}

int nameIndexPrefix(const NameIndex* ix, const Metro* m, const char* prefix, int* first)
{
    // TODO：用二分查找找到第一个不小于 prefix 的站名，再向后数有几个以 prefix 开头
    (void)ix; (void)m; (void)prefix;
    *first = 0;
    return -2;
}

int rankCentral(const Metro* m, int k, int* out)
{
    // TODO：用 routeAllTimes 算出每个车站的排序依据，再用堆选出前 k 个
    (void)m; (void)k; (void)out;
    return -2;
}
