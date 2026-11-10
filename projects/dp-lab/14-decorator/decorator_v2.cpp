// 《装饰器模式》：再加一种功能「丢掉坏包」，只要新写一个装饰器，再在使用的地方多套一层
#include <cstdio>
#include <memory>
#include <vector>

struct Packet {
    int index = 0;
    long long pts = 0;                 // 时间戳，单位 1/12288 秒（和 bbb-10s.mp4 的视频流一样，每帧 512）
    bool key = false;                  // 关键帧
    std::vector<unsigned char> data;
};

// ---------- 抽象接口：能一个一个地读出包 ----------
class PacketSource {
public:
    virtual ~PacketSource() = default;
    virtual bool read(Packet& out) = 0;          // 读到返回 true，读完返回 false
};

// ---------- 被装饰的对象：模拟测试视频的 240 个视频包 ----------
class MemorySource : public PacketSource {
public:
    explicit MemorySource(int n = 240) : n_(n) {}
    bool read(Packet& p) override {
        if (i_ >= n_) return false;
        p.index = i_;
        p.pts = i_ * 512LL;
        p.key = i_ % 48 == 0;                    // 每 48 帧一个关键帧
        int size = p.key ? 9000 : 1200 + i_ * 37 % 800;
        if (i_ % 60 == 59) size = 0;             // 第 59、119、179、239 个包是坏的（空包）
        p.data.assign(size, static_cast<unsigned char>(i_));
        ++i_;
        return true;
    }
private:
    int n_, i_ = 0;
};

// ---------- 装饰器的基类：持有一个 PacketSource，默认原样转发 ----------
class SourceDecorator : public PacketSource {
public:
    explicit SourceDecorator(std::unique_ptr<PacketSource> inner) : inner_(std::move(inner)) {}
    bool read(Packet& p) override { return inner_->read(p); }
protected:
    std::unique_ptr<PacketSource> inner_;
};

class Logging : public SourceDecorator {         // 读到关键帧时打一行日志
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧 pts=%.2fs %zu 字节\n", p.index, p.pts / 12288.0, p.data.size());
        return true;
    }
};

class Stats : public SourceDecorator {           // 统计包数和字节数
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) { report(); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    void report() const { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); }
    int packets_ = 0;
    size_t bytes_ = 0;
};

class Checksum : public SourceDecorator {        // 对所有字节算 Adler-32
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class Validate : public SourceDecorator {        // 丢掉空包（坏包）
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        while (inner_->read(p)) {
            if (!p.data.empty()) return true;
            ++dropped_;
        }
        return false;
    }
private:
    int dropped_ = 0;
};

int main() {
    // 想要「统计 + 校验 + 日志（+ 丢掉坏包）」：从里往外一层层套上去
    std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
    src = std::make_unique<Validate>(std::move(src));
    src = std::make_unique<Logging>(std::move(src));
    src = std::make_unique<Checksum>(std::move(src));
    src = std::make_unique<Stats>(std::move(src));

    Packet p;
    while (src->read(p)) {}                      // 使用者只认识 PacketSource
}
