// 《桥接模式》：多一层间接调用有多大开销？接口的粒度呢？（请用 -O2 编译）
//   继承：每个组合一个类，基类的 draw 调用虚函数 fill
//   桥接：视图通过 unique_ptr<Renderer> 调用虚函数 fill
//   桥接（逐像素）：同样是桥接，但接口是 setPixel，每画一帧要调用 30 × 7 = 210 次
//   模板：编译期的桥接，后端是模板参数，没有虚函数（也分粗粒度和逐像素两种）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Rect { int x, y, w, h; };
constexpr int W = 30, H = 7;

struct Canvas {                                      // 真正干活的部分：写一个 30 × 7 的字符画布
    char px[H][W];
    void fill(Rect r, char c) {
        for (int y = r.y; y < r.y + r.h; ++y) std::fill(px[y] + r.x, px[y] + r.x + r.w, c);
    }
    void set(int x, int y, char c) { px[y][x] = c; }
};

// 一帧的内容：5 个矩形（画面 4 条 + 进度条），粗粒度的接口调用 5 次 fill
template <class F> void layout(int t, F fill) {
    for (int i = 0; i < 4; ++i) fill(Rect{0, i, W, 1}, char('a' + (t + i) % 26));
    fill(Rect{0, 5, W, 2}, char('0' + t % 10));
}
char pixelAt(int t, int x, int y) {                  // 同样的画面，逐像素算出来
    (void)x;
    return y < 4 ? char('a' + (t + y) % 26) : char('0' + t % 10);
}

// ---------- 继承 ----------
class ViewI {
public:
    virtual ~ViewI() = default;
    void draw(int t) { layout(t, [this](Rect r, char c) { fill(r, c); }); }
    virtual void fill(Rect r, char c) = 0;
    virtual char sample() const = 0;
};
class ViewSoftI : public ViewI {
public:
    void fill(Rect r, char c) override { cv_.fill(r, c); }
    char sample() const override { return cv_.px[6][29]; }
private:
    Canvas cv_;
};
class ViewOtherI : public ViewI {                    // 另一个后端，让编译器没法确定调用哪个
public:
    void fill(Rect, char) override {}
    char sample() const override { return 0; }
};

// ---------- 桥接 ----------
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void fill(Rect r, char c) = 0;
    virtual void setPixel(int x, int y, char c) = 0;
    virtual char sample() const = 0;
};
class SoftRenderer : public Renderer {
public:
    void fill(Rect r, char c) override { cv_.fill(r, c); }
    void setPixel(int x, int y, char c) override { cv_.set(x, y, c); }
    char sample() const override { return cv_.px[6][29]; }
private:
    Canvas cv_;
};
class OtherRenderer : public Renderer {
public:
    void fill(Rect, char) override {}
    void setPixel(int, int, char) override {}
    char sample() const override { return 0; }
};
class ViewB {
public:
    explicit ViewB(std::unique_ptr<Renderer> r) : r_(std::move(r)) {}
    void draw(int t) { layout(t, [this](Rect r, char c) { r_->fill(r, c); }); }
    void drawPixels(int t) {                          // 逐像素的接口：每个像素一次虚函数调用
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) r_->setPixel(x, y, pixelAt(t, x, y));
    }
    char sample() const { return r_->sample(); }
private:
    std::unique_ptr<Renderer> r_;
};

// ---------- 模板：编译期的桥接 ----------
template <class Backend> class ViewT {
public:
    void draw(int t) { layout(t, [this](Rect r, char c) { be_.fill(r, c); }); }
    void drawPixels(int t) {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) be_.set(x, y, pixelAt(t, x, y));
    }
    char sample() const { return be_.px[6][29]; }
private:
    Backend be_;
};

// 告诉编译器 p 指向的内存会被读取，不许把写画布的代码优化掉（GCC 和 Clang 的内联汇编写法）
inline void escape(const void* p) { asm volatile("" : : "g"(p) : "memory"); }

template <class F> double nsPerFrame(int frames, F drawFrame) {
    std::vector<double> runs;
    for (int k = 0; k < 7; ++k) {                     // 跑 7 次取中位数
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) drawFrame(i);
        auto t1 = std::chrono::steady_clock::now();
        runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / frames);
    }
    std::sort(runs.begin(), runs.end());
    return runs[3];
}

int main(int argc, char**) {
    const int frames = 2000000;
    bool other = argc > 5;                            // 永远是 false，但编译器不知道：后端在运行时才确定
    std::unique_ptr<ViewI> vi = other ? std::unique_ptr<ViewI>(new ViewOtherI) : std::unique_ptr<ViewI>(new ViewSoftI);
    ViewB vb(other ? std::unique_ptr<Renderer>(new OtherRenderer) : std::unique_ptr<Renderer>(new SoftRenderer));
    ViewB vp(other ? std::unique_ptr<Renderer>(new OtherRenderer) : std::unique_ptr<Renderer>(new SoftRenderer));
    ViewT<Canvas> vt, vtp;

    double a = nsPerFrame(frames, [&](int t) { vi->draw(t); escape(vi.get()); });
    double b = nsPerFrame(frames, [&](int t) { vb.draw(t); escape(&vb); });
    double c = nsPerFrame(frames, [&](int t) { vt.draw(t); escape(&vt); });
    double d = nsPerFrame(frames, [&](int t) { vp.drawPixels(t); escape(&vp); });
    double e = nsPerFrame(frames, [&](int t) { vtp.drawPixels(t); escape(&vtp); });
    std::printf("粗粒度 fill，每帧 5 次调用\n");
    std::printf("  继承（虚函数）     %6.1f ns/帧\n", a);
    std::printf("  桥接（虚函数）     %6.1f ns/帧\n", b);
    std::printf("  模板（没有虚函数） %6.1f ns/帧\n", c);
    std::printf("逐像素 setPixel，每帧 210 次调用\n");
    std::printf("  桥接（虚函数）     %6.1f ns/帧\n", d);
    std::printf("  模板（没有虚函数） %6.1f ns/帧\n", e);
    std::printf("（校验：%c %c %c %c %c）\n", vi->sample(), vb.sample(), vt.sample(), vp.sample(), vtp.sample());
}
