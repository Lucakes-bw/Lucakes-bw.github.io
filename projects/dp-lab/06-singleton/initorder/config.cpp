// 《单例》静态初始化顺序：config.cpp 里的全局变量，需要在运行时计算初值
#include <cstdlib>

int computePort() {
    const char* env = std::getenv("DB_PORT");     // 运行时才能确定，所以是「动态初始化」
    return env ? std::atoi(env) : 5432;
}

int g_dbPort = computePort();

int& dbPort() {                                    // 改用「第一次使用时才构造」
    static int port = computePort();
    return port;
}
