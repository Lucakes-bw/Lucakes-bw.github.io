#ifndef SEARCH_H
#define SEARCH_H

#include "metro.h"

// 里程碑 5：按站名排好序的车站编号，用来做前缀搜索
typedef struct {
    int* order;                                 // 车站编号，按站名从小到大（strcmp 的顺序）排列
    int n;
} NameIndex;

void nameIndexBuild(NameIndex* ix, const Metro* m);
void nameIndexFree(NameIndex* ix);

// 查找站名以 prefix 开头的车站。返回匹配的个数，*first 是第一个匹配在 order 中的下标；
// 匹配的车站在 order 中是连续的：order[*first] ... order[*first + 返回值 - 1]。
// prefix 是空字符串时，所有车站都匹配。（起始代码中还没有实现的函数返回 -2。）
int nameIndexPrefix(const NameIndex* ix, const Metro* m, const char* prefix, int* first);

// 里程碑 5：到其他车站平均用时最短的 k 个车站，写到 out 中，返回实际写入的个数（车站不足 k 个时全部写入）。
// 排序的规则：能到达的车站多的在前；一样多时，到这些车站的时间总和小的在前；再一样时，编号小的在前。
int rankCentral(const Metro* m, int k, int* out);

#endif
