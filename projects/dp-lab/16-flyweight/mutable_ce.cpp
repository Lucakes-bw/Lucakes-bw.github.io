// 《享元模式》：修正——工厂返回 const Glyph&，颜色放在每个字的外部状态里；再想改享元就编译不过
#include <cstdint>
#include <unordered_map>

struct Glyph {
    char32_t ch;
    int advance;
};

class GlyphFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, 48}).first;
        return it->second;
    }
private:
    std::unordered_map<char32_t, Glyph> cache_;
};

struct PlacedGlyph {
    const Glyph* glyph;
    float x, y;
    std::uint32_t color;  // 外部状态：每个字自己的颜色
};

int main() {
    GlyphFactory f;
    PlacedGlyph a{&f.get(U'我'), 0, 0, 0xFFFFFF};
    a.color = 0xFFD700;        // 对：只改这一个字
    a.glyph->advance = 60;     // 编译错误：不能通过 const Glyph* 修改享元
}
