// 《迭代器模式》：环形缓冲区写满后覆盖旧帧——迭代器没有悬空，却悄悄指向了另一帧；带序号检查的迭代器能发现
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        // 还在缓冲区里吗：序号必须在 [read_, write_] 之间（等于 write_ 的是 end）
        bool valid() const { return p_ >= r_->read_ && p_ <= r_->write_; }
        const AudioFrame& operator*() const {
            assert(valid() && "迭代器指向的帧已经被覆盖");     // 调试版检查，发布版（-DNDEBUG）没有开销
            return r_->buf_[p_ & (r_->buf_.size() - 1)];
        }
        const AudioFrame* operator->() const { return &**this; }
        const_iterator& operator++() { ++p_; return *this; }
        bool operator!=(const const_iterator& o) const { return p_ != o.p_; }
        std::uint64_t seq() const { return p_; }

    private:
        const FrameRing* r_;
        std::uint64_t p_;
    };
    const_iterator begin() const { return {this, read_}; }
    const_iterator end() const { return {this, write_}; }
    // 不检查的取法：只看格子，不看序号（很多环形缓冲区就是这么写的）
    const AudioFrame& slot(std::uint64_t seq) const { return buf_[seq & (buf_.size() - 1)]; }

private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 8; ++k) ring.push({k * 1024LL, 1024, 0.1f});
    FrameRing::const_iterator it = ring.begin();               // 渲染线程记住「下一个要播的帧」
    std::printf("it 指向第 %lld 帧，valid=%d\n", static_cast<long long>(it->pts / 1024), it.valid());

    for (int k = 8; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.9f});   // 解码线程又写了 3 帧，覆盖最老的 3 帧
    std::printf("写入 3 帧之后：格子里现在是第 %lld 帧（本来要的是第 %llu 帧），valid=%d\n",
                static_cast<long long>(ring.slot(it.seq()).pts / 1024), static_cast<unsigned long long>(it.seq()), it.valid());
    std::printf("begin() 已经是第 %lld 帧\n", static_cast<long long>(ring.begin()->pts / 1024));
}
