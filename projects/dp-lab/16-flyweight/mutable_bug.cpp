// 《享元模式》：把外部状态（颜色）放进了享元——卡拉 OK 高亮一个字，所有同样的字都变了色
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

struct Glyph {
    char32_t ch;
    std::string utf8;       // 为了打印方便，存一份 UTF-8
    std::uint32_t color;    // 错：颜色每个字可能不同，是外部状态
};

class GlyphFactory {
public:
    Glyph& get(char32_t ch, const char* utf8) {  // 错：返回了非 const 引用，谁都能改
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, utf8, 0xFFFFFF}).first;
        return it->second;
    }
private:
    std::unordered_map<char32_t, Glyph> cache_;
};

void print(const char* label, const std::vector<Glyph*>& line) {
    std::printf("%s", label);
    for (auto* g : line) std::printf(" %s%s", g->utf8.c_str(), g->color == 0xFFFFFF ? "" : "(黄)");
    std::printf("\n");
}

int main() {
    GlyphFactory f;
    std::vector<Glyph*> line1{&f.get(U'我', "我"), &f.get(U'们', "们"), &f.get(U'走', "走"), &f.get(U'吧', "吧")};
    std::vector<Glyph*> line2{&f.get(U'我', "我"), &f.get(U'不', "不"), &f.get(U'走', "走")};

    // 卡拉 OK：第 1 行唱到第 1 个字，把它染成黄色
    line1[0]->color = 0xFFD700;
    print("第 1 行：", line1);
    print("第 2 行：", line2);  // 第 2 行还没开始唱，它的「我」也黄了
}
