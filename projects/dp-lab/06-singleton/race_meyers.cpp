// 《单例》：Meyers 单例，同样 8 个线程同时第一次访问
#include <atomic>
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

std::atomic<int> constructed{0};

class Config {
public:
    static Config& instance() {
        static Config inst;                // C++11 起，局部静态变量的初始化是线程安全的
        return inst;
    }
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    Config() {
        ++constructed;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

int main() {
    std::atomic<bool> go{false};
    std::vector<const void*> seen(8);
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i)
        ts.emplace_back([&, i] {
            while (!go) {}
            seen[i] = &Config::instance();
        });
    go = true;
    for (auto& t : ts) t.join();
    std::set<const void*> distinct(seen.begin(), seen.end());
    std::printf("构造函数执行了 %d 次，线程们拿到了 %zu 个不同的对象\n", constructed.load(), distinct.size());
}
