// 容器中的 1 号进程：要不要回收孤儿进程
//   pid1 lazy 命令...   只等待自己启动的那个命令，不管别的子进程（很多程序作为 1 号进程时就是这样）
//   pid1 reap 命令...   像 tini 一样：回收所有退出的子进程，把收到的 SIGTERM 转发给命令
// 在新的 PID 命名空间中运行：unshare --pid --fork --mount-proc ./build/pid1 lazy sh -c '...'
#include <signal.h>
#include <sys/wait.h>

#include "common.h"

static pid_t cmd;

static void forward(int sig)
{
    if (cmd > 0) kill(cmd, sig);
}

int main(int argc, char** argv)
{
    CHECK(argc >= 3);
    int reap = strcmp(argv[1], "reap") == 0;
    printf("pid1：我的进程号是 %d\n", getpid());
    fflush(stdout);
    if (reap) signal(SIGTERM, forward);
    cmd = fork();
    CHECK(cmd >= 0);
    if (cmd == 0) {
        execvp(argv[2], argv + 2);
        _exit(127);
    }
    int status = 0;
    for (;;) {
        pid_t p = reap ? waitpid(-1, &status, 0) : waitpid(cmd, &status, 0);
        if (p < 0 && errno == EINTR) continue;
        if (p == cmd || p < 0) break;
        printf("pid1：回收了孤儿进程 %d\n", p);
    }
    printf("pid1：命令退出，状态 %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    return 0;
}
