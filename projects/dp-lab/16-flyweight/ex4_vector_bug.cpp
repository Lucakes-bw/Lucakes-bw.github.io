// 《享元模式》练习 4：找问题——工厂把享元存在 vector 里，返回元素的引用；字多了以后先拿到的引用全部悬空
#include <cstdio>
#include <unordered_map>
#include <vector>

struct Glyph {
    char32_t ch;
    int advance;
};

class GlyphFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = index_.find(ch);
        if (it != index_.end()) return glyphs_[it->second];
        index_[ch] = glyphs_.size();
        glyphs_.push_back(Glyph{ch, 48});  // 容量不够时，vector 会搬到一块新内存
        return glyphs_.back();
    }
    std::size_t size() const { return glyphs_.size(); }

private:
    std::vector<Glyph> glyphs_;                     // 享元连续存放，看起来很省
    std::unordered_map<char32_t, std::size_t> index_;  // 字 → 下标
};

int main() {
    GlyphFactory f;
    const Glyph* first = &f.get(U'一');
    std::printf("第 1 个字形在 %p\n", static_cast<const void*>(first));
    for (char32_t ch = 0x4E01; ch < 0x4E01 + 100; ++ch) f.get(ch);  // 后面又来了 100 个新字
    std::printf("现在第 1 个字形在 %p\n", static_cast<const void*>(&f.get(U'一')));
    std::printf("first->ch = U+%04X，advance = %d\n", unsigned(first->ch), first->advance);  // 读的是已经释放的内存
}
