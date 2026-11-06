// 布隆过滤器的假阳性率：每个键用多少位、多少个哈希函数
//   bloomtest 键的个数   放入 n 个键，再查 n 个不存在的键，统计误判的比例，和理论值对比
// 哈希的做法和 lsm.c 中的完全相同
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash1(const char* k, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint8_t)k[i]) * 16777619u;
    return h;
}

static void add(uint8_t* bits, uint32_t nbits, int k, const char* key, uint32_t kl)
{
    uint32_t h = hash1(key, kl), delta = (h >> 17) | (h << 15);
    for (int i = 0; i < k; i++, h += delta) bits[(h % nbits) / 8] |= (uint8_t)(1u << ((h % nbits) % 8));
}

static int mayContain(const uint8_t* bits, uint32_t nbits, int k, const char* key, uint32_t kl)
{
    uint32_t h = hash1(key, kl), delta = (h >> 17) | (h << 15);
    for (int i = 0; i < k; i++, h += delta)
        if (!(bits[(h % nbits) / 8] & (1u << ((h % nbits) % 8)))) return 0;
    return 1;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    long n = atol(argv[1]);
    char key[17];
    printf("每键位数  哈希个数  实测假阳性率  理论值\n");
    for (int bpk = 2; bpk <= 16; bpk += 2) {
        int k = (int)(bpk * 0.69);
        if (k < 1) k = 1;
        uint32_t nbits = (uint32_t)(n * bpk);
        uint8_t* bits = calloc((nbits + 7) / 8, 1);
        for (long i = 0; i < n; i++) {
            snprintf(key, sizeof key, "%016lx", (unsigned long)i * 2654435761UL);
            add(bits, nbits, k, key, 16);
        }
        long fp = 0;
        for (long i = 0; i < n; i++) {
            snprintf(key, sizeof key, "%016lx", (unsigned long)(i + n) * 2654435761UL);
            fp += mayContain(bits, nbits, k, key, 16);
        }
        double theory = pow(1 - exp(-(double)k / bpk), k);
        printf("%8d  %8d  %11.3f%%  %6.3f%%\n", bpk, k, 100.0 * fp / n, 100 * theory);
        free(bits);
    }
    return 0;
}
