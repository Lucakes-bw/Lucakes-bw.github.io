// 《迭代器模式》：把环形缓冲区改成「容量取 2 的幂 + 两个只增不减的计数器」——暴露了内部结构，调用方跟着改
#include <algorithm>
#include <cstddef>
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
    explicit FrameRing(std::size_t capacity) : cap_(capacity), buf_(roundUp(capacity)), mask_(buf_.size() - 1) {}
    void push(const AudioFrame& f) {
        buf_[write_++ & mask_] = f;              // 用 & 代替 %：容量是 2 的幂
        if (write_ - read_ > cap_) ++read_;      // 满了：覆盖最老的
    }
    // 调用方要遍历，只好把内部全交出去
    const AudioFrame* data() const { return buf_.data(); }
    std::size_t mask() const { return mask_; }
    std::uint64_t readPos() const { return read_; }   // 最老的帧的序号（只增不减，用的时候 & mask）
    std::size_t size() const { return static_cast<std::size_t>(write_ - read_); }

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
    float p = 0;
    for (std::size_t i = 0; i < r.size(); ++i)
        p = std::max(p, r.data()[(r.readPos() + i) & r.mask()].peak);
    return p;
}

// 调用方 2：音画同步，找第一个 pts >= t 的帧
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {
    for (std::size_t i = 0; i < r.size(); ++i) {
        const AudioFrame& f = r.data()[(r.readPos() + i) & r.mask()];
        if (f.pts >= t) return &f;
    }
    return nullptr;
}

// 调用方 3：调试输出——写的人以为数组就是按时间排好的
void dump(const FrameRing& r) {
    std::printf("dump:");
    for (std::size_t i = 0; i < r.size(); ++i) std::printf(" %lld", static_cast<long long>(r.data()[i].pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});   // 11 帧放进 8 个格子
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const AudioFrame* f = findAt(ring, 5000)) std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);   // 应该是 3 4 5 6 7 8 9 10
}
