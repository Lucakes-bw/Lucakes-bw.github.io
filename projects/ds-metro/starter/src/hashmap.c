#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"

// 里程碑 1：实现字符串 -> 整数的哈希表（链地址法）。
// 可以参考《哈希表》一文的第 2、3 节。要求：
//   - 哈希函数用 FNV-1a，不要自己设计
//   - hmInit 时有 8 个桶；元素个数超过桶的个数时，桶的个数翻倍并重新分配（扩容）
//   - hmPut 要复制一份键保存，调用者的字符串之后可能会被修改
//   - hmGet 找不到时返回 -1
// 写完后可以单独运行哈希表的测试：tests/run.sh m1（最前面的 3 项就是）

static uint32_t hashFNV(const char* s)
{
    // TODO
    (void)s;
    return 0;
}

void hmInit(HashMap* m)
{
    // TODO
    m->buckets = NULL;
    m->nbuckets = 0;
    m->size = 0;
}

void hmFree(HashMap* m)
{
    // TODO：释放每个元素的键、元素本身和桶数组
    (void)m;
}

void hmPut(HashMap* m, const char* key, int value)
{
    // TODO
    (void)m; (void)key; (void)value;
    (void)hashFNV;
}

int hmGet(const HashMap* m, const char* key)
{
    // TODO
    (void)m; (void)key;
    return -1;
}
