// 《享元模式》：工厂返回 shared_ptr<const Glyph>、只用 weak_ptr 记住享元——没人用的字形自动释放，代价是重新光栅化
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
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
    static inline int alive = 0, peak = 0;
    Glyph(char32_t c, std::vector<std::uint8_t> b) : ch(c), bitmap(std::move(b)) { peak = std::max(peak, ++alive); }
    ~Glyph() { --alive; }
    Glyph(const Glyph&) = delete;
    Glyph& operator=(const Glyph&) = delete;
};
const int px = 48;

// 强引用：工厂一直持有所有享元，直到工厂销毁
class StrongFactory {
public:
    std::shared_ptr<const Glyph> get(char32_t ch) {
        auto& slot = cache_[ch];
        if (!slot) slot = std::make_shared<const Glyph>(ch, rasterize(ch, px));
        return slot;
    }
    std::size_t entries() const { return cache_.size(); }
private:
    std::unordered_map<char32_t, std::shared_ptr<const Glyph>> cache_;
};

// 弱引用：工厂只记 weak_ptr，最后一个使用者放手时享元就释放了
class WeakFactory {
public:
    std::shared_ptr<const Glyph> get(char32_t ch) {
        auto& slot = cache_[ch];
        if (auto sp = slot.lock()) return sp;  // 还有人在用：直接共享
        auto sp = std::make_shared<const Glyph>(ch, rasterize(ch, px));
        slot = sp;
        return sp;
    }
    std::size_t entries() const { return cache_.size(); }
    std::size_t expired() const {
        return std::count_if(cache_.begin(), cache_.end(), [](auto& kv) { return kv.second.expired(); });
    }
private:
    std::unordered_map<char32_t, std::weak_ptr<const Glyph>> cache_;
};

// 播放整条字幕：屏幕上同时显示当前行和上一行（淡出中），每行持有自己那些字的 shared_ptr
template <class Factory>
void play(const std::vector<std::u32string>& track, Factory& f) {
    std::vector<std::shared_ptr<const Glyph>> shown[2];
    for (std::size_t i = 0; i < track.size(); ++i) {
        auto& cur = shown[i % 2];
        cur.clear();  // 两行以前的那一行离开屏幕，放开它的字形
        for (char32_t ch : track[i]) cur.push_back(f.get(ch));
    }
}

int main() {
    auto track = make_track(1400);
    std::printf("sizeof(const Glyph*) = %zu，sizeof(shared_ptr<const Glyph>) = %zu\n",
                sizeof(const Glyph*), sizeof(std::shared_ptr<const Glyph>));
    {
        StrongFactory f;
        play(track, f);
        std::printf("强引用缓存：光栅化 %d 次，同时存活的字形最多 %d 个，播完还有 %d 个\n",
                    g_raster, Glyph::peak, Glyph::alive);
    }
    g_raster = 0;
    Glyph::peak = 0;
    {
        WeakFactory f;
        play(track, f);
        std::printf("weak_ptr 缓存：光栅化 %d 次，同时存活的字形最多 %d 个，播完还有 %d 个\n",
                    g_raster, Glyph::peak, Glyph::alive);
        std::printf("               map 里有 %zu 个条目，其中 %zu 个已经过期（控制块还没释放）\n",
                    f.entries(), f.expired());
    }
}
