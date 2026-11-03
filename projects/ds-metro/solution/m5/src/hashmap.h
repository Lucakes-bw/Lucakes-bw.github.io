#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>

// 字符串 -> 整数的哈希表（链地址法），用来按名字查找车站和线路的编号
typedef struct HmEntry {
    char* key;                                  // 键的副本，由哈希表负责释放
    int value;
    struct HmEntry* next;
} HmEntry;

typedef struct {
    HmEntry** buckets;
    size_t nbuckets;
    size_t size;                                // 元素个数
} HashMap;

void hmInit(HashMap* m);                        // 初始化为空表（8 个桶）
void hmFree(HashMap* m);                        // 释放所有的桶、元素和键
void hmPut(HashMap* m, const char* key, int value);   // 键已存在时更新它的值
int hmGet(const HashMap* m, const char* key);   // 找不到时返回 -1

#endif
