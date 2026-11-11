// 《策略模式》：同一个双线性/最近邻核，用 switch、虚函数、std::function、variant、模板参数五种方式按像素调用，再和「整帧调用一次虚函数」比（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

struct Plane {
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

inline float nearest(const Plane& p, float x, float y) {
    return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
}
inline float bilinear(const Plane& p, float x, float y) {
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    float fx = x - x0, fy = y - y0;
    float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
    float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
    return top * (1 - fy) + bot * fy;
}

// 外层循环：kernel 是怎么传进来的，就是下面几种写法的区别
template <class Sample>
void loop(const Plane& src, Plane& dst, Sample sample) {
    float rx = static_cast<float>(src.w) / dst.w, ry = static_cast<float>(src.h) / dst.h;
    for (int y = 0; y < dst.h; ++y)
        for (int x = 0; x < dst.w; ++x) {
            float v = sample(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * dst.w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
}

// 0. enum + switch（问题版）
enum class Algo { Nearest, Bilinear };
NOINLINE void scaleSwitch(const Plane& s, Plane& d, Algo a) {
    loop(s, d, [a](const Plane& p, float x, float y) {
        switch (a) {
        case Algo::Nearest: return nearest(p, x, y);
        case Algo::Bilinear: return bilinear(p, x, y);
        }
        return 0.f;
    });
}

// 1. 虚函数，每个像素调用一次
struct Kernel {
    virtual ~Kernel() = default;
    virtual float sample(const Plane& p, float x, float y) const = 0;
};
struct NearestK : Kernel { float sample(const Plane& p, float x, float y) const override { return nearest(p, x, y); } };
struct BilinearK : Kernel { float sample(const Plane& p, float x, float y) const override { return bilinear(p, x, y); } };
NOINLINE void scaleVirtual(const Plane& s, Plane& d, const Kernel& k) {
    loop(s, d, [&k](const Plane& p, float x, float y) { return k.sample(p, x, y); });
}

// 2. std::function，每个像素调用一次
using KernelFn = std::function<float(const Plane&, float, float)>;
NOINLINE void scaleFunction(const Plane& s, Plane& d, const KernelFn& f) {
    loop(s, d, [&f](const Plane& p, float x, float y) { return f(p, x, y); });
}

// 3. std::variant + std::visit，每个像素 visit 一次
struct NearestV { float operator()(const Plane& p, float x, float y) const { return nearest(p, x, y); } };
struct BilinearV { float operator()(const Plane& p, float x, float y) const { return bilinear(p, x, y); } };
using KernelVar = std::variant<NearestV, BilinearV>;
NOINLINE void scaleVariant(const Plane& s, Plane& d, const KernelVar& v) {
    loop(s, d, [&v](const Plane& p, float x, float y) {
        return std::visit([&](const auto& k) { return k(p, x, y); }, v);
    });
}

// 4. 模板参数：编译期就知道是哪个核，可以内联进循环
template <class K>
NOINLINE void scaleTemplate(const Plane& s, Plane& d, K k) { loop(s, d, k); }

// 5. 模板参数，但禁止内联核函数：看看快是因为「没有虚调用」还是因为「内联」
struct NearestNI { NOINLINE float operator()(const Plane& p, float x, float y) const { return nearest(p, x, y); } };
struct BilinearNI { NOINLINE float operator()(const Plane& p, float x, float y) const { return bilinear(p, x, y); } };

// 6. 策略的粒度放大到「整帧」：虚函数每帧只调用一次，里面用模板
struct FrameScaler {
    virtual ~FrameScaler() = default;
    virtual void scale(const Plane& s, Plane& d) const = 0;
};
struct NearestF : FrameScaler { void scale(const Plane& s, Plane& d) const override { loop(s, d, NearestV{}); } };
struct BilinearF : FrameScaler { void scale(const Plane& s, Plane& d) const override { loop(s, d, BilinearV{}); } };

volatile int g_pick = 1;                         // 运行时才决定用哪个核，防止编译器提前看穿

template <class F>
double medianMs(F run) {
    std::vector<double> t;
    run();                                       // 先热身一次
    for (int i = 0; i < 7; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        run();
        t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

int main() {
    Plane src(1280, 720), dst(1920, 1080);       // 720p 的帧放大到 1080p 全屏
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            src.px[static_cast<size_t>(y) * src.w + x] = static_cast<uint8_t>((x * 7 + y * 13 + (x * y) % 31) & 255);

    NearestK nk; BilinearK bk;
    const Kernel* ks[] = {&nk, &bk};
    NearestF nf; BilinearF bf;
    const FrameScaler* fs[] = {&nf, &bf};
    unsigned check = 0;
    auto sum = [&] { for (int i = 0; i < 1920 * 1080; i += 4099) check += dst.px[static_cast<size_t>(i)]; };

    for (int i = 0; i < 50; ++i) scaleTemplate(src, dst, BilinearV{});   // 让 CPU 先升到稳定频率
    std::printf("%-26s %10s %10s   （ms/帧，1280x720 → 1920x1080，7 次中位数）\n", "写法", "nearest", "bilinear");
    auto row = [&](const char* name, auto runFor) {
        double r[2];
        for (int k = 0; k < 2; ++k) { g_pick = k; r[k] = medianMs([&] { runFor(g_pick); }); sum(); }
        std::printf("%-26s %10.2f %10.2f\n", name, r[0], r[1]);
    };
    row("0 enum + switch（每像素）", [&](int k) { scaleSwitch(src, dst, k ? Algo::Bilinear : Algo::Nearest); });
    row("1 虚函数（每像素）", [&](int k) { scaleVirtual(src, dst, *ks[k]); });
    row("2 std::function（每像素）", [&](int k) {
        KernelFn f = k ? KernelFn(BilinearV{}) : KernelFn(NearestV{});
        scaleFunction(src, dst, f);
    });
    row("3 variant + visit（每像素）", [&](int k) {
        KernelVar v = k ? KernelVar(BilinearV{}) : KernelVar(NearestV{});
        scaleVariant(src, dst, v);
    });
    row("4 模板参数", [&](int k) { k ? scaleTemplate(src, dst, BilinearV{}) : scaleTemplate(src, dst, NearestV{}); });
    row("5 模板参数 + 禁止内联", [&](int k) { k ? scaleTemplate(src, dst, BilinearNI{}) : scaleTemplate(src, dst, NearestNI{}); });
    row("6 虚函数（每帧一次）", [&](int k) { fs[k]->scale(src, dst); });
    std::printf("(校验和 %u)\n", check);
}
