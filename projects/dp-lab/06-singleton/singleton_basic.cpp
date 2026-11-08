// 《单例》：Meyers 单例的完整写法
#include <iostream>
#include <string>

class Logger {
public:
    static Logger& instance() {
        static Logger inst;                    // 第一次调用时构造，之后一直是同一个对象
        return inst;
    }
    Logger(const Logger&) = delete;            // 禁止拷贝：不然 Logger copy = Logger::instance(); 会悄悄造出第二个
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& msg) { std::cout << '[' << ++count_ << "] " << msg << '\n'; }

private:
    Logger() { std::cout << "Logger 构造\n"; }  // 构造函数私有：外面不能 new，也不能直接定义变量
    ~Logger() { std::cout << "Logger 析构\n"; }
    int count_ = 0;
};

void connect() { Logger::instance().log("连接数据库"); }
void query() { Logger::instance().log("执行查询"); }

int main() {
    std::cout << "main 开始\n";
    connect();
    query();
    std::cout << "两次拿到的是同一个对象：" << (&Logger::instance() == &Logger::instance()) << '\n';
    std::cout << "main 结束\n";
}
