// 《装饰器模式》练习 2：加一个 Limit 装饰器（只读前 n 个包），它和 Validate 谁在外面，结果不一样
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

class Limit : public SourceDecorator {
public:
    Limit(std::unique_ptr<PacketSource> in, int n) : SourceDecorator(std::move(in)), left_(n) {}
    bool read(Packet& p) override {
        if (left_ == 0) return false;            // 先判断还能不能读，不要多读一个包
        --left_;
        return inner_->read(p);
    }
private:
    int left_;
};

int count(PacketSource& src) {
    Packet p;
    int n = 0;
    while (src.read(p)) ++n;
    return n;
}

int main() {
    auto a = std::make_unique<Limit>(std::make_unique<Validate>(std::make_unique<MemorySource>()), 100);
    auto b = std::make_unique<Validate>(std::make_unique<Limit>(std::make_unique<MemorySource>(), 100));
    std::printf("Limit(Validate(src), 100)：%d 个包\n", count(*a));
    std::printf("Validate(Limit(src, 100))：%d 个包\n", count(*b));
}
