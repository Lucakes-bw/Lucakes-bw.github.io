// 迷你容器：把本章的内容组合起来，用两百多行 C 运行一个「容器」
//   minictr [-h 主机名] [-m 内存MB] [-p 最多进程数] [-c CPU百分比] 根文件系统目录 命令 [参数...]
// 需要 root 和 cgroup v2（/sys/fs/cgroup）。
//   1. 父进程：创建 cgroup，写入限制
//   2. clone 出子进程，带上新的 PID、mount、UTS、IPC、network 命名空间
//   3. 父进程把子进程放进 cgroup，然后通知它继续
//   4. 子进程：新的 cgroup 命名空间；主机名；overlayfs 叠在根文件系统上；pivot_root；挂载 /proc 等；
//      去掉危险的 capabilities；作为容器的 1 号进程，启动命令并回收所有的子进程
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/capability.h>

#include "common.h"

struct Config {
    const char* hostname;
    const char* rootfs;
    char** argv;
    char dir[320];    // 这个容器的工作目录：overlayfs 的 upper、work、merged
    int syncFd;       // 管道的读端：等父进程准备好
};

static char stack[1 << 20];

// 容器中保留的 capabilities 之外，全部从边界集中去掉（和 Docker 默认保留的那 14 个相同）
static const int keep[] = {CAP_CHOWN, CAP_DAC_OVERRIDE, CAP_FOWNER, CAP_FSETID, CAP_KILL,
                           CAP_SETGID, CAP_SETUID, CAP_SETPCAP, CAP_NET_BIND_SERVICE, CAP_NET_RAW,
                           CAP_SYS_CHROOT, CAP_MKNOD, CAP_AUDIT_WRITE, CAP_SETFCAP};

static void dropCaps(void)
{
    for (int c = 0; c <= CAP_LAST_CAP; c++) {
        int k = 0;
        for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++)
            if (keep[i] == c) k = 1;
        if (!k) CHECK(prctl(PR_CAPBSET_DROP, c, 0, 0, 0) == 0);
    }
}

static void bindDev(const char* root, const char* name)
{
    char src[64], dst[512];
    snprintf(src, sizeof src, "/dev/%s", name);
    snprintf(dst, sizeof dst, "%s/dev/%s", root, name);
    int fd = open(dst, O_CREAT | O_WRONLY, 0666);
    CHECK(fd >= 0);
    close(fd);
    CHECK(mount(src, dst, NULL, MS_BIND, NULL) == 0);
}

