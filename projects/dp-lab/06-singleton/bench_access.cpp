// 《单例》：五种「全局唯一对象」的访问开销，单线程和 8 线程
// 编译：clang++ -std=c++17 -O2 -pthread bench_access.cpp -o bench_access（或 g++）
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

struct Config { int value = 1; };

#define NOINLINE __attribute__((noinline))   // 模拟从别的源文件调用：编译器看不到函数体，不能把检查提到循环外面

// ① 普通全局对象（程序启动时构造）
Config g_config;
NOINLINE Config& viaGlobal() { return g_config; }

// ② Meyers 单例：局部静态变量
NOINLINE Config& viaMeyers() {
    static Config c;
    return c;
}

// ③ std::call_once
std::once_flag g_once;
Config* g_oncePtr = nullptr;
NOINLINE Config& viaCallOnce() {
    std::call_once(g_once, [] { g_oncePtr = new Config; });
    return *g_oncePtr;
}

// ④ 每次都加锁
std::mutex g_mu;
Config* g_lockPtr = nullptr;
NOINLINE Config& viaMutex() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_lockPtr) g_lockPtr = new Config;
    return *g_lockPtr;
}

// ⑤ 正确的双重检查锁定：atomic + acquire / release
std::atomic<Config*> g_dclp{nullptr};
std::mutex g_dclpMu;
NOINLINE Config& viaDclp() {
    Config* p = g_dclp.load(std::memory_order_acquire);
    if (!p) {
        std::lock_guard<std::mutex> lock(g_dclpMu);
        p = g_dclp.load(std::memory_order_relaxed);
        if (!p) {
            p = new Config;
            g_dclp.store(p, std::memory_order_release);
        }
    }
    return *p;
}

constexpr long OPS = 20'000'000;
volatile long sink;

// threads 个线程各访问 OPS / threads 次，返回每次访问的平均耗时（总耗时 / 总次数，纳秒），取 9 次的中位数
template <typename F>
double measure(F get, int threads) {
    std::vector<double> runs;
    for (int r = 0; r < 9; ++r) {
        std::atomic<bool> go{false};
        std::vector<std::thread> ts;
        std::atomic<long> total{0};
        for (int t = 0; t < threads; ++t)
            ts.emplace_back([&] {
                while (!go) {}
                long s = 0;
                for (long i = 0; i < OPS / threads; ++i) s += get().value;
                total += s;
            });
        auto t0 = std::chrono::steady_clock::now();
        go = true;
        for (auto& t : ts) t.join();
        runs.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / OPS);
        sink = total;
    }
    std::nth_element(runs.begin(), runs.begin() + 4, runs.end());
    return runs[4];
}

int main() {
    struct { const char* name; Config& (*get)(); } ways[] = {
        {"① 全局对象", viaGlobal}, {"② Meyers 单例", viaMeyers}, {"③ std::call_once", viaCallOnce},
        {"④ 每次加锁", viaMutex}, {"⑤ atomic 双重检查", viaDclp}};
    std::printf("%-20s %12s %12s\n", "ns / 次访问", "1 个线程", "8 个线程");
    for (auto& w : ways) std::printf("%-20s %12.2f %12.2f\n", w.name, measure(w.get, 1), measure(w.get, 8));
}
