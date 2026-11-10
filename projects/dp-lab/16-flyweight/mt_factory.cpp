// 《享元模式》：多线程共用一个享元工厂——不加锁会出错，加锁的代价有多大（mutex / shared_mutex / 线程局部缓存）
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static std::atomic<int> g_raster{0};
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    char32_t ch;
    int advance;
    std::vector<std::uint8_t> bitmap;
};
const int px = 48;

// ① 一把 mutex：查找和插入都要加锁
class MutexFactory {
public:
    const Glyph& get(char32_t ch) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, px, rasterize(ch, px)}).first;
        return it->second;
    }
    std::size_t size() const { return cache_.size(); }
private:
    std::mutex mu_;
    std::unordered_map<char32_t, Glyph> cache_;
};

// ② 读写锁：命中时只加共享锁；没命中再加独占锁，并且要重新查一次（别的线程可能刚插进去）
class SharedMutexFactory {
public:
    const Glyph& get(char32_t ch) {
        {
            std::shared_lock<std::shared_mutex> lk(mu_);
            auto it = cache_.find(ch);
            if (it != cache_.end()) return it->second;
        }
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, px, rasterize(ch, px)}).first;
        return it->second;
    }
    std::size_t size() const { return cache_.size(); }
private:
    std::shared_mutex mu_;
    std::unordered_map<char32_t, Glyph> cache_;
};

// ③ 每个线程先查自己的小表（不用加锁），查不到再去共享工厂（加锁）
class LocalCacheFactory {
public:
    const Glyph& get(char32_t ch) {
        struct Local {
            const LocalCacheFactory* owner = nullptr;
            std::unordered_map<char32_t, const Glyph*> map;
        };
        thread_local Local local;
        if (local.owner != this) { local.map.clear(); local.owner = this; }  // 换了工厂，旧指针作废
        auto it = local.map.find(ch);
        if (it != local.map.end()) return *it->second;
        const Glyph& g = shared_.get(ch);
        local.map.emplace(ch, &g);
        return g;
    }
    std::size_t size() const { return shared_.size(); }
private:
    MutexFactory shared_;
};

// 基准：只有一个线程，不加锁
class NoLockFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, px, rasterize(ch, px)}).first;
        return it->second;
    }
    std::size_t size() const { return cache_.size(); }
private:
    std::unordered_map<char32_t, Glyph> cache_;
};

using Clock = std::chrono::steady_clock;
const int kThreads = 4, kReps = 10;

// 每个线程把整条字幕查若干遍（各自从不同的位置开始），总次数固定；返回总耗时
template <class Factory>
double run(const std::vector<char32_t>& all, int threads, std::size_t& distinct) {
    g_raster = 0;
    Factory f;
    for (char32_t ch : all) f.get(ch);  // 先单线程把所有字都光栅化好，下面只测查找（全部命中）
    auto t0 = Clock::now();
    std::vector<std::thread> ts;
    std::vector<long long> sums(threads);
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
            long long s = 0;
            std::size_t n = all.size(), off = n / threads * t;
            for (int r = 0; r < kReps * kThreads / threads; ++r)
                for (std::size_t i = 0; i < n; ++i) s += f.get(all[(i + off) % n]).bitmap[0];
            sums[t] = s;
        });
    for (auto& th : ts) th.join();
    auto t1 = Clock::now();
    distinct = f.size();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

template <class Factory>
void report(const char* name, const std::vector<char32_t>& all, int threads) {
    std::vector<double> v;
    std::size_t distinct = 0;
    for (int i = 0; i < 7; ++i) v.push_back(run<Factory>(all, threads, distinct));
    std::sort(v.begin(), v.end());
    double total = double(all.size()) * kReps * kThreads;
    std::printf("%s，%d 线程：%7.1f ms，每次 get %5.1f ns；光栅化 %d 次（%zu 种字）\n",
                name, threads, v[3], v[3] * 1e6 / total, g_raster.load(), distinct);
}

int main() {
    auto track = make_track(1400);
    std::vector<char32_t> all;
    for (auto& t : track) all.insert(all.end(), t.begin(), t.end());
    std::printf("硬件线程数：%u；共 %d × %zu 次 get\n", std::thread::hardware_concurrency(), kThreads * kReps, all.size());
    report<NoLockFactory>("不加锁", all, 1);
    report<MutexFactory>("mutex", all, 1);
    report<MutexFactory>("mutex", all, kThreads);
    report<SharedMutexFactory>("shared_mutex", all, kThreads);
    report<LocalCacheFactory>("线程局部缓存 + mutex", all, kThreads);
}