static void setupRoot(struct Config* c)
{
    char lower[512], upper[400], work[400], merged[400], opts[1400], old[500];
    snprintf(upper, sizeof upper, "%s/upper", c->dir);
    snprintf(work, sizeof work, "%s/work", c->dir);
    snprintf(merged, sizeof merged, "%s/merged", c->dir);
    snprintf(lower, sizeof lower, "%s", c->rootfs);

    // 新的 mount 命名空间中的挂载，不要传播回宿主
    CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
    // overlayfs：根文件系统作为只读的下层，容器中的修改都写进 upper
    snprintf(opts, sizeof opts, "lowerdir=%s,upperdir=%s,workdir=%s", lower, upper, work);
    CHECK(mount("overlay", merged, "overlay", 0, opts) == 0);
    // 最小的 /dev：tmpfs 上放几个绑定挂载的设备文件
    char dev[500];
    snprintf(dev, sizeof dev, "%s/dev", merged);
    CHECK(mount("tmpfs", dev, "tmpfs", MS_NOSUID, "mode=755") == 0);
    const char* devs[] = {"null", "zero", "random", "urandom", "tty"};
    for (size_t i = 0; i < sizeof devs / sizeof devs[0]; i++) bindDev(merged, devs[i]);

    // pivot_root：merged 成为新的根，原来的根挂到 /.oldroot，然后卸载掉
    snprintf(old, sizeof old, "%s/.oldroot", merged);
    mkdir(old, 0700);
    CHECK(syscall(SYS_pivot_root, merged, old) == 0);
    CHECK(chdir("/") == 0);
    CHECK(umount2("/.oldroot", MNT_DETACH) == 0);
    rmdir("/.oldroot");

    CHECK(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
    CHECK(mount("sysfs", "/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
    CHECK(mount("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
    CHECK(mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, NULL) == 0);
}

// 容器中的 1 号进程：启动命令，回收所有退出的子进程，把 SIGTERM、SIGINT 转发给命令
static pid_t cmdPid;
static void forward(int sig)
{
    if (cmdPid > 0) kill(cmdPid, sig);
}

static int containerMain(void* arg)
{
    struct Config* c = arg;
    char b;
    CHECK(read(c->syncFd, &b, 1) == 1);  // 等父进程把我们放进 cgroup
    close(c->syncFd);
    CHECK(unshare(CLONE_NEWCGROUP) == 0);  // 现在所在的 cgroup 成为容器看到的 cgroup 根
    CHECK(sethostname(c->hostname, strlen(c->hostname)) == 0);
    setupRoot(c);
    dropCaps();

    signal(SIGTERM, forward);
    signal(SIGINT, forward);
    cmdPid = fork();
    CHECK(cmdPid >= 0);
    if (cmdPid == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("HOSTNAME", c->hostname, 1);
        execvp(c->argv[0], c->argv);
        fprintf(stderr, "minictr: 无法执行 %s：%s\n", c->argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0, cmdStatus = 0;
    pid_t p;
    while ((p = wait(&status)) > 0 || errno == EINTR)
        if (p == cmdPid) cmdStatus = status;
    return WIFEXITED(cmdStatus) ? WEXITSTATUS(cmdStatus) : 128 + WTERMSIG(cmdStatus);
}

static void cgWrite(const char* cg, const char* file, const char* value)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", cg, file);
    if (writeFile(path, value) != 0) {
        fprintf(stderr, "minictr: 写 %s 失败：%s\n", path, strerror(errno));
        exit(1);
    }
}

int main(int argc, char** argv)
{
    struct Config c = {.hostname = "minictr"};
    long memMB = 0, pids = 0, cpu = 0;
    int opt;
    while ((opt = getopt(argc, argv, "+h:m:p:c:")) != -1) {
        if (opt == 'h') c.hostname = optarg;
        else if (opt == 'm') memMB = atol(optarg);
        else if (opt == 'p') pids = atol(optarg);
        else if (opt == 'c') cpu = atol(optarg);
        else return 1;
    }
    if (argc - optind < 2) {
        fprintf(stderr, "用法：minictr [-h 主机名] [-m 内存MB] [-p 最多进程数] [-c CPU百分比] 根文件系统 命令...\n");
        return 1;
    }
    c.rootfs = argv[optind];
    c.argv = argv + optind + 1;

    // 1. cgroup
    char cg[256], v[64];
    snprintf(cg, sizeof cg, "/sys/fs/cgroup/minictr-%d", getpid());
    CHECK(mkdir(cg, 0755) == 0);
    if (memMB) {
        snprintf(v, sizeof v, "%ld", memMB << 20);
        cgWrite(cg, "memory.max", v);
        cgWrite(cg, "memory.swap.max", "0");
    }
    if (pids) {
        snprintf(v, sizeof v, "%ld", pids);
        cgWrite(cg, "pids.max", v);
    }
    if (cpu) {  // 每 100 毫秒最多用 cpu × 1 毫秒，即 cpu% 个 CPU
        snprintf(v, sizeof v, "%ld 100000", cpu * 1000);
        cgWrite(cg, "cpu.max", v);
    }

    // 2. overlayfs 用的目录：放在根文件系统旁边（upper 不能在另一个 overlayfs 上，比如 Docker 容器的 /var/lib）
    char base[300], path[400];
    snprintf(base, sizeof base, "%s.minictr", c.rootfs);
    snprintf(c.dir, sizeof c.dir, "%s/%d", base, getpid());
    const char* sub[] = {"", "/upper", "/work", "/merged"};
    mkdir(base, 0700);
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s%s", c.dir, sub[i]);
        CHECK(mkdir(path, 0700) == 0);
    }

    // 3. 带新命名空间的子进程
    int fds[2];
    CHECK(pipe(fds) == 0);
    c.syncFd = fds[0];
    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | SIGCHLD;
    pid_t child = clone(containerMain, stack + sizeof stack, flags, &c);
    CHECK(child > 0);
    close(fds[0]);
    snprintf(v, sizeof v, "%d", child);
    cgWrite(cg, "cgroup.procs", v);  // 在子进程做任何事之前放进 cgroup
    CHECK(write(fds[1], "g", 1) == 1);
    close(fds[1]);

    int status;
    CHECK(waitpid(child, &status, 0) == child);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);

    // 4. 清理：cgroup 和 overlayfs 的目录（容器中写的东西都在 upper 中，一起删掉）
    rmdir(cg);
    snprintf(path, sizeof path, "rm -rf %s", c.dir);
    if (system(path) != 0) fprintf(stderr, "minictr: 清理 %s 失败\n", c.dir);
    return code;
}
