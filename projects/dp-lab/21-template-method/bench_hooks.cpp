// 《模板方法模式》：钩子的调用开销——给 10 秒音频调音量，钩子每个采样调一次 vs 每个缓冲区调一次（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

constexpr int kBlock = 1024 * 2;                 // 一个音频包：1024 个采样 × 2 声道
constexpr int kBlocks = 470;                     // bbb-10s.mp4 有 470 个音频包

// 1. 虚函数钩子，每个采样调一次
struct SampleJob {
    virtual ~SampleJob() = default;
    void run(std::vector<float>& pcm) {          // 模板方法
        for (float& s : pcm) s = processSample(s);
    }
    virtual float processSample(float s) = 0;
};
struct SampleGain : SampleJob {
    float g;
    explicit SampleGain(float g_) : g(g_) {}
    float processSample(float s) override { return s * g; }
};

// 2. 虚函数钩子，每个缓冲区调一次
struct BlockJob {
    virtual ~BlockJob() = default;
    void run(std::vector<float>& pcm) {
        for (size_t i = 0; i < pcm.size(); i += kBlock) processBlock(pcm.data() + i, kBlock);
    }
    virtual void processBlock(float* p, int n) = 0;
};
struct BlockGain : BlockJob {
    float g;
    explicit BlockGain(float g_) : g(g_) {}
    void processBlock(float* p, int n) override { for (int i = 0; i < n; ++i) p[i] *= g; }
};

// 3. CRTP，每个采样调一次（编译期绑定，能内联）
template <class D>
struct CrtpJob {
    void run(std::vector<float>& pcm) {
        for (float& s : pcm) s = static_cast<D*>(this)->processSample(s);
    }
};
struct CrtpGain : CrtpJob<CrtpGain> {
    float g;
    explicit CrtpGain(float g_) : g(g_) {}
    float processSample(float s) const { return s * g; }
};

// 4. std::function，每个采样调一次
struct FuncJob {
    std::function<float(float)> process;
    void run(std::vector<float>& pcm) {
        for (float& s : pcm) s = process(s);
    }
};

template <class F>
double median_ms(F&& f) {
    std::vector<double> t;
    for (int r = 0; r < 7; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; ++k) f();        // 一次计时处理 20 遍，取每遍的时间
        auto t1 = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / 20);
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

int main(int argc, char**) {
    std::vector<float> pcm(static_cast<size_t>(kBlock) * kBlocks);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<float>(i % 200) / 100.0f - 1.0f;
    float g = argc > 5 ? 1.0f : 0.999f;          // 让编译器看不出增益是多少

    std::unique_ptr<SampleJob> a = std::make_unique<SampleGain>(g);   // 通过基类指针调用，防止编译器去虚化
    std::unique_ptr<BlockJob> b = std::make_unique<BlockGain>(g);
    CrtpGain c(g);
    FuncJob d{[g](float s) { return s * g; }};

    double ta = median_ms([&] { a->run(pcm); });
    double tb = median_ms([&] { b->run(pcm); });
    double tc = median_ms([&] { c.run(pcm); });
    double td = median_ms([&] { d.run(pcm); });
    std::printf("10 秒立体声 %zu 个采样，每遍的毫秒数（7 次取中位数）：\n", pcm.size());
    std::printf("  虚函数钩子，每个采样一次    %.3f ms\n", ta);
    std::printf("  虚函数钩子，每个缓冲区一次  %.3f ms\n", tb);
    std::printf("  CRTP，每个采样一次          %.3f ms\n", tc);
    std::printf("  std::function，每个采样一次 %.3f ms\n", td);
    std::printf("（校验 %.3f）\n", pcm[12345]);
}
