// 《策略模式》：用 bbb-10s.mp4 的一帧真实画面，比较四种缩放策略的耗时和画质（PSNR），请用 -O2
//   ffmpeg -ss 5 -i bbb-10s.mp4 -frames:v 1 -pix_fmt gray frame.pgm && ./quality frame.pgm [输出目录]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
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
    virtual const char* name() const = 0;
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
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

float cubic(float t) {
    t = std::fabs(t);
    if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
    if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
    return 0;
}
float lanczos3(float t) {
    t = std::fabs(t);
    if (t < 1e-6f) return 1;
    if (t >= 3) return 0;
    const float pi = 3.14159265f;
    return 3 * std::sin(pi * t) * std::sin(pi * t / 3) / (pi * pi * t * t);
}

struct Nearest : Scaler {
    const char* name() const override { return "nearest"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};
struct Bilinear : Scaler {
    const char* name() const override { return "bilinear"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
            float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
            return top * (1 - fy) + bot * fy;
        });
    }
};
struct Bicubic : Scaler {
    const char* name() const override { return "bicubic"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
            for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
            return sum;
        });
    }
};
struct Lanczos : Scaler {
    const char* name() const override { return "lanczos"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
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
};

Plane readPgm(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic; int w = 0, h = 0, maxv = 0;
    if (!(in >> magic >> w >> h >> maxv) || magic != "P5") return Plane(0, 0);
    in.get();
    Plane p(w, h);
    in.read(reinterpret_cast<char*>(p.px.data()), static_cast<std::streamsize>(p.px.size()));
    return p;
}
void writePgm(const std::string& path, const Plane& p) {
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << p.w << ' ' << p.h << "\n255\n";
    out.write(reinterpret_cast<const char*>(p.px.data()), static_cast<std::streamsize>(p.px.size()));
}

Plane halve(const Plane& s) {                    // 2×2 求平均，缩小到一半
    Plane d(s.w / 2, s.h / 2);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x)
            d.px[static_cast<size_t>(y) * d.w + x] = static_cast<uint8_t>(
                (s.at(2 * x, 2 * y) + s.at(2 * x + 1, 2 * y) + s.at(2 * x, 2 * y + 1) + s.at(2 * x + 1, 2 * y + 1) + 2) / 4);
    return d;
}
double psnr(const Plane& a, const Plane& b) {
    double se = 0;
    for (size_t i = 0; i < a.px.size(); ++i) { double d = a.px[i] - b.px[i]; se += d * d; }
    return 10 * std::log10(255.0 * 255.0 / (se / static_cast<double>(a.px.size())));
}
Plane crop(const Plane& s, int x0, int y0, int w, int h) {
    Plane d(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) d.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(s.at(x0 + x, y0 + y));
    return d;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("用法：quality frame.pgm [输出目录]\n"); return 1; }
    Plane frame = readPgm(argv[1]);
    if (frame.w < 16 || frame.h < 16) { std::printf("读不到 %s\n", argv[1]); return 1; }
    Plane small = halve(frame);
    std::unique_ptr<Scaler> all[] = {std::make_unique<Nearest>(), std::make_unique<Bilinear>(),
                                     std::make_unique<Bicubic>(), std::make_unique<Lanczos>()};
    for (auto& s : all) s->scale(frame, 1920, 1080);              // 热身

    std::printf("%-9s %14s %20s\n", "策略", "720p→1080p ms", "缩小一半再放大 PSNR dB");
    for (auto& s : all) {
        std::vector<double> t;
        for (int i = 0; i < 7; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            Plane out = s->scale(frame, 1920, 1080);
            t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(t.begin(), t.end());
        Plane back = s->scale(small, frame.w, frame.h);
        std::printf("%-9s %14.2f %20.2f\n", s->name(), t[3], psnr(frame, back));
        if (argc > 2) {                          // 把眼睛附近 40×24 的一块放大 6 倍存下来，配图用
            Plane piece = crop(small, 268, 36, 40, 24);
            writePgm(std::string(argv[2]) + "/" + s->name() + ".pgm", s->scale(piece, 240, 144));
        }
    }
}
