#include "wordfreq/tokenizer.h"

namespace wordfreq {

std::vector<std::string> tokenize(std::string_view text)
{
    std::vector<std::string> words;
    std::string buffer;
    forEachWord(text, buffer, [&](std::string_view w) { words.emplace_back(w); });
    return words;
}

}  // namespace wordfreq
