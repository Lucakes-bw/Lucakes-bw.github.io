// 《策略模式》：练习 4——找问题：两个缩略图生成器共用一个「带缓存」的策略对象，第二个出来的图不对
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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

class Scaler {
public:
    virtual ~Scaler() = default;
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};

// 「优化版」双线性：每一列对应的源坐标和权重只算一次，存起来以后查表
class CachedBilinear : public Scaler {
public:
    Plane scale(const Plane& src, int w, int h) const override {
        if (static_cast<int>(col0_.size()) != w) {               // 输出宽度变了才重新算
            col0_.assign(static_cast<size_t>(w), 0);
            colf_.assign(static_cast<size_t>(w), 0);
            for (int x = 0; x < w; ++x) {
                float sx = (x + 0.5f) * src.w / w - 0.5f;
                col0_[static_cast<size_t>(x)] = static_cast<int>(std::floor(sx));
                colf_[static_cast<size_t>(x)] = sx - std::floor(sx);
            }
        }
        Plane dst(w, h);
        for (int y = 0; y < h; ++y) {
            float sy = (y + 0.5f) * src.h / h - 0.5f;
            int y0 = static_cast<int>(std::floor(sy));
            float fy = sy - y0;
            for (int x = 0; x < w; ++x) {
                int x0 = col0_[static_cast<size_t>(x)];
                float fx = colf_[static_cast<size_t>(x)];
                float top = src.at(x0, y0) * (1 - fx) + src.at(x0 + 1, y0) * fx;
                float bot = src.at(x0, y0 + 1) * (1 - fx) + src.at(x0 + 1, y0 + 1) * fx;
                dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(top * (1 - fy) + bot * fy + 0.5f);
            }
        }
        return dst;
    }

private:
    mutable std::vector<int> col0_;
    mutable std::vector<float> colf_;
};

class Thumbnailer {
public:
    Thumbnailer(int w, int h, std::shared_ptr<const Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
    Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }
private:
    int w_, h_;
    std::shared_ptr<const Scaler> scaler_;
};

Plane ramp(int w, int h) {                       // 从左到右 0 → 220 的渐变
    Plane p(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) p.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(220 * x / (w - 1));
    return p;
}
void show(const char* tag, const Plane& p) {
    std::printf("%s", tag);
    for (int x = 0; x < p.w; ++x) std::printf(" %3d", p.at(x, 0));
    std::printf("\n");
}

int main() {
    auto bilinear = std::make_shared<CachedBilinear>();          // 「省内存」：只建一个，大家共用
    Thumbnailer forSd(4, 2, bilinear), forHd(4, 2, bilinear);    // 都生成 4×2 的缩略图
    show("标清视频 8×4  →", forSd.make(ramp(8, 4)));
    show("高清视频 16×8 →", forHd.make(ramp(16, 8)));
    show("高清（新对象）→", CachedBilinear{}.scale(ramp(16, 8), 4, 2));
}
