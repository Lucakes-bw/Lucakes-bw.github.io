// 《享元模式》练习 4 的修正：享元改存在 deque 里——push_back 只在两端加块，已有元素不搬家，引用一直有效
#include <cstdio>
#include <deque>
#include <unordered_map>

struct Glyph {
    char32_t ch;
    int advance;
};

class GlyphFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = index_.find(ch);
        if (it != index_.end()) return *it->second;
        glyphs_.push_back(Glyph{ch, 48});  // deque::push_back 不会让已有元素的引用失效
        index_[ch] = &glyphs_.back();
        return glyphs_.back();
    }

private:
    std::deque<Glyph> glyphs_;
    std::unordered_map<char32_t, const Glyph*> index_;
};

int main() {
    GlyphFactory f;
    const Glyph* first = &f.get(U'一');
    for (char32_t ch = 0x4E01; ch < 0x4E01 + 100; ++ch) f.get(ch);
    std::printf("第 1 个字形还在原地：%s\n", first == &f.get(U'一') ? "是" : "否");
    std::printf("first->ch = U+%04X，advance = %d\n", unsigned(first->ch), first->advance);
}
