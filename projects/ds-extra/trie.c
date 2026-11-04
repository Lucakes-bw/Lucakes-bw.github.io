// Trie：插入、查找、前缀计数、按前缀列出单词；用系统词典（/usr/share/dict/words）测内存和速度
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "common.h"

/* ---------- 每个节点 26 个孩子指针的 Trie ---------- */

typedef struct TrieNode {
    struct TrieNode* child[26];
    int pass;                               // 经过这个节点的单词数：以这个前缀开头的单词有几个
    bool end;                               // 有没有单词在这里结束
} TrieNode;

static long long nodeCount;

static TrieNode* newTrieNode(void)
{
    TrieNode* n = calloc(1, sizeof(TrieNode));
    if (n == NULL) exit(1);
    ++nodeCount;
    return n;
}

static bool trieInsert(TrieNode* root, const char* w)
{
    TrieNode* x = root;
    for (const char* p = w; *p; ++p) {
        int c = *p - 'a';
        if (!x->child[c]) x->child[c] = newTrieNode();
        x = x->child[c];
    }
    if (x->end) return false;               // 已经有这个单词了，不重复计数
    x->end = true;
    for (const char* p = w; ; ++p) {        // 确认是新单词以后，再沿路径把 pass 加一
        root->pass++;
        if (!*p) break;
        root = root->child[*p - 'a'];
    }
    return true;
}

// 走到前缀对应的节点，不存在返回 NULL
static TrieNode* walk(TrieNode* root, const char* prefix)
{
    TrieNode* x = root;
    for (const char* p = prefix; *p && x; ++p) x = x->child[*p - 'a'];
    return x;
}

static bool trieContains(TrieNode* root, const char* w)
{
    TrieNode* x = walk(root, w);
    return x && x->end;
}

static int countPrefix(TrieNode* root, const char* prefix)
{
    TrieNode* x = walk(root, prefix);
    return x ? x->pass : 0;
}

// 深度优先列出以 buf 为前缀的单词，最多 limit 个；按字母顺序
static int listWords(TrieNode* x, char* buf, int len, int limit)
{
    int printed = 0;
    if (x->end) {
        buf[len] = '\0';
        printf("%s ", buf);
        ++printed;
    }
    for (int c = 0; c < 26 && printed < limit; ++c) {
        if (!x->child[c]) continue;
        buf[len] = (char)('a' + c);
        printed += listWords(x->child[c], buf, len + 1, limit - printed);
    }
    return printed;
}

static void trieFree(TrieNode* x)
{
    for (int c = 0; c < 26; ++c)
        if (x->child[c]) trieFree(x->child[c]);
    free(x);
}

/* ---------- 省内存的写法：孩子用链表连起来（第一个孩子 + 下一个兄弟） ---------- */

typedef struct LNode {
    struct LNode *child, *sibling;          // 第一个孩子；同一个父亲的下一个孩子
    char c;
    bool end;
} LNode;

static long long lnodeCount;

static LNode* lfind(LNode* parent, char c, bool create)
{
    LNode** link = &parent->child;
    while (*link && (*link)->c < c) link = &(*link)->sibling;   // 兄弟按字母顺序排列
    if (*link && (*link)->c == c) return *link;
    if (!create) return NULL;
    LNode* n = calloc(1, sizeof(LNode));
    if (n == NULL) exit(1);
    n->c = c;
    n->sibling = *link;
    *link = n;
    ++lnodeCount;
    return n;
}

static void linsert(LNode* root, const char* w)
{
    for (const char* p = w; *p; ++p) root = lfind(root, *p, true);
    root->end = true;
}

static bool lcontains(LNode* root, const char* w)
{
    for (const char* p = w; *p && root; ++p) root = lfind(root, *p, false);
    return root && root->end;
}

static void lfree(LNode* x)
{
    while (x) {
        LNode* next = x->sibling;
        lfree(x->child);
        free(x);
        x = next;
    }
}

/* ---------- 对比：排好序的字符串数组 ---------- */

