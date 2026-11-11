// 《策略模式》：策略版加一种 Lanczos 算法——新增一个类，再在表里登记一行（和 strategy_scaler.cpp 对比）
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Plane {                                   // 一个 8 位灰度平面（视频帧的 Y 分量）
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {                 // 越界时取边上的像素
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

// 策略接口：一种缩放算法
class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual int taps() const = 0;                // 每个输出像素读几个源像素
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};

// 各个算法共用的外层循环：把输出坐标映射回源图，交给 kernel 取值
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

class Nearest : public Scaler {
public:
    const char* name() const override { return "nearest"; }
    int taps() const override { return 1; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};

class Bilinear : public Scaler {
public:
    const char* name() const override { return "bilinear"; }
    int taps() const override { return 4; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
            float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
            return top * (1 - fy) + bot * fy;
        });
    }
};

class Bicubic : public Scaler {
public:
    const char* name() const override { return "bicubic"; }
    int taps() const override { return 16; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
            for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
            return sum;
        });
    }

private:
    static float cubic(float t) {                // Keys 三次卷积核，a = -0.5
        t = std::fabs(t);
        if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
        if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
        return 0;
    }
};

class Lanczos : public Scaler {
public:
    const char* name() const override { return "lanczos"; }
    int taps() const override { return 36; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[6], wy[6], sx = 0, sy = 0, sum = 0;
            for (int i = 0; i < 6; ++i) {
                wx[i] = lanczos3(fx - (i - 2)), wy[i] = lanczos3(fy - (i - 2));
                sx += wx[i], sy += wy[i];
            }
            for (int j = 0; j < 6; ++j)
                for (int i = 0; i < 6; ++i) sum += p.at(x0 + i - 2, y0 + j - 2) * wx[i] * wy[j];
            return sum / (sx * sy);                      // 权重之和不是 1，要归一化
        });
    }

private:
    static float lanczos3(float t) {             // sinc(t) · sinc(t / 3)，|t| < 3
        t = std::fabs(t);
        if (t < 1e-6f) return 1;
        if (t >= 3) return 0;
        const float pi = 3.14159265f;
        return 3 * std::sin(pi * t) * std::sin(pi * t / 3) / (pi * pi * t * t);
    }
};

// 名字 → 策略：命令行、配置文件、界面下拉框都从这里取
std::unique_ptr<Scaler> makeScaler(const std::string& name) {
    static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
        {"nearest", [] { return std::make_unique<Nearest>(); }},
        {"bilinear", [] { return std::make_unique<Bilinear>(); }},
        {"bicubic", [] { return std::make_unique<Bicubic>(); }},
        {"lanczos", [] { return std::make_unique<Lanczos>(); }},
    };
    auto it = table.find(name);
    return it != table.end() ? it->second() : std::make_unique<Bilinear>();
}

// 上下文：生成缩略图。它不知道有哪些算法，只知道 Scaler 接口
class Thumbnailer {
public:
    Thumbnailer(int w, int h, std::unique_ptr<Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
    void setScaler(std::unique_ptr<Scaler> s) { scaler_ = std::move(s); }
    const Scaler& scaler() const { return *scaler_; }
    Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }

private:
    int w_, h_;
    std::unique_ptr<Scaler> scaler_;
};

int main() {
    Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    Thumbnailer thumb(12, 6, makeScaler("nearest"));
    for (std::string arg : {"nearest", "bilinear", "bicubic", "lanczos"}) {
        thumb.setScaler(makeScaler(arg));        // 运行时换算法
        Plane big = thumb.make(frame);
        std::printf("%-8s taps=%-2d 第一行:", thumb.scaler().name(), thumb.scaler().taps());
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
