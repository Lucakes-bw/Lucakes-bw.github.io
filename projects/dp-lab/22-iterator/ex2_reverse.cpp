// 《迭代器模式》练习 2：给环形缓冲区的迭代器加上 --，变成双向迭代器，于是 std::reverse_iterator 和 std::prev 都能用了
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

class FrameRing {
public:
    explicit FrameRing(std::size_t cap) : buf_(cap) {}   // cap 取 2 的幂
    void push(const AudioFrame& f) {
        buf_[write_++ & (buf_.size() - 1)] = f;
        if (write_ - read_ > buf_.size()) ++read_;
    }
    class const_iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;   // 改了分类
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;
        using pointer = const AudioFrame*;
        using reference = const AudioFrame&;

        const_iterator() = default;
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        reference operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        pointer operator->() const { return &**this; }
        const_iterator& operator++() { ++p_; return *this; }
        const_iterator operator++(int) { auto t = *this; ++p_; return t; }
        const_iterator& operator--() { --p_; return *this; }               // 新增
        const_iterator operator--(int) { auto t = *this; --p_; return t; } // 新增
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.p_ == b.p_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.p_ != b.p_; }

    private:
        const FrameRing* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;   // 新增
    const_iterator begin() const { return {this, read_}; }
    const_iterator end() const { return {this, write_}; }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});
    std::printf("最新的 3 帧（从新到旧）：");
    std::for_each(ring.rbegin(), std::next(ring.rbegin(), 3), [](const AudioFrame& f) { std::printf("%lld ", static_cast<long long>(f.pts / 1024)); });
    std::printf("\n最后一帧：%lld\n", static_cast<long long>(std::prev(ring.end())->pts / 1024));
    // 从后往前找最近一个峰值超过 0.4 的帧
    auto it = std::find_if(ring.rbegin(), ring.rend(), [](const AudioFrame& f) { return f.peak > 0.4f; });
    if (it != ring.rend()) std::printf("最近一个峰值超过 0.4 的帧：%lld\n", static_cast<long long>(it->pts / 1024));
}
