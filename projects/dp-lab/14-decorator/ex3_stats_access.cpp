// 《装饰器模式》练习 3：Stats 被别的装饰器包在里面以后，怎么拿到它的统计结果——让它把结果写到外面的对象里
#include <cstdio>
#include <memory>

struct Packet { int index = 0; bool key = false; int size = 0; };

class PacketSource {
public:
    virtual ~PacketSource() = default;
    virtual bool read(Packet& out) = 0;
};

class MemorySource : public PacketSource {       // 240 个包，第 59、119、179、239 个是空包
public:
    bool read(Packet& p) override {
        if (i_ >= 240) return false;
        p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
        ++i_;
        return true;
    }
private:
    int i_ = 0;
};

class SourceDecorator : public PacketSource {
public:
    explicit SourceDecorator(std::unique_ptr<PacketSource> in) : inner_(std::move(in)) {}
    bool read(Packet& p) override { return inner_->read(p); }
protected:
    std::unique_ptr<PacketSource> inner_;
};

class Validate : public SourceDecorator {        // 丢掉空包
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        while (inner_->read(p)) if (p.size > 0) return true;
        return false;
    }
};

struct StatsResult { int packets = 0; long bytes = 0; };

class Stats : public SourceDecorator {
public:
    Stats(std::unique_ptr<PacketSource> in, StatsResult& out) : SourceDecorator(std::move(in)), out_(out) {}
    bool read(Packet& p) override {
        if (!inner_->read(p)) return false;
        ++out_.packets;
        out_.bytes += p.size;
        return true;
    }
private:
    StatsResult& out_;                           // 结果放在装饰器外面，谁都能看
};

int main() {
    StatsResult raw, valid;                      // 同一条链上可以放两个 Stats：坏包丢掉前、丢掉后各统计一次
    std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
    src = std::make_unique<Stats>(std::move(src), raw);
    src = std::make_unique<Validate>(std::move(src));
    src = std::make_unique<Stats>(std::move(src), valid);

    Packet p;
    while (src->read(p)) {}
    std::printf("丢包前 %d 个包，丢包后 %d 个包，丢掉 %d 个\n", raw.packets, valid.packets, raw.packets - valid.packets);
}
