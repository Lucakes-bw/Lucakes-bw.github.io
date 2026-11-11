// 《迭代器模式》：环形缓冲区把内部结构交给调用方——每个调用方都要自己算「第 i 个帧在哪个格子」
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AudioFrame {
    std::int64_t pts;   // 以采样为单位的时间戳
    int samples;        // 这一帧有多少个采样
    float peak;         // 这一帧的峰值（0～1）
};

// 最近 N 个音频帧：写满以后，新的帧覆盖最老的帧
class FrameRing {
public:
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();   // 满了：覆盖最老的，head 往前走
    }
    // 调用方要遍历，只好把内部全交出去
    const AudioFrame* data() const { return buf_.data(); }
    int capacity() const { return static_cast<int>(buf_.size()); }
    int head() const { return head_; }           // 最老的帧在哪个格子
    int size() const { return size_; }

private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
};

// ===== 调用方 =====

// 调用方 1：音量表，取最近这些帧的峰值
float peakLevel(const FrameRing& r) {
    float p = 0;
    for (int i = 0; i < r.size(); ++i)
        p = std::max(p, r.data()[(r.head() + i) % r.capacity()].peak);
    return p;
}

// 调用方 2：音画同步，找第一个 pts >= t 的帧
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {
    for (int i = 0; i < r.size(); ++i) {
        const AudioFrame& f = r.data()[(r.head() + i) % r.capacity()];
        if (f.pts >= t) return &f;
    }
    return nullptr;
}

// 调用方 3：调试输出——写的人以为数组就是按时间排好的
void dump(const FrameRing& r) {
    std::printf("dump:");
    for (int i = 0; i < r.size(); ++i) std::printf(" %lld", static_cast<long long>(r.data()[i].pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});   // 11 帧放进 8 个格子
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const AudioFrame* f = findAt(ring, 5000)) std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);   // 应该是 3 4 5 6 7 8 9 10
}
