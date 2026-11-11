// 《策略模式》：同一个缩略图生成器的四种写法——虚函数接口、std::function、模板参数、std::variant
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

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

template <class Kernel>
Plane resample(const Plane& src, int w, int h, Kernel kernel) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

// 两个算法写成普通的函数对象，四种写法共用
struct NearestK {
    float operator()(const Plane& p, float x, float y) const {
        return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
    }
};
struct BilinearK {
    float operator()(const Plane& p, float x, float y) const {
        int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        float fx = x - x0, fy = y - y0;
        float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
        float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
        return top * (1 - fy) + bot * fy;
    }
};

// ① 虚函数接口：经典的 GoF 写法
struct Scaler {
    virtual ~Scaler() = default;
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};
template <class K> struct KernelScaler : Scaler {
    Plane scale(const Plane& src, int w, int h) const override { return resample(src, w, h, K{}); }
};
class ThumbV {
public:
    explicit ThumbV(std::unique_ptr<Scaler> s) : s_(std::move(s)) {}
    void set(std::unique_ptr<Scaler> s) { s_ = std::move(s); }
    Plane make(const Plane& f) const { return s_->scale(f, 4, 2); }
private:
    std::unique_ptr<Scaler> s_;
};

// ② std::function：策略就是「一个函数」，任何签名对得上的可调用对象都行
using ScaleFn = std::function<Plane(const Plane&, int, int)>;
class ThumbF {
public:
    explicit ThumbF(ScaleFn f) : f_(std::move(f)) {}
    void set(ScaleFn f) { f_ = std::move(f); }
    Plane make(const Plane& f) const { return f_(f, 4, 2); }
private:
    ScaleFn f_;
};

// ③ 模板参数：编译期选定，像 std::sort 的比较函数、unique_ptr 的删除器
template <class Kernel = BilinearK>
class ThumbT {
public:
    Plane make(const Plane& f) const { return resample(f, 4, 2, Kernel{}); }
};

// ④ std::variant：算法的集合是封闭的，按值存放，换算法就是赋一个新值
using AnyKernel = std::variant<NearestK, BilinearK>;
class ThumbVar {
public:
    explicit ThumbVar(AnyKernel k) : k_(k) {}
    void set(AnyKernel k) { k_ = k; }
    Plane make(const Plane& f) const {
        return std::visit([&](auto k) { return resample(f, 4, 2, k); }, k_);
    }
private:
    AnyKernel k_;
};

void show(const char* tag, const Plane& p) {
    std::printf("%-22s", tag);
    for (int x = 0; x < p.w; ++x) std::printf(" %3d", p.at(x, 0));
    std::printf("\n");
}

int main() {
    Plane frame(8, 4);
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    ThumbV v(std::make_unique<KernelScaler<NearestK>>());
    show("① 虚函数 nearest", v.make(frame));
    v.set(std::make_unique<KernelScaler<BilinearK>>());
    show("① 虚函数 bilinear", v.make(frame));

    ThumbF f([](const Plane& s, int w, int h) { return resample(s, w, h, NearestK{}); });
    show("② function nearest", f.make(frame));
    float gain = 1.2f;                           // lambda 可以带上自己的参数：先缩放再提亮 20%
    f.set([gain](const Plane& s, int w, int h) {
        return resample(s, w, h, [gain](const Plane& p, float x, float y) { return BilinearK{}(p, x, y) * gain; });
    });
    show("② function 提亮", f.make(frame));

    show("③ 模板 nearest", ThumbT<NearestK>{}.make(frame));
    show("③ 模板 bilinear", ThumbT<>{}.make(frame));

    ThumbVar var(NearestK{});
    show("④ variant nearest", var.make(frame));
    var.set(BilinearK{});
    show("④ variant bilinear", var.make(frame));
}
