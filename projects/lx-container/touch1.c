// overlayfs 的写时复制：打开下层的文件写 1 个字节要多久
//   touch1 文件   以读写方式打开（不截断），在开头写 1 个字节，打印 open 和 write 各用了多久
#include <fcntl.h>

#include "common.h"

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    double t0 = nowSeconds();
    int fd = open(argv[1], O_RDWR);
    CHECK(fd >= 0);
    double t1 = nowSeconds();
    CHECK(write(fd, "x", 1) == 1);
    double t2 = nowSeconds();
    close(fd);
    printf("open %.1f ms，write %.3f ms\n", (t1 - t0) * 1e3, (t2 - t1) * 1e3);
    return 0;
}
