// 《代理模式》：实测虚代理——打开媒体库并显示第一屏的耗时，以及解码之后每次 draw 多绕一层代理的开销（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

constexpr int kW = 160, kH = 90, kPerPage = 12, kItems = 120;
static int g_decodes = 0;
static std::uint32_t g_screen = 0;

struct Thumbnail {
    virtual ~Thumbnail() = default;
    virtual void draw(int slot) = 0;
};

class VideoThumb final : public Thumbnail {
public:
    explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
        ++g_decodes;
        std::uint32_t h = 2166136261u;
        for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        for (int pass = 0; pass < 16; ++pass)
            for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
    }
    void draw(int slot) override { g_screen += pixels_[static_cast<std::size_t>(slot) * 4 % pixels_.size()] + static_cast<std::uint32_t>(slot); }

private:
    std::string path_;
    std::vector<std::uint8_t> pixels_;
};

class ThumbProxy final : public Thumbnail {
public:
    explicit ThumbProxy(std::string path) : path_(std::move(path)) {}
    void draw(int slot) override {
        if (!real_) real_ = std::make_unique<VideoThumb>(path_);
        real_->draw(slot);
    }

private:
    std::string path_;
    std::unique_ptr<VideoThumb> real_;
};

template <class T>
std::vector<std::unique_ptr<Thumbnail>> open_library() {
    std::vector<std::unique_ptr<Thumbnail>> v;
    for (int i = 0; i < kItems; ++i) v.push_back(std::make_unique<T>("clip" + std::to_string(i) + ".mp4"));
    return v;
}

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

template <class T>
void startup(const char* name) {
    std::vector<double> open_ms, first_ms;
    for (int r = 0; r < 7; ++r) {
        g_decodes = 0;
        auto t0 = Clock::now();
        auto lib = open_library<T>();
        open_ms.push_back(ms_since(t0));
        for (int s = 0; s < kPerPage; ++s) lib[static_cast<std::size_t>(s)]->draw(s);
        first_ms.push_back(ms_since(t0));
    }
    std::printf("%s 打开 %8.3f ms   打开并显示第一屏 %7.2f ms   解码 %3d 次\n", name, median(open_ms), median(first_ms), g_decodes);
}

// 解码完成之后，每次 draw 经过代理比直接调真实对象多花多少
void per_call() {
    constexpr int kCalls = 20'000'000;
    VideoThumb real("clip0.mp4");
    ThumbProxy proxy("clip0.mp4");
    std::vector<Thumbnail*> objs{&real, &proxy};
    objs[1]->draw(0);   // 先让代理把真实对象创建出来
    for (int k = 0; k < 2; ++k) {
        std::vector<double> ns;
        for (int r = 0; r < 7; ++r) {
            Thumbnail* t = objs[static_cast<std::size_t>(k)];
            auto t0 = Clock::now();
            for (int i = 0; i < kCalls; ++i) t->draw(i & 1023);
            ns.push_back(ms_since(t0) * 1e6 / kCalls);
        }
        std::printf("%s 每次 draw %.2f ns\n", k == 0 ? "直接调  ：" : "经过代理：", median(ns));
    }
}

int main() {
    startup<VideoThumb>("全部解码：");
    startup<ThumbProxy>("虚代理  ：");
    per_call();
    std::printf("(校验和 %u)\n", g_screen);
}
