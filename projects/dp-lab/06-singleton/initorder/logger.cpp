// 《单例》静态初始化顺序：logger.cpp 里的全局对象，构造时要用到 config.cpp 里的全局变量
#include <cstdio>

extern int g_dbPort;
int& dbPort();

struct Logger {
    Logger() {
        std::printf("Logger 构造时：g_dbPort = %d，dbPort() = %d\n", g_dbPort, dbPort());
    }
};

Logger g_logger;
