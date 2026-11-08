// 《单例》静态初始化顺序：main 开始时，所有全局对象都已经构造完了
#include <cstdio>

extern int g_dbPort;

int main() {
    std::printf("main 开始时：g_dbPort = %d\n", g_dbPort);
}
