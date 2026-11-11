// 《迭代器模式》：迭代器版也改成「容量取 2 的幂 + 两个只增不减的计数器」——只改类的内部，调用方一行不动
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct AudioFrame {
    std::int64_t pts;   // 以采样为单位的时间戳
    int samples;        // 这一帧有多少个采样
    float peak;         // 这一帧的峰值（0～1）
};

class FrameRing {
public:
    explicit FrameRing(std::size_t capacity) : cap_(capacity), buf_(roundUp(capacity)), mask_(buf_.size() - 1) {}
    void push(const AudioFrame& f) {
        buf_[write_++ & mask_] = f;              // 用 & 代替 %：容量是 2 的幂
        if (write_ - read_ > cap_) ++read_;      // 满了：覆盖最老的
    }
    std::size_t size() const { return static_cast<std::size_t>(write_ - read_); }

    class const_iterator {
    public:
        // 这五个类型让标准库认识这个迭代器（std::iterator_traits 读的就是它们）
        using iterator_category = std::forward_iterator_tag;
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;
        using pointer = const AudioFrame*;
        using reference = const AudioFrame&;

        const_iterator() = default;
        reference operator*() const { return ring_->buf_[pos_ & ring_->mask_]; }
        pointer operator->() const { return &**this; }
        const_iterator& operator++() { ++pos_; return *this; }
        const_iterator operator++(int) { const_iterator t = *this; ++pos_; return t; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.pos_ == b.pos_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.pos_ != b.pos_; }

    private:
        friend class FrameRing;
        const_iterator(const FrameRing* r, std::uint64_t pos) : ring_(r), pos_(pos) {}
        const FrameRing* ring_ = nullptr;
        std::uint64_t pos_ = 0;                  // 帧的序号（只增不减）
    };
    const_iterator begin() const { return const_iterator(this, read_); }
    const_iterator end() const { return const_iterator(this, write_); }

private:
    static std::size_t roundUp(std::size_t n) { std::size_t p = 1; while (p < n) p <<= 1; return p; }
    std::size_t cap_;
    std::vector<AudioFrame> buf_;
    std::size_t mask_;
    std::uint64_t read_ = 0, write_ = 0;
};

// ===== 调用方 =====

// 调用方 1：音量表，取最近这些帧的峰值
float peakLevel(const FrameRing& r) {
    auto it = std::max_element(r.begin(), r.end(), [](const AudioFrame& a, const AudioFrame& b) { return a.peak < b.peak; });
    return it == r.end() ? 0.0f : it->peak;
}

// 调用方 2：音画同步，找第一个 pts >= t 的帧
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {
    auto it = std::find_if(r.begin(), r.end(), [t](const AudioFrame& f) { return f.pts >= t; });
    return it == r.end() ? nullptr : &*it;
}

// 调用方 3：调试输出
void dump(const FrameRing& r) {
    std::printf("dump:");
    for (const AudioFrame& f : r) std::printf(" %lld", static_cast<long long>(f.pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});   // 11 帧放进 8 个格子
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const AudioFrame* f = findAt(ring, 5000)) std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);
    std::printf("一共 %td 帧，其中峰值超过 0.3 的 %td 帧\n", std::distance(ring.begin(), ring.end()),
                std::count_if(ring.begin(), ring.end(), [](const AudioFrame& f) { return f.peak > 0.3f; }));
}
