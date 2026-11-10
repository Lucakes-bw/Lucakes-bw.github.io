// 《享元模式》练习 3：ASS 字幕的样式——1400 行台词只引用几种样式；行内覆盖（\b1、\c）生成的新样式也驻留起来
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

struct Style {  // 内部状态：同一种样式的所有台词都一样
    std::string font;
    int size;
    std::uint32_t primary, outline;
    float outline_w;
    bool bold, italic;
    int align;
    bool operator==(const Style& o) const {
        return font == o.font && size == o.size && primary == o.primary && outline == o.outline &&
               outline_w == o.outline_w && bold == o.bold && italic == o.italic && align == o.align;
    }
};
struct StyleHash {
    std::size_t operator()(const Style& s) const {
        std::size_t h = std::hash<std::string>{}(s.font);
        for (std::size_t v : {std::size_t(s.size), std::size_t(s.primary), std::size_t(s.outline),
                              std::size_t(s.outline_w * 100), std::size_t(s.bold), std::size_t(s.italic),
                              std::size_t(s.align)})
            h = h * 1000003 ^ v;
        return h;
    }
};

// 样式池：按「值」驻留，内容相同的样式只存一份
class StylePool {
public:
    const Style* intern(const Style& s) { return &*pool_.insert(s).first; }  // unordered_set 的元素不会搬家
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_set<Style, StyleHash> pool_;
};

struct Dialogue {  // 外部状态：每行台词自己的时间和文字
    int start_ms, end_ms;
    std::string text;
    const Style* style;
};

int main() {
    StylePool pool;
    const Style* def = pool.intern({"Noto Sans CJK SC", 48, 0xFFFFFF, 0x000000, 2.5f, false, false, 2});
    const Style* top = pool.intern({"Noto Sans CJK SC", 40, 0xFFFFFF, 0x000000, 2.0f, false, false, 8});
    const Style* sign = pool.intern({"Source Han Serif SC", 56, 0x00FFFF, 0x202020, 3.0f, true, false, 5});

    std::vector<Dialogue> lines;
    std::uint32_t s = 11;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (s >> 8) % 100; };
    for (int i = 0; i < 1400; ++i) {
        const Style* base = i % 50 == 0 ? sign : (i % 7 == 0 ? top : def);
        int r = rnd();
        if (r < 5) {                         // 5% 的台词有 {\b1}：在基础样式上加粗
            Style st = *base; st.bold = true; base = pool.intern(st);
        } else if (r < 10) {                 // 5% 有 {\c&H00FFFF&}：改成黄色
            Style st = *base; st.primary = 0x00FFFF; base = pool.intern(st);
        }
        lines.push_back({i * 3500, i * 3500 + 3000, "……", base});
    }
    std::printf("%zu 行台词，样式池里 %zu 种样式；每行只存一个 %zu 字节的指针，不用各存一份 %zu 字节的 Style\n",
                lines.size(), pool.size(), sizeof(const Style*), sizeof(Style));
}
