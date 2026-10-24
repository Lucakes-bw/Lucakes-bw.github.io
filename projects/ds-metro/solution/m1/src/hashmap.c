#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"

static uint32_t hashFNV(const char* s)          // FNV-1a
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

void hmInit(HashMap* m)
{
    m->nbuckets = 8;
    m->size = 0;
    m->buckets = calloc(m->nbuckets, sizeof(HmEntry*));
    if (m->buckets == NULL) exit(1);
}

void hmFree(HashMap* m)
{
    for (size_t i = 0; i < m->nbuckets; ++i) {
        HmEntry* e = m->buckets[i];
        while (e) {
            HmEntry* next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(m->buckets);
    m->buckets = NULL;
    m->nbuckets = m->size = 0;
}

static void hmRehash(HashMap* m)                // 桶的个数翻倍，所有元素重新分配
{
    size_t nb = m->nbuckets * 2;
    HmEntry** nbk = calloc(nb, sizeof(HmEntry*));
    if (nbk == NULL) exit(1);
    for (size_t i = 0; i < m->nbuckets; ++i) {
        HmEntry* e = m->buckets[i];
        while (e) {
            HmEntry* next = e->next;
            size_t b = hashFNV(e->key) % nb;
            e->next = nbk[b];
            nbk[b] = e;
            e = next;
        }
    }
    free(m->buckets);
    m->buckets = nbk;
    m->nbuckets = nb;
}

void hmPut(HashMap* m, const char* key, int value)
{
    size_t b = hashFNV(key) % m->nbuckets;
    for (HmEntry* e = m->buckets[b]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) { e->value = value; return; }   // 已存在：更新
    }
    HmEntry* e = malloc(sizeof(HmEntry));
    if (e == NULL) exit(1);
    e->key = malloc(strlen(key) + 1);
    if (e->key == NULL) exit(1);
    strcpy(e->key, key);
    e->value = value;
    e->next = m->buckets[b];
    m->buckets[b] = e;
    if (++m->size > m->nbuckets) hmRehash(m);   // 负载因子超过 1 就扩容
}

int hmGet(const HashMap* m, const char* key)
{
    size_t b = hashFNV(key) % m->nbuckets;
    for (const HmEntry* e = m->buckets[b]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->value;
    }
    return -1;
}
