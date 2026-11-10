// 《享元模式》练习 2：给字形缓存加容量上限，满了按 LRU（最久没用的先扔）淘汰；比较不同容量要光栅化多少次
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <list>
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
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    char32_t ch;
    std::vector<std::uint8_t> bitmap;
};
const int px = 48;

class LruGlyphCache {
public:
    explicit LruGlyphCache(std::size_t capacity) : cap_(capacity) {}
    // 返回的引用只保证用到下一次 get 之前：再 get 可能把它淘汰掉
    const Glyph& get(char32_t ch) {
        auto it = index_.find(ch);
        if (it != index_.end()) {
            order_.splice(order_.begin(), order_, it->second);  // 移到最前面（最近用过），不复制
            return *it->second;
        }
        if (order_.size() == cap_) {  // 满了：扔掉最后面那个（最久没用的）
            index_.erase(order_.back().ch);
            order_.pop_back();
        }
        order_.push_front(Glyph{ch, rasterize(ch, px)});
        index_[ch] = order_.begin();
        return order_.front();
    }

private:
    std::size_t cap_;
    std::list<Glyph> order_;  // 前面是最近用过的
    std::unordered_map<char32_t, std::list<Glyph>::iterator> index_;
};

int main() {
    auto track = make_track(1400);
    const int kFrames = 75;  // 每行字幕显示 3 秒，25 fps，每帧都要画一遍
    std::size_t gets = 0;
    for (auto& t : track) gets += t.size() * kFrames;
    std::printf("不缓存：光栅化 %zu 次\n", gets);
    for (std::size_t cap : {16, 64, 256, 1024, 4096}) {
        g_raster = 0;
        LruGlyphCache cache(cap);
        long long sum = 0;
        for (auto& t : track)
            for (int frame = 0; frame < kFrames; ++frame)
                for (char32_t ch : t) sum += cache.get(ch).bitmap[0];  // 拿到就画，不保存引用
        std::printf("容量 %4zu：光栅化 %6d 次（校验和 %lld）\n", cap, g_raster, sum);
    }
}