static int cmpStr(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

int main(void)
{
    printf("== 小例子\n");
    TrieNode* root = newTrieNode();
    const char* small[] = {"tea", "ten", "to", "inn", "in", "a", "tea"};
    for (int i = 0; i < 7; ++i) printf("插入 %-4s %s\n", small[i], trieInsert(root, small[i]) ? "" : "（已经存在）");
    printf("节点数（含根）：%lld\n", nodeCount);
    printf("以 te 开头：%d 个；以 t 开头：%d 个；以 x 开头：%d 个\n", countPrefix(root, "te"), countPrefix(root, "t"),
           countPrefix(root, "x"));
    printf("contains(in)=%d contains(i)=%d contains(tea)=%d\n", trieContains(root, "in"), trieContains(root, "i"),
           trieContains(root, "tea"));
    trieFree(root);

    printf("== 系统词典\n");
    FILE* f = fopen("/usr/share/dict/words", "r");
    if (!f) {
        printf("没有找到 /usr/share/dict/words\n");
        return 0;
    }
    int cap = 1 << 18, n = 0;
    char** words = malloc(sizeof(char*) * (size_t)cap);
    char line[128];
    long long chars = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        bool ok = line[0] != '\0';
        for (char* p = line; *p && ok; ++p) ok = islower((unsigned char)*p);   // 只要全是小写字母的单词
        if (!ok) continue;
        if (n == cap) words = realloc(words, sizeof(char*) * (size_t)(cap *= 2));
        words[n] = strdup(line);
        chars += (long long)strlen(line);
        ++n;
    }
    fclose(f);

    nodeCount = 0;
    root = newTrieNode();
    double t0 = nowSeconds();
    int inserted = 0;
    for (int i = 0; i < n; ++i) inserted += trieInsert(root, words[i]);
    double tBuild = nowSeconds() - t0;
    printf("单词 %d 个（全小写），不重复的 %d 个，共 %lld 个字母\n", n, inserted, chars);
    printf("Trie 节点 %lld 个，每个节点 %zu 字节，共 %.1f MB；单词本身只有 %.1f MB\n", nodeCount, sizeof(TrieNode),
           (double)nodeCount * sizeof(TrieNode) / 1048576.0, (double)(chars + n) / 1048576.0);
    long long used = 0;
    // 统计实际用到的孩子指针：每个节点（根除外）恰好被一个指针指着
    used = nodeCount - 1;
    printf("孩子指针一共 %lld 个，实际用到 %lld 个（%.1f%%）\n", nodeCount * 26, used, 100.0 * used / (nodeCount * 26.0));

    t0 = nowSeconds();
    int hits = 0;
    for (int r = 0; r < 10; ++r)
        for (int i = 0; i < n; ++i) hits += trieContains(root, words[i]);
    double tTrie = nowSeconds() - t0;
    CHECK(hits == 10 * n);

    char** sorted = malloc(sizeof(char*) * (size_t)n);
    memcpy(sorted, words, sizeof(char*) * (size_t)n);
    qsort(sorted, (size_t)n, sizeof(char*), cmpStr);
    t0 = nowSeconds();
    hits = 0;
    for (int r = 0; r < 10; ++r)
        for (int i = 0; i < n; ++i) hits += bsearch(&words[i], sorted, (size_t)n, sizeof(char*), cmpStr) != NULL;
    double tBin = nowSeconds() - t0;
    CHECK(hits == 10 * n);
    printf("建树 %.3f 秒；查找 %d 次：Trie %.3f 秒，排序数组二分查找 %.3f 秒\n", tBuild, 10 * n, tTrie, tBin);

    LNode lroot = {0};
    t0 = nowSeconds();
    for (int i = 0; i < n; ++i) linsert(&lroot, words[i]);
    double tLBuild = nowSeconds() - t0;
    t0 = nowSeconds();
    hits = 0;
    for (int r = 0; r < 10; ++r)
        for (int i = 0; i < n; ++i) hits += lcontains(&lroot, words[i]);
    double tL = nowSeconds() - t0;
    CHECK(hits == 10 * n);
    printf("孩子用链表：节点 %lld 个，每个 %zu 字节，共 %.1f MB；建树 %.3f 秒，查找 %.3f 秒\n", lnodeCount, sizeof(LNode),
           (double)lnodeCount * sizeof(LNode) / 1048576.0, tLBuild, tL);
    lfree(lroot.child);

    const char* prefixes[] = {"pre", "inter", "zy", "qu", "xylo"};
    for (int i = 0; i < 5; ++i) {
        // 用排序数组暴力数一遍，检查前缀计数
        int brute = 0;
        size_t len = strlen(prefixes[i]);
        for (int k = 0; k < n; ++k) brute += strncmp(words[k], prefixes[i], len) == 0;
        int cnt = countPrefix(root, prefixes[i]);
        CHECK(cnt <= brute);   // words 中可能有重复的单词，Trie 只算一次
        printf("以 %-5s 开头：%5d 个；前几个：", prefixes[i], cnt);
        char buf[128];
        strcpy(buf, prefixes[i]);
        TrieNode* x = walk(root, prefixes[i]);
        if (x) listWords(x, buf, (int)len, 5);
        printf("\n");
    }
    trieFree(root);
    for (int i = 0; i < n; ++i) free(words[i]);
    free(words);
    free(sorted);
    return 0;
}
