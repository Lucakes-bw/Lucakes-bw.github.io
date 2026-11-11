// 《策略模式》：练习 2——加一个区域平均（Area）策略：它读多少个源像素取决于缩小倍数，接口里的 taps() 得改
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
    virtual int taps(float rx, float ry) const = 0;   // 每个输出像素最多读几个源像素；rx、ry 是源图和输出的尺寸比
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
    int taps(float, float) const override { return 1; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};

class Bilinear : public Scaler {
public:
    const char* name() const override { return "bilinear"; }
    int taps(float, float) const override { return 4; }
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
    int taps(float, float) const override { return 16; }
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

// 区域平均：缩小时，一个输出像素盖住源图上 rx × ry 的一块，把这块全部平均（放大时退化成最近邻）
class Area : public Scaler {
public:
    const char* name() const override { return "area"; }
    int taps(float rx, float ry) const override {
        return static_cast<int>(std::ceil(std::max(rx, 1.f)) * std::ceil(std::max(ry, 1.f)));
    }
    Plane scale(const Plane& src, int w, int h) const override {
        Plane dst(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int x0 = x * src.w / w, x1 = std::max(x0 + 1, (x + 1) * src.w / w);
                int y0 = y * src.h / h, y1 = std::max(y0 + 1, (y + 1) * src.h / h);
                int sum = 0, n = (x1 - x0) * (y1 - y0);
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx) sum += src.at(sx, sy);
                dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>((sum + n / 2) / n);
            }
        return dst;
    }
};

// 名字 → 策略：命令行、配置文件、界面下拉框都从这里取
std::unique_ptr<Scaler> makeScaler(const std::string& name) {
    static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
        {"nearest", [] { return std::make_unique<Nearest>(); }},
        {"bilinear", [] { return std::make_unique<Bilinear>(); }},
        {"bicubic", [] { return std::make_unique<Bicubic>(); }},
        {"area", [] { return std::make_unique<Area>(); }},
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

    Thumbnailer thumb(3, 2, makeScaler("nearest"));     // 这次是缩小：8×4 → 3×2
    float rx = 8.f / 3, ry = 4.f / 2;
    for (std::string arg : {"nearest", "bilinear", "bicubic", "area"}) {
        thumb.setScaler(makeScaler(arg));        // 运行时换算法
        Plane big = thumb.make(frame);
        std::printf("%-8s taps=%-2d 第一行:", thumb.scaler().name(), thumb.scaler().taps(rx, ry));
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
