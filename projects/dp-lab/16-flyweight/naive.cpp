// 《享元模式》：不共享——整条字幕轨排版后，每个字符都是一个完整的 Glyph 对象，各自带一份字形位图
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

// ---- 统计堆分配：替换全局 operator new / delete，在每块内存前面记下大小 ----
static std::size_t g_allocs = 0, g_live = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    g_live += n;
    auto* h = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!h) std::abort();
    *h = n;
    return reinterpret_cast<char*>(h) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    // 内部状态：只取决于「哪个字体、多大、哪个字」
    std::string font;
    int px;
    char32_t ch;
    int advance;
    std::vector<std::uint8_t> bitmap;
    // 外部状态：每个字符在屏幕上的位置、颜色、显示时间
    float x, y;
    std::uint32_t color;
    int start_ms, end_ms;
};

int main() {
    auto track = make_track(1400);
    std::size_t chars = 0;
    std::unordered_set<char32_t> distinct;
    for (auto& t : track) { chars += t.size(); distinct.insert(t.begin(), t.end()); }

    std::size_t a0 = g_allocs, m0 = g_live;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::vector<Glyph>> lines;  // 排版结果：每行一组 Glyph
    lines.reserve(track.size());
    const int px = 48;
    for (std::size_t i = 0; i < track.size(); ++i) {
        std::vector<Glyph> line;
        line.reserve(track[i].size());
        float x = 960 - px * track[i].size() / 2.0f;
        for (char32_t ch : track[i]) {
            line.push_back(Glyph{"Noto Sans CJK SC", px, ch, px, rasterize(ch, px),
                                 x, 1000, 0xFFFFFFFF, int(i) * 3500, int(i) * 3500 + 3000});
            x += px;
        }
        lines.push_back(std::move(line));
    }
    auto t1 = std::chrono::steady_clock::now();

    long long ck = 0;  // 「画」一遍：每个字取位图中间的一个像素，加上位置
    for (auto& line : lines)
        for (auto& g : line) ck += g.bitmap[px * px / 2] + int(g.x);

    std::printf("字幕：%zu 行，%zu 个字，%zu 种不同的字\n", track.size(), chars, distinct.size());
    std::printf("Glyph 对象：%zu 个，sizeof(Glyph) = %zu 字节\n", chars, sizeof(Glyph));
    std::printf("光栅化：%d 次\n", g_raster);
    std::printf("堆分配：%zu 次，占用 %.1f MB\n", g_allocs - a0, (g_live - m0) / 1048576.0);
    std::printf("排版用时：%.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
    std::printf("校验和：%lld\n", ck);
}
