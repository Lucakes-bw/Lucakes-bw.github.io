// 《单例》：朴素的懒汉式单例，8 个线程同时第一次访问
#include <atomic>
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

std::atomic<int> constructed{0};

class Config {
public:
    static Config* instance() {
        if (inst_ == nullptr)              // ① 检查
            inst_ = new Config;            // ② 创建并赋值
        return inst_;
    }

private:
    Config() {
        ++constructed;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));   // 模拟读取配置文件
    }
    static Config* inst_;
};
Config* Config::inst_ = nullptr;

int main() {
    std::atomic<bool> go{false};
    std::vector<const void*> seen(8);
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i)
        ts.emplace_back([&, i] {
            while (!go) {}                 // 等所有线程都就位，再一起开始
            seen[i] = Config::instance();
        });
    go = true;
    for (auto& t : ts) t.join();
    std::set<const void*> distinct(seen.begin(), seen.end());
    std::printf("构造函数执行了 %d 次，线程们拿到了 %zu 个不同的对象\n", constructed.load(), distinct.size());
}
