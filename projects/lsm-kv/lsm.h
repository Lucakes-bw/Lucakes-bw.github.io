// lsm-kv：一个用来学习的 LSM 树键值存储
//   写入：先追加到预写日志（WAL），再放进内存表（跳表）；内存表写满后整个写成一个 SSTable 文件（L0）
//   读取：内存表 → L0（新的在前）→ L1 → L2 ……，每个 SSTable 先问布隆过滤器
//   合并：L0 的文件达到 4 个，和 L1 合并；某一层超过上限，整层合并到下一层
#ifndef LSM_H
#define LSM_H

#include <stddef.h>
#include <stdint.h>

typedef struct Lsm Lsm;

typedef struct {
    size_t memtableBytes;  // 内存表写到多大就落盘，默认 4 MB
    int syncEvery;         // WAL：0 从不 fsync；1 每次写都 fsync；N 每 N 次写 fsync 一次
    int bloomBitsPerKey;   // 布隆过滤器每个键用几位，0 表示不用
    size_t tableBytes;     // 合并时输出的每个 SSTable 的大小，默认 4 MB
    size_t level1Bytes;    // L1 的上限，默认 20 MB；每往下一层乘以 10
} LsmOptions;

typedef struct {
    uint64_t userBytes;       // 程序写入的键和值的字节数
    uint64_t walBytes;        // 写进 WAL 的字节数
    uint64_t flushBytes;      // 内存表落盘写出的字节数
    uint64_t compactRead;     // 合并时读入的字节数
    uint64_t compactWrite;    // 合并时写出的字节数
    uint64_t fsyncs;          // fsync 的次数
    uint64_t gets;            // get 的次数
    uint64_t blockReads;      // get 时读数据块的次数
    uint64_t bloomChecks;     // 问过布隆过滤器的次数
    uint64_t bloomNegatives;  // 布隆过滤器回答「一定不在」的次数
    uint64_t flushes, compactions;
    uint64_t trivialMoves;    // 不重写、直接挪到下一层的次数
} LsmStats;

void lsmDefaultOptions(LsmOptions* o);
Lsm* lsmOpen(const char* dir, const LsmOptions* o);  // 打开（不存在就创建），并从 WAL 恢复内存表
void lsmClose(Lsm* db);
int lsmPut(Lsm* db, const char* key, uint32_t klen, const char* val, uint32_t vlen);
int lsmDelete(Lsm* db, const char* key, uint32_t klen);
// 找到返回 1，*val 指向 malloc 出来的值（调用者 free）；没找到或已删除返回 0
int lsmGet(Lsm* db, const char* key, uint32_t klen, char** val, uint32_t* vlen);
const LsmStats* lsmStats(const Lsm* db);
void lsmPrintLevels(const Lsm* db);       // 打印每一层的文件数和大小
uint64_t lsmDiskBytes(const Lsm* db);     // 所有 SSTable 加起来的大小

#endif
