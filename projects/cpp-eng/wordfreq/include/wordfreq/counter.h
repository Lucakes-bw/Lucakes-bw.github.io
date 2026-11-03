#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wordfreq {

struct Entry {
    std::string word;
    std::int64_t count = 0;

    friend bool operator==(const Entry& a, const Entry& b) { return a.word == b.word && a.count == b.count; }
};

// 统计单词出现的次数
class Counter {
public:
    void add(std::string_view text);                              // 切分 text，统计其中的每个单词
    void addWord(std::string_view word, std::int64_t times = 1);  // 直接加一个已经切分好的单词
    void merge(const Counter& other);                             // 把另一个 Counter 的结果加进来

    std::int64_t total() const { return total_; }          // 单词的总数（重复的也算）
    std::size_t unique() const { return counts_.size(); }  // 不同的单词有几个
    std::int64_t count(std::string_view word) const;       // 某个单词出现的次数，没有出现过是 0

    // 出现次数最多的 k 个单词：次数多的在前，次数相同时按字典序。k 大于 unique() 时返回全部
    std::vector<Entry> top(std::size_t k) const;

private:
    void bump(const std::string& word, std::int64_t times);

    std::unordered_map<std::string, std::int64_t> counts_;
    std::int64_t total_ = 0;
    std::string buffer_;  // add 切分单词时复用的缓冲区
};

// 用 threads 个线程统计 text：把 text 在单词边界处切成几段，每个线程统计一段，最后合并
Counter countParallel(std::string_view text, unsigned threads);

}  // namespace wordfreq
