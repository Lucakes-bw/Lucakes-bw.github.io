// cgroup 的 pids 限制：不停地 fork，直到失败
//   forkbomb 最多个数   每个子进程都暂停不退出；打印成功了多少个、失败的原因，然后结束所有的子进程
#include <signal.h>
#include <sys/wait.h>

#include "common.h"

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    int max = atoi(argv[1]), n = 0;
    pid_t* kids = malloc(sizeof(pid_t) * (size_t)max);
    for (; n < max; n++) {
        pid_t p = fork();
        if (p < 0) {
            printf("第 %d 次 fork 失败：%s\n", n + 1, strerror(errno));
            break;
        }
        if (p == 0) {
            pause();
            _exit(0);
        }
        kids[n] = p;
    }
    printf("成功创建了 %d 个子进程\n", n);
    for (int i = 0; i < n; i++) kill(kids[i], SIGKILL);
    while (wait(NULL) > 0) {
    }
    return 0;
}
