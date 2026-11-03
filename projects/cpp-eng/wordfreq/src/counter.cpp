#include "wordfreq/counter.h"

#include <algorithm>
#include <thread>

#include "wordfreq/tokenizer.h"

namespace wordfreq {

void Counter::add(std::string_view text)
{
    // 回调收到的单词就在 buffer_ 中，直接用 buffer_ 查找，不用再构造一个 std::string
    forEachWord(text, buffer_, [this](std::string_view) { bump(buffer_, 1); });
}

void Counter::addWord(std::string_view word, std::int64_t times)
{
    bump(std::string(word), times);
}

void Counter::bump(const std::string& word, std::int64_t times)
{
    auto it = counts_.find(word);
    if (it == counts_.end()) it = counts_.emplace(word, 0).first;
    it->second += times;
    total_ += times;
}

void Counter::merge(const Counter& other)
{
    for (const auto& [word, n] : other.counts_) addWord(word, n);
}

std::int64_t Counter::count(std::string_view word) const
{
    auto it = counts_.find(std::string(word));
    return it == counts_.end() ? 0 : it->second;
}

std::vector<Entry> Counter::top(std::size_t k) const
{
    std::vector<Entry> all;
    all.reserve(counts_.size());
    for (const auto& [word, n] : counts_) all.push_back({word, n});
    k = std::min(k, all.size());
    auto before = [](const Entry& a, const Entry& b) {
        return a.count != b.count ? a.count > b.count : a.word < b.word;
    };
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(k), all.end(), before);
    all.resize(k);
    return all;
}

Counter countParallel(std::string_view text, unsigned threads)
{
    if (threads == 0) threads = 1;
    // 切成 threads 段，每段的结尾向后移到单词边界，保证一个单词不会被切开
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    for (unsigned t = 0; t < threads && begin < text.size(); ++t) {
        std::size_t end = t + 1 == threads ? text.size() : std::max(begin, text.size() * (t + 1) / threads);
        while (end < text.size() && isWordChar(text[end])) ++end;
        parts.push_back(text.substr(begin, end - begin));
        begin = end;
    }

    std::vector<Counter> partial(parts.size());
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        workers.emplace_back([&partial, &parts, i] { partial[i].add(parts[i]); });
    }
    for (auto& w : workers) w.join();

    Counter result;
    for (const auto& c : partial) result.merge(c);
    return result;
}

}  // namespace wordfreq
