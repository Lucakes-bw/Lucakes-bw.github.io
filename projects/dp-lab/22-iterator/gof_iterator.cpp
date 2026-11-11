// 《迭代器模式》：GoF 原书的写法——抽象的 Iterator（First / Next / IsDone / CurrentItem），容器用 createIterator() 造一个
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

template <class T>
class Iterator {                                  // 抽象迭代器
public:
    virtual ~Iterator() = default;
    virtual void first() = 0;
    virtual void next() = 0;
    virtual bool isDone() const = 0;
    virtual const T& currentItem() const = 0;
};

class FrameRing {                                 // 具体聚合
public:
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % cap()] = f;
        if (size_ < cap()) ++size_;
        else head_ = (head_ + 1) % cap();
    }
    std::unique_ptr<Iterator<AudioFrame>> createIterator() const;   // 工厂方法：造一个配套的迭代器

private:
    int cap() const { return static_cast<int>(buf_.size()); }
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
    friend class FrameRingIterator;
};

class FrameRingIterator : public Iterator<AudioFrame> {   // 具体迭代器：只有它知道格子怎么排
public:
    explicit FrameRingIterator(const FrameRing& r) : r_(r) {}
    void first() override { i_ = 0; }
    void next() override { ++i_; }
    bool isDone() const override { return i_ >= r_.size_; }
    const AudioFrame& currentItem() const override { return r_.buf_[(r_.head_ + i_) % r_.cap()]; }

private:
    const FrameRing& r_;
    int i_ = 0;
};

std::unique_ptr<Iterator<AudioFrame>> FrameRing::createIterator() const { return std::make_unique<FrameRingIterator>(*this); }

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});
    auto it = ring.createIterator();
    std::printf("dump:");
    for (it->first(); !it->isDone(); it->next()) std::printf(" %lld", static_cast<long long>(it->currentItem().pts / 1024));
    std::printf("\n");
}
