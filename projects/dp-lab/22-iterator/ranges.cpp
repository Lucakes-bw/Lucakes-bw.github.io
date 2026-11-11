// 《迭代器模式》：C++20——用 concept 检查自己写的迭代器，再把环形缓冲区接上 std::views 的管道
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <ranges>
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
        using iterator_concept = std::forward_iterator_tag;   // C++20 的 concept 先看这个
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;                           // forward_iterator 要求能默认构造
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        const AudioFrame& operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        const_iterator& operator++() { ++p_; return *this; }
        const_iterator operator++(int) { auto t = *this; ++p_; return t; }
        bool operator==(const const_iterator& o) const { return p_ == o.p_; }   // != 由编译器自动生成

    private:
        const FrameRing* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    const_iterator begin() const { return {this, read_}; }
    const_iterator end() const { return {this, write_}; }

private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

static_assert(std::forward_iterator<FrameRing::const_iterator>);
static_assert(!std::bidirectional_iterator<FrameRing::const_iterator>);   // 没写 --
static_assert(std::ranges::forward_range<FrameRing>);

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});

    auto loudest = std::ranges::max(ring, {}, &AudioFrame::peak);           // 投影：按 peak 比较
    std::printf("最响的一帧：第 %lld 帧，峰值 %.1f\n", static_cast<long long>(loudest.pts / 1024), loudest.peak);

    // 惰性管道：不生成中间数组，取够 3 个就停
    auto loud = ring | std::views::filter([](const AudioFrame& f) { return f.peak > 0.2f; })
                     | std::views::transform([](const AudioFrame& f) { return f.pts / 1024; })
                     | std::views::take(3);
    std::printf("峰值超过 0.2 的前 3 帧：");
    for (long long k : loud) std::printf("%lld ", k);
    std::printf("\n");
}
