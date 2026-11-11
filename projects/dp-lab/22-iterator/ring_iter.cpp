// 《迭代器模式》：给环形缓冲区写一个迭代器——调用方只用 begin/end，range-for 和 <algorithm> 都能直接用
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
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();   // 满了：覆盖最老的，head 往前走
    }
    int capacity() const { return static_cast<int>(buf_.size()); }
    int size() const { return size_; }

    class const_iterator {
    public:
        // 这五个类型让标准库认识这个迭代器（std::iterator_traits 读的就是它们）
        using iterator_category = std::forward_iterator_tag;
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;
        using pointer = const AudioFrame*;
        using reference = const AudioFrame&;

        const_iterator() = default;
        reference operator*() const { return ring_->buf_[(ring_->head_ + i_) % ring_->capacity()]; }
        pointer operator->() const { return &**this; }
        const_iterator& operator++() { ++i_; return *this; }
        const_iterator operator++(int) { const_iterator t = *this; ++i_; return t; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.i_ == b.i_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.i_ != b.i_; }

    private:
        friend class FrameRing;
        const_iterator(const FrameRing* r, int i) : ring_(r), i_(i) {}
        const FrameRing* ring_ = nullptr;
        int i_ = 0;                              // 逻辑下标：最老的帧是 0
    };
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size_); }

private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
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
