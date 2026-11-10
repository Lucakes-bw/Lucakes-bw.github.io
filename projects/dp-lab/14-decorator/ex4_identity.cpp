// 《装饰器模式》练习 4：包上装饰器以后，dynamic_cast 就认不出原来的对象了
#include <cstdio>
#include <memory>

struct Packet { int index = 0; bool key = false; int size = 0; };

class PacketSource {
public:
    virtual ~PacketSource() = default;
    virtual bool read(Packet& out) = 0;
    virtual int total() const { return -1; }     // 修正：把要问的问题放进接口，-1 表示不知道
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
    int total() const override { return inner_->total(); }   // 修正：装饰器把问题转给里面一层
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

class FileInfo : public MemorySource {           // 原来的写法：只有 FileInfo 知道总包数
public:
    int packetCount() const { return 240; }
    int total() const override { return 240; }
};

void describe(const char* what, PacketSource& src) {
    if (auto* f = dynamic_cast<FileInfo*>(&src))   // 有问题的写法：按具体类型去问
        std::printf("%s：dynamic_cast 得到 %d 个包", what, f->packetCount());
    else
        std::printf("%s：dynamic_cast 失败", what);
    std::printf("；total()：%d\n", src.total());
}

int main() {
    FileInfo plain;
    describe("没包装", plain);
    auto wrapped = std::make_unique<Validate>(std::make_unique<FileInfo>());
    describe("包了一层 Validate", *wrapped);
}
