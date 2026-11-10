// 《享元模式》：实测排版（创建）、整轨绘制、工厂查找的开销：不共享 vs 享元，-O2，7 次取中位数
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

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
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct FatGlyph {
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

// 享元：只保存内部状态，创建以后不再修改
struct Glyph {
    int font;  // 字体编号（字体名只存一份，见 fonts）
    int px;
    char32_t ch;
    int advance;
    std::vector<std::uint8_t> bitmap;
};

struct GlyphKey {
    int font, px;
    char32_t ch;
    bool operator==(const GlyphKey& o) const { return font == o.font && px == o.px && ch == o.ch; }
};
struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& k) const {
        return (std::size_t(k.font) * 131 + k.px) * 1000003 ^ k.ch;
    }
};

// 享元工厂：同一个 (字体, 字号, 字) 只光栅化一次，以后都返回同一个 Glyph
class GlyphFactory {
public:
    const Glyph& get(int font, int px, char32_t ch) {
        GlyphKey key{font, px, ch};
        auto it = cache_.find(key);
        if (it == cache_.end())
            it = cache_.emplace(key, Glyph{font, px, ch, px, rasterize(ch, px)}).first;
        return it->second;  // unordered_map 的元素在节点里，rehash 也不会移动它
    }
    std::size_t size() const { return cache_.size(); }

private:
    std::unordered_map<GlyphKey, Glyph, GlyphKeyHash> cache_;
};

// 外部状态：每个字只剩一个指针和位置；颜色和显示时间是整行共用的，放到行上
struct PlacedGlyph {
    const Glyph* glyph;
    float x, y;
};
struct Line {
    int start_ms, end_ms;
    std::uint32_t color;
    std::vector<PlacedGlyph> glyphs;
};


using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
const int px = 48;
volatile long long sink;

std::vector<std::vector<FatGlyph>> layout_naive(const std::vector<std::u32string>& track) {
    std::vector<std::vector<FatGlyph>> lines;
    lines.reserve(track.size());
    for (std::size_t i = 0; i < track.size(); ++i) {
        std::vector<FatGlyph> line;
        line.reserve(track[i].size());
        float x = 960 - px * track[i].size() / 2.0f;
        for (char32_t ch : track[i]) {
            line.push_back(FatGlyph{"Noto Sans CJK SC", px, ch, px, rasterize(ch, px),
                                    x, 1000, 0xFFFFFFFF, int(i) * 3500, int(i) * 3500 + 3000});
            x += px;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<Line> layout_fly(const std::vector<std::u32string>& track, GlyphFactory& factory) {
    std::vector<Line> lines;
    lines.reserve(track.size());
    for (std::size_t i = 0; i < track.size(); ++i) {
        Line line{int(i) * 3500, int(i) * 3500 + 3000, 0xFFFFFFFF, {}};
        line.glyphs.reserve(track[i].size());
        float x = 960 - px * track[i].size() / 2.0f;
        for (char32_t ch : track[i]) {
            line.glyphs.push_back({&factory.get(0, px, ch), x, 1000});
            x += px;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

// 整轨绘制：把每个字的整张位图混合一遍（读遍每个字的全部像素）
long long draw(const std::vector<std::vector<FatGlyph>>& lines) {
    long long s = 0;
    for (auto& line : lines)
        for (auto& g : line)
            for (std::uint8_t b : g.bitmap) s += b;
    return s;
}
long long draw(const std::vector<Line>& lines) {
    long long s = 0;
    for (auto& line : lines)
        for (auto& g : line.glyphs)
            for (std::uint8_t b : g.glyph->bitmap) s += b;
    return s;
}

int main() {
    auto track = make_track(1400);
    std::vector<char32_t> all;
    for (auto& t : track) all.insert(all.end(), t.begin(), t.end());

    std::vector<double> lay_n, lay_f, draw_n, draw_f, hit, raster;
    for (int round = 0; round < 7; ++round) {
        auto t0 = Clock::now();
        auto a = layout_naive(track);
        auto t1 = Clock::now();
        GlyphFactory f;
        auto b = layout_fly(track, f);
        auto t2 = Clock::now();
        long long s1 = draw(a);
        auto t3 = Clock::now();
        long long s2 = draw(b);
        auto t4 = Clock::now();
        if (s1 != s2) std::printf("绘制结果不一致！\n");
        sink = s1;
        // 工厂已经全部命中以后，再查一遍整轨的字：每次 get 的开销
        auto t5 = Clock::now();
        long long s = 0;
        for (int rep = 0; rep < 10; ++rep)
            for (char32_t ch : all) s += f.get(0, px, ch).advance;
        auto t6 = Clock::now();
        sink = s;
        // 光栅化一个字的开销
        auto t7 = Clock::now();
        for (int k = 0; k < 2000; ++k) s += rasterize(0x4E00 + k, px)[0];
        auto t8 = Clock::now();
        sink = s;
        lay_n.push_back(ms(t0, t1)); lay_f.push_back(ms(t1, t2));
        draw_n.push_back(ms(t2, t3)); draw_f.push_back(ms(t3, t4));
        hit.push_back(ms(t5, t6) * 1e6 / (10.0 * all.size()));
        raster.push_back(ms(t7, t8) * 1e6 / 2000);
    }
    std::printf("排版整条字幕（%zu 个字）：不共享 %.1f ms，享元 %.1f ms\n", all.size(), median(lay_n), median(lay_f));
    std::printf("整轨绘制（读每个字的全部像素）：不共享 %.1f ms，享元 %.1f ms\n", median(draw_n), median(draw_f));
    std::printf("工厂命中一次 get：%.1f ns；光栅化一个字：%.0f ns\n", median(hit), median(raster));
}
