// lsm-kv 的实现，按数据流动的顺序：WAL → 内存表 → SSTable → 合并 → 读取 → 打开与恢复
#include "lsm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            fprintf(stderr, "%s:%d: 检查失败：%s（%s）\n", __FILE__, __LINE__, #cond, strerror(errno)); \
            exit(1);                                                                                 \
        }                                                                                            \
    } while (0)

#define TOMBSTONE 0xFFFFFFFFu  // 值的长度为这个数，表示「这个键被删除了」
#define MAX_LEVEL 7
#define L0_TRIGGER 4
#define BLOCK_SIZE 4096
#define TABLE_MAGIC 0x4C534D31u  // "LSM1"

static int keyCmp(const char* a, uint32_t al, const char* b, uint32_t bl)
{
    int c = memcmp(a, b, al < bl ? al : bl);
    return c ? c : (al > bl) - (al < bl);
}

/* ===================== 校验和 ===================== */

static uint32_t crcTable[256];

static uint32_t crc32(const void* data, size_t n)
{
    if (!crcTable[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t* p = data;
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ===================== 内存表：跳表 ===================== */

#define SKIP_MAX 16

typedef struct Node {
    char* key;
    char* val;
    uint32_t klen, vlen;  // vlen == TOMBSTONE 表示删除
    int height;
    struct Node* next[];
} Node;

typedef struct {
    Node* head;
    int height;
    size_t bytes, count;
} Mem;

static Node* newNode(int height)
{
    Node* n = calloc(1, sizeof(Node) + sizeof(Node*) * (size_t)height);
    CHECK(n);
    n->height = height;
    return n;
}

static Mem* memNew(void)
{
    Mem* m = calloc(1, sizeof(Mem));
    m->head = newNode(SKIP_MAX);
    m->height = 1;
    return m;
}

static void memFree(Mem* m)
{
    Node* n = m->head->next[0];
    while (n) {
        Node* next = n->next[0];
        free(n->key);
        free(n->val);
        free(n);
        n = next;
    }
    free(m->head);
    free(m);
}

// 插入或覆盖：同一个键只保留最新的值
static void memPut(Mem* m, const char* k, uint32_t kl, const char* v, uint32_t vl)
{
    Node* update[SKIP_MAX];
    Node* x = m->head;
    for (int i = m->height - 1; i >= 0; i--) {
        while (x->next[i] && keyCmp(x->next[i]->key, x->next[i]->klen, k, kl) < 0) x = x->next[i];
        update[i] = x;
    }
    Node* hit = x->next[0];
    uint32_t vbytes = vl == TOMBSTONE ? 0 : vl;
    if (hit && keyCmp(hit->key, hit->klen, k, kl) == 0) {
        m->bytes -= hit->vlen == TOMBSTONE ? 0 : hit->vlen;
        free(hit->val);
        hit->val = vbytes ? malloc(vbytes) : NULL;
        if (vbytes) memcpy(hit->val, v, vbytes);
        hit->vlen = vl;
        m->bytes += vbytes;
        return;
    }
    int h = 1;
    while (h < SKIP_MAX && (rand() & 3) == 0) h++;  // 每升一层的概率是 1/4
    if (h > m->height) {
        for (int i = m->height; i < h; i++) update[i] = m->head;
        m->height = h;
    }
    Node* n = newNode(h);
    n->key = malloc(kl);
    memcpy(n->key, k, kl);
    n->klen = kl;
    n->val = vbytes ? malloc(vbytes) : NULL;
    if (vbytes) memcpy(n->val, v, vbytes);
    n->vlen = vl;
    for (int i = 0; i < h; i++) {
        n->next[i] = update[i]->next[i];
        update[i]->next[i] = n;
    }
    m->bytes += kl + vbytes + sizeof(Node) + sizeof(Node*) * (size_t)h;
    m->count++;
}

static Node* memFind(Mem* m, const char* k, uint32_t kl)
{
    Node* x = m->head;
    for (int i = m->height - 1; i >= 0; i--)
        while (x->next[i] && keyCmp(x->next[i]->key, x->next[i]->klen, k, kl) < 0) x = x->next[i];
    x = x->next[0];
    return x && keyCmp(x->key, x->klen, k, kl) == 0 ? x : NULL;
}

/* ===================== 布隆过滤器 ===================== */

static uint32_t hash1(const char* k, uint32_t n)
{
    uint32_t h = 2166136261u;  // FNV-1a
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint8_t)k[i]) * 16777619u;
    return h;
}

// 用一个哈希值模拟 k 个哈希函数：h, h + delta, h + 2·delta ……（LevelDB 的做法）
static void bloomAdd(uint8_t* bits, uint32_t nbits, int k, const char* key, uint32_t kl)
{
    uint32_t h = hash1(key, kl), delta = (h >> 17) | (h << 15);
    for (int i = 0; i < k; i++, h += delta) bits[(h % nbits) / 8] |= (uint8_t)(1u << ((h % nbits) % 8));
}

static int bloomMayContain(const uint8_t* bits, uint32_t nbits, int k, const char* key, uint32_t kl)
{
    uint32_t h = hash1(key, kl), delta = (h >> 17) | (h << 15);
    for (int i = 0; i < k; i++, h += delta)
        if (!(bits[(h % nbits) / 8] & (1u << ((h % nbits) % 8)))) return 0;
    return 1;
}

/* ===================== SSTable ===================== */
// 文件格式：
//   数据块 × N：条目 [klen u32][vlen u32][key][val]，每块约 4 KB，块内按键有序
//   索引块：     [最小键的长度 u32][最小键]，然后每个数据块一项 [最后一个键的长度 u32][最后一个键][偏移 u64][大小 u32]
//   布隆过滤器： [位数 u32][哈希函数个数 u32][位数组]
//   文件尾 40 字节：[索引偏移 u64][索引大小 u32][布隆偏移 u64][布隆大小 u32][条目数 u64][块数 u32][魔数 u32]

typedef struct {
    char* lastKey;
    uint32_t lastLen;
    uint64_t off;
    uint32_t size;
} BlockIndex;

typedef struct {
    uint64_t number;
    int fd;
    uint64_t fileSize, count;
    char *minKey, *maxKey;
    uint32_t minLen, maxLen;
    BlockIndex* blocks;
    uint32_t nblocks;
    uint8_t* bloom;
    uint32_t bloomBits;
    int bloomK;
} Table;

typedef struct {
    Table** t;
    int n, cap;
} Level;

struct Lsm {
    char dir[512];
    LsmOptions opt;
    Mem* mem;
    int walFd;
    uint64_t walNumber, nextFile;
    int sinceSync;
    Level levels[MAX_LEVEL];  // levels[0] 中的文件按新旧排列（新的在后）；其余各层按键排列、互不重叠
    LsmStats st;
};

static void path(const Lsm* db, char* out, size_t n, const char* kind, uint64_t num)
{
    snprintf(out, n, "%s/%06llu.%s", db->dir, (unsigned long long)num, kind);
}

static void put32(char* p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(char* p, uint64_t v) { memcpy(p, &v, 8); }
static uint32_t get32(const char* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static uint64_t get64(const char* p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static void writeAll(int fd, const void* p, size_t n)
{
    const char* c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        CHECK(w > 0);
        c += w;
        n -= (size_t)w;
    }
}

// 逐条写出一个 SSTable
typedef struct {
    Lsm* db;
    int fd;
    uint64_t number, off, count;
    char* block;
    size_t blockLen;
    char *lastKey, *minKey;
    uint32_t lastLen, minLen;
    char* index;
    size_t indexLen, indexCap;
    uint32_t nblocks;
    char** keys;  // 所有的键，最后统一建布隆过滤器
    uint32_t* klens;
    size_t nkeys, keysCap;
} Builder;

static void builderStart(Builder* b, Lsm* db)
{
    memset(b, 0, sizeof *b);
    b->db = db;
    b->number = db->nextFile++;
    char p[600];
    path(db, p, sizeof p, "sst", b->number);
    b->fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(b->fd >= 0);
    b->block = malloc(BLOCK_SIZE * 4);
}

static void indexAppend(Builder* b, const void* p, size_t n)
{
    if (b->indexLen + n > b->indexCap) {
        b->indexCap = (b->indexCap + n) * 2;
        b->index = realloc(b->index, b->indexCap);
    }
    memcpy(b->index + b->indexLen, p, n);
    b->indexLen += n;
}

static void flushBlock(Builder* b)
{
    if (!b->blockLen) return;
    writeAll(b->fd, b->block, b->blockLen);
    char tmp[12];
    put32(tmp, b->lastLen);
    indexAppend(b, tmp, 4);
    indexAppend(b, b->lastKey, b->lastLen);
    put64(tmp, b->off);
    put32(tmp + 8, (uint32_t)b->blockLen);
    indexAppend(b, tmp, 12);
    b->off += b->blockLen;
    b->blockLen = 0;
    b->nblocks++;
}

static void builderAdd(Builder* b, const char* k, uint32_t kl, const char* v, uint32_t vl)
{
    uint32_t vbytes = vl == TOMBSTONE ? 0 : vl;
    if (b->blockLen && b->blockLen + 8 + kl + vbytes > BLOCK_SIZE) flushBlock(b);
    CHECK(8 + kl + vbytes <= BLOCK_SIZE * 4);  // 简化：一个条目不超过 16 KB
    put32(b->block + b->blockLen, kl);
    put32(b->block + b->blockLen + 4, vl);
    memcpy(b->block + b->blockLen + 8, k, kl);
    if (vbytes) memcpy(b->block + b->blockLen + 8 + kl, v, vbytes);
    b->blockLen += 8 + kl + vbytes;
    b->lastKey = realloc(b->lastKey, kl);
    memcpy(b->lastKey, k, kl);
    b->lastLen = kl;
    if (!b->count) {
        b->minKey = malloc(kl);
        memcpy(b->minKey, k, kl);
        b->minLen = kl;
    }
    b->count++;
    if (b->db->opt.bloomBitsPerKey) {
        if (b->nkeys == b->keysCap) {
            b->keysCap = b->keysCap ? b->keysCap * 2 : 1024;
            b->keys = realloc(b->keys, sizeof(char*) * b->keysCap);
            b->klens = realloc(b->klens, sizeof(uint32_t) * b->keysCap);
        }
        b->keys[b->nkeys] = malloc(kl);
        memcpy(b->keys[b->nkeys], k, kl);
        b->klens[b->nkeys++] = kl;
    }
}

static uint64_t builderSize(const Builder* b) { return b->off + b->blockLen; }

static Table* tableOpen(Lsm* db, uint64_t number);

// 写出索引、布隆过滤器和文件尾，返回打开的 Table
static Table* builderFinish(Builder* b, int doSync)
{
    flushBlock(b);
    uint64_t indexOff = b->off;
    char tmp[8];
    put32(tmp, b->minLen);
    writeAll(b->fd, tmp, 4);
    writeAll(b->fd, b->minKey, b->minLen);
    writeAll(b->fd, b->index, b->indexLen);
    uint32_t indexSize = 4 + b->minLen + (uint32_t)b->indexLen;
    uint64_t bloomOff = indexOff + indexSize;
    uint32_t bloomSize = 0;
    int bpk = b->db->opt.bloomBitsPerKey;
    if (bpk) {
        uint32_t nbits = (uint32_t)(b->nkeys * (size_t)bpk);
        if (nbits < 64) nbits = 64;
        int k = (int)(bpk * 0.69);  // 最优的哈希函数个数 ≈ 每键位数 × ln2
        if (k < 1) k = 1;
        if (k > 30) k = 30;
        uint8_t* bits = calloc((nbits + 7) / 8, 1);
        for (size_t i = 0; i < b->nkeys; i++) {
            bloomAdd(bits, nbits, k, b->keys[i], b->klens[i]);
            free(b->keys[i]);
        }
        char hdr[8];
        put32(hdr, nbits);
        put32(hdr + 4, (uint32_t)k);
        writeAll(b->fd, hdr, 8);
        writeAll(b->fd, bits, (nbits + 7) / 8);
        bloomSize = 8 + (nbits + 7) / 8;
        free(bits);
    }
    char foot[40];
    put64(foot, indexOff);
    put32(foot + 8, indexSize);
    put64(foot + 12, bloomOff);
    put32(foot + 20, bloomSize);
    put64(foot + 24, b->count);
    put32(foot + 32, b->nblocks);
    put32(foot + 36, TABLE_MAGIC);
    writeAll(b->fd, foot, 40);
    if (doSync) {
        CHECK(fsync(b->fd) == 0);
        b->db->st.fsyncs++;
    }
    uint64_t total = bloomOff + bloomSize + 40;
    close(b->fd);
    free(b->block);
    free(b->index);
    free(b->lastKey);
    free(b->minKey);
    free(b->keys);
    free(b->klens);
    Table* t = tableOpen(b->db, b->number);
    CHECK(t->fileSize == total);
    return t;
}

static Table* tableOpen(Lsm* db, uint64_t number)
{
    char p[600];
    path(db, p, sizeof p, "sst", number);
    Table* t = calloc(1, sizeof(Table));
    t->number = number;
    t->fd = open(p, O_RDONLY);
    CHECK(t->fd >= 0);
    struct stat s;
    CHECK(fstat(t->fd, &s) == 0);
    t->fileSize = (uint64_t)s.st_size;
    char foot[40];
    CHECK(pread(t->fd, foot, 40, (off_t)(t->fileSize - 40)) == 40);
    CHECK(get32(foot + 36) == TABLE_MAGIC);
    uint64_t indexOff = get64(foot);
    uint32_t indexSize = get32(foot + 8);
    uint64_t bloomOff = get64(foot + 12);
    uint32_t bloomSize = get32(foot + 20);
    t->count = get64(foot + 24);
    t->nblocks = get32(foot + 32);
    // 索引和布隆过滤器常驻内存，查找时只需要读一个数据块
    char* idx = malloc(indexSize);
    CHECK(pread(t->fd, idx, indexSize, (off_t)indexOff) == (ssize_t)indexSize);
    char* p2 = idx;
    t->minLen = get32(p2);
    t->minKey = malloc(t->minLen);
    memcpy(t->minKey, p2 + 4, t->minLen);
    p2 += 4 + t->minLen;
    t->blocks = calloc(t->nblocks, sizeof(BlockIndex));
    for (uint32_t i = 0; i < t->nblocks; i++) {
        BlockIndex* bi = &t->blocks[i];
        bi->lastLen = get32(p2);
        bi->lastKey = malloc(bi->lastLen);
        memcpy(bi->lastKey, p2 + 4, bi->lastLen);
        p2 += 4 + bi->lastLen;
        bi->off = get64(p2);
        bi->size = get32(p2 + 8);
        p2 += 12;
    }
    free(idx);
    t->maxKey = t->blocks[t->nblocks - 1].lastKey;
    t->maxLen = t->blocks[t->nblocks - 1].lastLen;
    if (bloomSize) {
        char hdr[8];
        CHECK(pread(t->fd, hdr, 8, (off_t)bloomOff) == 8);
        t->bloomBits = get32(hdr);
        t->bloomK = (int)get32(hdr + 4);
        t->bloom = malloc(bloomSize - 8);
        CHECK(pread(t->fd, t->bloom, bloomSize - 8, (off_t)bloomOff + 8) == (ssize_t)(bloomSize - 8));
    }
    return t;
}

static void tableClose(Table* t, int removeFile, const Lsm* db)
{
    close(t->fd);
    if (removeFile) {
        char p[600];
        path(db, p, sizeof p, "sst", t->number);
        unlink(p);
    }
    for (uint32_t i = 0; i < t->nblocks; i++) free(t->blocks[i].lastKey);
    free(t->blocks);
    free(t->minKey);
    free(t->bloom);
    free(t);
}

// 在一个 SSTable 中查找：1 找到，0 确定不在，-1 是墓碑（已删除）
static int tableGet(Lsm* db, Table* t, const char* k, uint32_t kl, char** val, uint32_t* vlen)
{
    if (keyCmp(k, kl, t->minKey, t->minLen) < 0 || keyCmp(k, kl, t->maxKey, t->maxLen) > 0) return 0;
    if (t->bloom) {
        db->st.bloomChecks++;
        if (!bloomMayContain(t->bloom, t->bloomBits, t->bloomK, k, kl)) {
            db->st.bloomNegatives++;
            return 0;
        }
    }
    uint32_t lo = 0, hi = t->nblocks;  // 第一个「最后一个键 >= k」的块
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (keyCmp(t->blocks[mid].lastKey, t->blocks[mid].lastLen, k, kl) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == t->nblocks) return 0;
    BlockIndex* bi = &t->blocks[lo];
    char* buf = malloc(bi->size);
    CHECK(pread(t->fd, buf, bi->size, (off_t)bi->off) == (ssize_t)bi->size);
    db->st.blockReads++;
    int r = 0;
    for (uint32_t p = 0; p < bi->size;) {
        uint32_t el = get32(buf + p), ev = get32(buf + p + 4);
        uint32_t evb = ev == TOMBSTONE ? 0 : ev;
        int c = keyCmp(buf + p + 8, el, k, kl);
        if (c == 0) {
            if (ev == TOMBSTONE) {
                r = -1;
            } else {
                *val = malloc(ev ? ev : 1);
                memcpy(*val, buf + p + 8 + el, ev);
                *vlen = ev;
                r = 1;
            }
            break;
        }
        if (c > 0) break;
        p += 8 + el + evb;
    }
    free(buf);
    return r;
}

/* ===================== 迭代器：按键的顺序遍历一个 SSTable ===================== */

typedef struct {
    // 当前条目
    const char* key;
    const char* val;
    uint32_t klen, vlen;
    int valid;
    Table* t;
    uint32_t block, pos;
    char* buf;
    uint32_t bufSize;
    uint64_t* readBytes;
} Iter;

static void iterLoadEntry(Iter* it)
{
    it->klen = get32(it->buf + it->pos);
    it->vlen = get32(it->buf + it->pos + 4);
    it->key = it->buf + it->pos + 8;
    it->val = it->key + it->klen;
    it->valid = 1;
}

static int iterLoadBlock(Iter* it)
{
    if (it->block >= it->t->nblocks) {
        it->valid = 0;
        return 0;
    }
    BlockIndex* bi = &it->t->blocks[it->block];
    if (bi->size > it->bufSize) {
        it->buf = realloc(it->buf, bi->size);
        it->bufSize = bi->size;
    }
    CHECK(pread(it->t->fd, it->buf, bi->size, (off_t)bi->off) == (ssize_t)bi->size);
    if (it->readBytes) *it->readBytes += bi->size;
    it->pos = 0;
    iterLoadEntry(it);
    return 1;
}

static void iterTable(Iter* it, Table* t, uint64_t* readBytes)
{
    memset(it, 0, sizeof *it);
    it->t = t;
    it->readBytes = readBytes;
    iterLoadBlock(it);
}

static void iterNext(Iter* it)
{
    uint32_t vb = it->vlen == TOMBSTONE ? 0 : it->vlen;
    it->pos += 8 + it->klen + vb;
    if (it->pos < it->t->blocks[it->block].size) {
        iterLoadEntry(it);
        return;
    }
    it->block++;
    iterLoadBlock(it);
}

/* ===================== WAL 与清单 ===================== */

static void walOpen(Lsm* db, int truncate)
{
    char p[600];
    path(db, p, sizeof p, "log", db->walNumber);
    db->walFd = open(p, O_WRONLY | O_CREAT | O_APPEND | (truncate ? O_TRUNC : 0), 0644);
    CHECK(db->walFd >= 0);
}

static void syncDir(const Lsm* db)
{
    int fd = open(db->dir, O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

// 清单（MANIFEST）记录哪些 SSTable 属于哪一层、当前的 WAL 和下一个文件号。
// 先写临时文件、fsync，再 rename：要么是旧的清单，要么是新的，不会只写了一半。
static void writeManifest(Lsm* db)
{
    char tmp[600], fin[600];
    snprintf(tmp, sizeof tmp, "%s/MANIFEST.tmp", db->dir);
    snprintf(fin, sizeof fin, "%s/MANIFEST", db->dir);
    FILE* f = fopen(tmp, "w");
    CHECK(f);
    fprintf(f, "next %llu\nwal %llu\n", (unsigned long long)db->nextFile, (unsigned long long)db->walNumber);
    for (int l = 0; l < MAX_LEVEL; l++)
        for (int i = 0; i < db->levels[l].n; i++)
            fprintf(f, "table %d %llu\n", l, (unsigned long long)db->levels[l].t[i]->number);
    CHECK(fflush(f) == 0);
    CHECK(fsync(fileno(f)) == 0);
    fclose(f);
    CHECK(rename(tmp, fin) == 0);
    syncDir(db);
    db->st.fsyncs += 2;
}

static void levelAppend(Level* lv, Table* t)
{
    if (lv->n == lv->cap) {
        lv->cap = lv->cap ? lv->cap * 2 : 8;
        lv->t = realloc(lv->t, sizeof(Table*) * (size_t)lv->cap);
    }
    lv->t[lv->n++] = t;
}

static uint64_t levelBytes(const Level* lv)
{
    uint64_t s = 0;
    for (int i = 0; i < lv->n; i++) s += lv->t[i]->fileSize;
    return s;
}

/* ===================== 落盘与合并 ===================== */

static void maybeCompact(Lsm* db);

// 内存表整个写成一个 L0 的 SSTable，然后换一个新的 WAL
static void flushMemtable(Lsm* db)
{
    if (!db->mem->count) return;
    Builder b;
    builderStart(&b, db);
    for (Node* n = db->mem->head->next[0]; n; n = n->next[0]) builderAdd(&b, n->key, n->klen, n->val, n->vlen);
    Table* t = builderFinish(&b, 1);
    db->st.flushBytes += t->fileSize;
    db->st.flushes++;
    levelAppend(&db->levels[0], t);

    uint64_t oldWal = db->walNumber;
    db->walNumber = db->nextFile++;
    writeManifest(db);  // 清单写好以后，旧的 WAL 才可以删除
    close(db->walFd);
    char p[600];
    path(db, p, sizeof p, "log", oldWal);
    unlink(p);
    walOpen(db, 1);
    db->sinceSync = 0;
    memFree(db->mem);
    db->mem = memNew();
    maybeCompact(db);
}

// 多路归并：输入按新旧排列（下标小的更新），同一个键只保留最新的版本
static void mergeInto(Lsm* db, Iter* its, int n, int outLevel, int dropTombstones)
{
    Level out = {0};
    Builder b;
    int building = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (!its[i].valid) continue;
            if (best < 0 || keyCmp(its[i].key, its[i].klen, its[best].key, its[best].klen) < 0) best = i;
        }
        if (best < 0) break;
        // its[best] 是最小的键中最新的版本（相同的键，下标小的先被选中）
        char* k = malloc(its[best].klen);
        uint32_t kl = its[best].klen;
        memcpy(k, its[best].key, kl);
        if (!(dropTombstones && its[best].vlen == TOMBSTONE)) {
            if (!building) {
                builderStart(&b, db);
                building = 1;
            }
            builderAdd(&b, its[best].key, its[best].klen, its[best].val, its[best].vlen);
            if (builderSize(&b) >= db->opt.tableBytes) {
                Table* t = builderFinish(&b, 1);
                db->st.compactWrite += t->fileSize;
                levelAppend(&out, t);
                building = 0;
            }
        }
        for (int i = 0; i < n; i++)  // 所有来源中跳过这个键（旧的版本被丢弃）
            while (its[i].valid && keyCmp(its[i].key, its[i].klen, k, kl) == 0) iterNext(&its[i]);
        free(k);
    }
    if (building) {  // 只有加入过条目才会开始一个文件，所以这里至少有一条
        Table* t = builderFinish(&b, 1);
        db->st.compactWrite += t->fileSize;
        levelAppend(&out, t);
    }
    // 输出替换掉目标层
    db->levels[outLevel].n = 0;
    for (int i = 0; i < out.n; i++) levelAppend(&db->levels[outLevel], out.t[i]);
    free(out.t);
}

static int overlaps(const Table* x, const Table* y)
{
    return keyCmp(x->minKey, x->minLen, y->maxKey, y->maxLen) <= 0 &&
           keyCmp(y->minKey, y->minLen, x->maxKey, x->maxLen) <= 0;
}

static int byMinKey(const void* a, const void* b)
{
    const Table* x = *(Table* const*)a;
    const Table* y = *(Table* const*)b;
    return keyCmp(x->minKey, x->minLen, y->minKey, y->minLen);
}

// 平移：本层的文件和下一层的文件（以及 L0 中彼此之间）在键的范围上都不重叠，
// 就不必重写，直接把文件挪到下一层（只改清单）。按顺序写入的数据几乎总是这样
static int trivialMove(Lsm* db, int from)
{
    Level* a = &db->levels[from];
    Level* bl = &db->levels[from + 1];
    for (int i = 0; i < a->n; i++) {
        for (int j = 0; j < bl->n; j++)
            if (overlaps(a->t[i], bl->t[j])) return 0;
        for (int j = i + 1; j < a->n; j++)
            if (overlaps(a->t[i], a->t[j])) return 0;
    }
    for (int i = 0; i < a->n; i++) levelAppend(bl, a->t[i]);
    a->n = 0;
    qsort(bl->t, (size_t)bl->n, sizeof(Table*), byMinKey);
    db->st.trivialMoves++;
    writeManifest(db);
    return 1;
}

// 把 from 层整层合并进 from + 1 层
static void compactLevel(Lsm* db, int from)
{
    int to = from + 1;
    CHECK(to < MAX_LEVEL);
    if (trivialMove(db, from)) return;
    Level* a = &db->levels[from];
    Level* bl = &db->levels[to];
    int n = a->n + bl->n;
    Iter* its = calloc((size_t)n, sizeof(Iter));
    Table** old = malloc(sizeof(Table*) * (size_t)n);
    // 来源按从新到旧排列：本层在前，下一层在后。
    // L0 的文件互相重叠，新的在数组后面，所以倒着放；其余各层的文件互不重叠，顺序无所谓
    int k = 0;
    for (int i = 0; i < a->n; i++) old[k++] = a->t[from == 0 ? a->n - 1 - i : i];
    for (int i = 0; i < bl->n; i++) old[k++] = bl->t[i];
    for (int i = 0; i < n; i++) iterTable(&its[i], old[i], &db->st.compactRead);

    int deeperEmpty = 1;  // 更深的层都是空的，墓碑就可以丢掉了
    for (int l = to + 1; l < MAX_LEVEL; l++)
        if (db->levels[l].n) deeperEmpty = 0;
    a->n = 0;
    mergeInto(db, its, n, to, deeperEmpty);
    db->st.compactions++;
    writeManifest(db);
    for (int i = 0; i < n; i++) {
        free(its[i].buf);
        tableClose(old[i], 1, db);
    }
    free(its);
    free(old);
}

static void maybeCompact(Lsm* db)
{
    if (db->levels[0].n >= L0_TRIGGER) compactLevel(db, 0);
    uint64_t limit = db->opt.level1Bytes;
    for (int l = 1; l < MAX_LEVEL - 1; l++, limit *= 10)
        if (levelBytes(&db->levels[l]) > limit) compactLevel(db, l);
}

/* ===================== 写入 ===================== */

static int writeRecord(Lsm* db, const char* k, uint32_t kl, const char* v, uint32_t vl)
{
    uint32_t vbytes = vl == TOMBSTONE ? 0 : vl;
    size_t n = 12 + kl + vbytes;
    char stackBuf[512];
    char* rec = n <= sizeof stackBuf ? stackBuf : malloc(n);
    put32(rec + 4, kl);
    put32(rec + 8, vl);
    memcpy(rec + 12, k, kl);
    if (vbytes) memcpy(rec + 12 + kl, v, vbytes);
    put32(rec, crc32(rec + 4, n - 4));
    writeAll(db->walFd, rec, n);  // 一次 write：要么整条在页缓存中，要么没有
    if (rec != stackBuf) free(rec);
    db->st.walBytes += n;
    if (db->opt.syncEvery && ++db->sinceSync >= db->opt.syncEvery) {
        CHECK(fdatasync(db->walFd) == 0);
        db->st.fsyncs++;
        db->sinceSync = 0;
    }
    memPut(db->mem, k, kl, v, vl);
    db->st.userBytes += kl + vbytes;
    if (db->mem->bytes >= db->opt.memtableBytes) flushMemtable(db);
    return 0;
}

int lsmPut(Lsm* db, const char* key, uint32_t klen, const char* val, uint32_t vlen)
{
    CHECK(vlen != TOMBSTONE);
    return writeRecord(db, key, klen, val, vlen);
}

int lsmDelete(Lsm* db, const char* key, uint32_t klen) { return writeRecord(db, key, klen, NULL, TOMBSTONE); }

/* ===================== 读取 ===================== */

int lsmGet(Lsm* db, const char* key, uint32_t klen, char** val, uint32_t* vlen)
{
    db->st.gets++;
    Node* n = memFind(db->mem, key, klen);
    if (n) {
        if (n->vlen == TOMBSTONE) return 0;
        *val = malloc(n->vlen ? n->vlen : 1);
        memcpy(*val, n->val, n->vlen);
        *vlen = n->vlen;
        return 1;
    }
    for (int i = db->levels[0].n - 1; i >= 0; i--) {  // L0 从新到旧，每个文件都可能有
        int r = tableGet(db, db->levels[0].t[i], key, klen, val, vlen);
        if (r) return r > 0;
    }
    for (int l = 1; l < MAX_LEVEL; l++) {  // 其余各层互不重叠，二分找到唯一可能的文件
        Level* lv = &db->levels[l];
        int lo = 0, hi = lv->n;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (keyCmp(lv->t[mid]->maxKey, lv->t[mid]->maxLen, key, klen) < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < lv->n) {
            int r = tableGet(db, lv->t[lo], key, klen, val, vlen);
            if (r) return r > 0;
        }
    }
    return 0;
}

/* ===================== 打开、恢复、关闭 ===================== */

void lsmDefaultOptions(LsmOptions* o)
{
    o->memtableBytes = 4 << 20;
    o->syncEvery = 1;
    o->bloomBitsPerKey = 10;
    o->tableBytes = 4 << 20;
    o->level1Bytes = 20 << 20;
}

// 重放 WAL：逐条检查校验和，遇到不完整或损坏的记录就停下，并把文件截断到最后一条好记录的末尾
static size_t replayWal(Lsm* db)
{
    char p[600];
    path(db, p, sizeof p, "log", db->walNumber);
    int fd = open(p, O_RDWR);
    if (fd < 0) return 0;
    struct stat s;
    fstat(fd, &s);
    char* data = malloc((size_t)s.st_size + 1);
    CHECK(pread(fd, data, (size_t)s.st_size, 0) == s.st_size);
    size_t off = 0, n = 0;
    while (off + 12 <= (size_t)s.st_size) {
        uint32_t kl = get32(data + off + 4), vl = get32(data + off + 8);
        uint32_t vb = vl == TOMBSTONE ? 0 : vl;
        if (off + 12 + kl + vb > (size_t)s.st_size) break;
        if (crc32(data + off + 4, 8 + kl + vb) != get32(data + off)) break;
        memPut(db->mem, data + off + 12, kl, data + off + 12 + kl, vl);
        off += 12 + kl + vb;
        n++;
    }
    if (off < (size_t)s.st_size) CHECK(ftruncate(fd, (off_t)off) == 0);
    close(fd);
    free(data);
    return n;
}

Lsm* lsmOpen(const char* dir, const LsmOptions* o)
{
    Lsm* db = calloc(1, sizeof(Lsm));
    snprintf(db->dir, sizeof db->dir, "%s", dir);
    if (o)
        db->opt = *o;
    else
        lsmDefaultOptions(&db->opt);
    mkdir(dir, 0755);
    db->mem = memNew();
    db->nextFile = 1;
    char mp[600];
    snprintf(mp, sizeof mp, "%s/MANIFEST", dir);
    FILE* f = fopen(mp, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            unsigned long long a;
            int lv;
            if (sscanf(line, "next %llu", &a) == 1) db->nextFile = a;
            else if (sscanf(line, "wal %llu", &a) == 1) db->walNumber = a;
            else if (sscanf(line, "table %d %llu", &lv, &a) == 2) levelAppend(&db->levels[lv], tableOpen(db, a));
        }
        fclose(f);
        size_t n = replayWal(db);
        if (n) fprintf(stderr, "lsm: 从 WAL 恢复了 %zu 条记录\n", n);
        walOpen(db, 0);
    } else {
        db->walNumber = db->nextFile++;
        walOpen(db, 1);
        writeManifest(db);
    }
    return db;
}

void lsmClose(Lsm* db)
{
    flushMemtable(db);  // 关闭时把内存表落盘，下次打开不必重放 WAL
    if (db->opt.syncEvery == 0) {
        fdatasync(db->walFd);
        db->st.fsyncs++;
    }
    close(db->walFd);
    for (int l = 0; l < MAX_LEVEL; l++) {
        for (int i = 0; i < db->levels[l].n; i++) tableClose(db->levels[l].t[i], 0, db);
        free(db->levels[l].t);
    }
    memFree(db->mem);
    free(db);
}

const LsmStats* lsmStats(const Lsm* db) { return &db->st; }

uint64_t lsmDiskBytes(const Lsm* db)
{
    uint64_t s = 0;
    for (int l = 0; l < MAX_LEVEL; l++) s += levelBytes(&db->levels[l]);
    return s;
}

void lsmPrintLevels(const Lsm* db)
{
    for (int l = 0; l < MAX_LEVEL; l++) {
        const Level* lv = &db->levels[l];
        if (!lv->n) continue;
        uint64_t entries = 0;
        for (int i = 0; i < lv->n; i++) entries += lv->t[i]->count;
        printf("  L%d：%3d 个文件，%7.1f MB，%llu 条\n", l, lv->n, levelBytes(lv) / 1048576.0,
               (unsigned long long)entries);
    }
}
