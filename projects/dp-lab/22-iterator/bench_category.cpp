// 《迭代器模式》：同一个环形缓冲区迭代器标成三种分类，再和 std::list 比：std::distance / std::lower_bound 差多少（分别用 -O2 和 -O0 编译）
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <list>
#include <random>
#include <vector>

template <class T>
class Ring {
public:
    explicit Ring(std::size_t cap) : buf_(cap) {}   // cap 必须是 2 的幂
    void push(const T& v) {
        buf_[write_++ & (buf_.size() - 1)] = v;
        if (write_ - read_ > buf_.size()) ++read_;
    }

    // Tag 只决定「告诉标准库自己是哪一类」；下面的运算两种都实现了
    template <class Tag>
    class Iter {
    public:
        using iterator_category = Tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iter() = default;
        Iter(const Ring* r, std::uint64_t p) : r_(r), p_(p) {}
        reference operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        pointer operator->() const { return &**this; }
        reference operator[](difference_type n) const { return *(*this + n); }
        Iter& operator++() { ++p_; return *this; }
        Iter operator++(int) { Iter t = *this; ++p_; return t; }
        Iter& operator--() { --p_; return *this; }
        Iter operator--(int) { Iter t = *this; --p_; return t; }
        Iter& operator+=(difference_type n) { p_ += n; return *this; }
        Iter& operator-=(difference_type n) { p_ -= n; return *this; }
        friend Iter operator+(Iter a, difference_type n) { return a += n; }
        friend Iter operator+(difference_type n, Iter a) { return a += n; }
        friend Iter operator-(Iter a, difference_type n) { return a -= n; }
        friend difference_type operator-(const Iter& a, const Iter& b) { return static_cast<difference_type>(a.p_ - b.p_); }
        friend bool operator==(const Iter& a, const Iter& b) { return a.p_ == b.p_; }
        friend bool operator!=(const Iter& a, const Iter& b) { return a.p_ != b.p_; }
        friend bool operator<(const Iter& a, const Iter& b) { return a.p_ < b.p_; }
        friend bool operator>(const Iter& a, const Iter& b) { return a.p_ > b.p_; }
        friend bool operator<=(const Iter& a, const Iter& b) { return a.p_ <= b.p_; }
        friend bool operator>=(const Iter& a, const Iter& b) { return a.p_ >= b.p_; }

    private:
        const Ring* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    template <class Tag> Iter<Tag> begin() const { return {this, read_}; }
    template <class Tag> Iter<Tag> end() const { return {this, write_}; }

private:
    std::vector<T> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

volatile std::int64_t sink;

template <class F>
double medianUs(F f) {   // 跑 7 遍取中位数，单位微秒
    std::vector<double> t;
    for (int k = 0; k < 7; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

template <class GetBegin, class GetEnd>
void run(GetBegin b, GetEnd e, const std::vector<std::int64_t>& queries, const char* name) {
    double d = medianUs([&] {
        for (int k = 0; k < 10; ++k) sink = std::distance(b(), e());
    }) / 10;
    double lb = medianUs([&] {
        for (std::int64_t q : queries) sink = *std::lower_bound(b(), e(), q);
    }) / queries.size();
    std::printf("%-28s std::distance %11.3f us   std::lower_bound %11.3f us/次\n", name, d, lb);
}

int main() {
    const std::size_t cap = 1 << 20;                   // 约 100 万帧的时间戳
    Ring<std::int64_t> ring(cap);
    for (std::int64_t k = 0; k < static_cast<std::int64_t>(cap + cap / 2); ++k) ring.push(k * 1024);   // 写满后又绕了半圈
    std::mt19937_64 rng(42);
    std::vector<std::int64_t> queries(20);
    for (auto& q : queries) q = static_cast<std::int64_t>(cap / 2 + rng() % cap) * 1024;   // 都在缓冲区里

    std::printf("%zu 个元素\n", static_cast<std::size_t>(std::distance(ring.begin<std::random_access_iterator_tag>(), ring.end<std::random_access_iterator_tag>())));
    using F = std::forward_iterator_tag;
    using B = std::bidirectional_iterator_tag;
    using R = std::random_access_iterator_tag;
    run([&] { return ring.begin<F>(); }, [&] { return ring.end<F>(); }, queries, "Ring 标成 forward");
    run([&] { return ring.begin<B>(); }, [&] { return ring.end<B>(); }, queries, "Ring 标成 bidirectional");
    run([&] { return ring.begin<R>(); }, [&] { return ring.end<R>(); }, queries, "Ring 标成 random_access");

    std::list<std::int64_t> lst(ring.begin<R>(), ring.end<R>());       // 同样的数据放进链表：迭代器是 bidirectional
    run([&] { return lst.begin(); }, [&] { return lst.end(); }, queries, "std::list（bidirectional）");
}
