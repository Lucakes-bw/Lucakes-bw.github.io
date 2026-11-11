// 《策略模式》：问题版——用 enum + switch 选缩放算法，和算法有关的代码散落在 4 个函数里
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

enum class Algo { Nearest, Bilinear, Bicubic };

// ① 命令行参数 → 算法
Algo parseAlgo(const std::string& s) {
    if (s == "nearest") return Algo::Nearest;
    if (s == "bilinear") return Algo::Bilinear;
    if (s == "bicubic") return Algo::Bicubic;
    return Algo::Bilinear;
}

// ② 算法 → 名字（日志、界面上的下拉框）
const char* algoName(Algo a) {
    switch (a) {
    case Algo::Nearest: return "nearest";
    case Algo::Bilinear: return "bilinear";
    case Algo::Bicubic: return "bicubic";
    }
    return "?";
}

// ③ 每个输出像素要读几个源像素（估算开销用）
int algoTaps(Algo a) {
    switch (a) {
    case Algo::Nearest: return 1;
    case Algo::Bilinear: return 4;
    case Algo::Bicubic: return 16;
    }
    return 0;
}

float cubic(float t) {                           // Keys 三次卷积核，a = -0.5
    t = std::fabs(t);
    if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
    if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
    return 0;
}

// ④ 在源图的 (x, y) 处取一个值：每个算法的核心都挤在这一个 switch 里
float sample(const Plane& p, float x, float y, Algo a) {
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    float fx = x - x0, fy = y - y0;
    switch (a) {
    case Algo::Nearest:
        return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
    case Algo::Bilinear: {
        float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
        float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
        return top * (1 - fy) + bot * fy;
    }
    case Algo::Bicubic: {
        float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
        for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
        return sum;
    }
    }
    return 0;
}

Plane scale(const Plane& src, int w, int h, Algo a) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = sample(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f, a);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

int main() {
    Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    for (std::string arg : {"nearest", "bilinear", "bicubic"}) {
        Algo a = parseAlgo(arg);
        Plane big = scale(frame, 12, 6, a);      // 放大 1.5 倍
        std::printf("%-8s taps=%-2d 第一行:", algoName(a), algoTaps(a));
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
