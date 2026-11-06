// 崩溃恢复：子进程不停地写，父进程在随机的时刻用 SIGKILL 杀掉它，再打开数据库检查
//   crash 目录 同步间隔 毫秒数 [nocheck]
//   子进程每次 lsmPut 返回后，把「已确认的条数」记在共享内存中；
//   父进程重新打开数据库后，检查已确认的键是否都在，以及有没有多出来的
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lsm.h"

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "用法：crash 目录 同步间隔 毫秒数 [nocheck]\n");
        return 1;
    }
    LsmOptions o;
    lsmDefaultOptions(&o);
    o.syncEvery = atoi(argv[2]);
    volatile unsigned long long* acked =
        mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *acked = 0;
    pid_t pid = fork();
    if (pid == 0) {
        Lsm* db = lsmOpen(argv[1], &o);
        char k[24], v[100];
        memset(v, 'v', sizeof v);
        for (unsigned long long i = 0;; i++) {
            snprintf(k, sizeof k, "%016llu", i);
            lsmPut(db, k, 16, v, sizeof v);
            *acked = i + 1;  // put 返回了，对调用者来说这条已经写成功
        }
    }
    usleep((useconds_t)atoi(argv[3]) * 1000);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unsigned long long n = *acked;
    printf("杀死子进程时，已确认写入 %llu 条\n", n);
    if (argc == 5) return 0;  // nocheck：保留现场，不打开数据库

    Lsm* db = lsmOpen(argv[1], &o);
    unsigned long long present = 0, extra = 0;
    char k[24];
    for (unsigned long long i = 0; i < n + 1000; i++) {
        snprintf(k, sizeof k, "%016llu", i);
        char* v;
        uint32_t vl;
        if (lsmGet(db, k, 16, &v, &vl)) {
            free(v);
            if (i < n) present++;
            else extra++;
        }
    }
    printf("重新打开后：已确认的 %llu 条中找到 %llu 条，丢失 %llu 条；未确认就被杀死的写入找到 %llu 条\n", n, present,
           n - present, extra);
    lsmClose(db);
    return 0;
}
