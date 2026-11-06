// 命名空间：创建一个带有新命名空间的进程要多久
//   nscost 次数    对每一种命名空间，各创建这么多个子进程（子进程立即退出），打印平均耗时
#include <sched.h>
#include <signal.h>
#include <sys/wait.h>

#include "common.h"

static char stack[64 * 1024];

static int child(void* arg)
{
    (void)arg;
    return 0;
}

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    int n = atoi(argv[1]);
    struct {
        const char* name;
        int flags;
    } kinds[] = {
        {"无（普通的 fork）", 0},
        {"UTS（主机名）", CLONE_NEWUTS},
        {"IPC", CLONE_NEWIPC},
        {"PID", CLONE_NEWPID},
        {"mount", CLONE_NEWNS},
        {"user", CLONE_NEWUSER},
        {"cgroup", CLONE_NEWCGROUP},
        {"network", CLONE_NEWNET},
        {"全部", CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWCGROUP | CLONE_NEWNET},
    };
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        double t0 = nowSeconds();
        for (int i = 0; i < n; i++) {
            pid_t pid = clone(child, stack + sizeof stack, kinds[k].flags | SIGCHLD, NULL);
            CHECK(pid > 0);
            CHECK(waitpid(pid, NULL, 0) == pid);
        }
        printf("%-20s 每个 %8.1f 微秒\n", kinds[k].name, (nowSeconds() - t0) / n * 1e6);
        fflush(stdout);
    }
    return 0;
}
