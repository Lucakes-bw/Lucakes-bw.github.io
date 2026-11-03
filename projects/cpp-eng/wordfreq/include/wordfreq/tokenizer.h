#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wordfreq {

// 判断一个字节能不能出现在单词中：ASCII 字母、数字和撇号（don't、it's）。
// 写在头文件中并且是 inline 的：forEachWord 对每个字节都要调用它，必须能被内联
inline bool isWordChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
}

// 把文本切成单词，并转成小写。不能出现在单词中的字节（空白、标点、非 ASCII 字节）都是分隔符。
// 只由撇号组成的「单词」会被丢掉，单词开头和结尾的撇号也会去掉：'hello' → hello。
std::vector<std::string> tokenize(std::string_view text);

// 和 tokenize 相同的规则，但不创建 vector：每找到一个单词就调用一次 f(std::string_view)。
// 传给 f 的 string_view 指向 buffer，下一次调用时内容会被覆盖。
template <class F>
void forEachWord(std::string_view text, std::string& buffer, F&& f)
{
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        while (i < n && !isWordChar(text[i])) ++i;
        std::size_t begin = i;
        while (i < n && isWordChar(text[i])) ++i;
        std::size_t end = i;
        while (begin < end && text[begin] == '\'') ++begin;  // 去掉两端的撇号
        while (end > begin && text[end - 1] == '\'') --end;
        if (begin == end) continue;
        buffer.assign(text.data() + begin, end - begin);
        for (char& c : buffer) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        f(std::string_view(buffer));
    }
}

}  // namespace wordfreq
