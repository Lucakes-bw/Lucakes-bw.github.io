#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

// 生成用来测试的英文文本：词汇表中有 vocab 个用音节拼出来的「单词」，
// 出现的频率服从齐夫定律（第 r 常见的词出现的概率和 1/r 成正比），和真实的英文文本相近。
// 种子固定，每次生成的内容都相同。
inline std::string generateText(std::size_t bytes, std::size_t vocab = 20000, std::uint32_t seed = 42)
{
    static const char* syllables[] = {"ka", "lo", "mi", "ne", "ru", "sa",  "to", "vi", "an", "el",
                                      "or", "is", "un", "th", "er", "ing", "st", "pr", "qu", "ch"};
    std::mt19937 rng(seed);
    std::vector<std::string> words;
    words.reserve(vocab);
    for (std::size_t i = 0; i < vocab; ++i) {
        std::string w;
        std::size_t x = i;
        do {
            w += syllables[x % 20];
            x /= 20;
        } while (x > 0);
        words.push_back(w);
    }
    std::vector<double> weights(vocab);
    for (std::size_t r = 0; r < vocab; ++r) weights[r] = 1.0 / static_cast<double>(r + 1);
    std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
    std::uniform_int_distribution<int> percent(0, 99);

    std::string text;
    text.reserve(bytes + 32);
    bool sentenceStart = true;
    while (text.size() < bytes) {
        std::string w = words[pick(rng)];
        if (sentenceStart) w[0] = static_cast<char>(w[0] - 'a' + 'A');
        text += w;
        int p = percent(rng);
        sentenceStart = p < 6;
        if (p < 6)
            text += ". ";
        else if (p < 12)
            text += ", ";
        else if (p < 14)
            text += "\n";
        else
            text += ' ';
    }
    return text;
}
