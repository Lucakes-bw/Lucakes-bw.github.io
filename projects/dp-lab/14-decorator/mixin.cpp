// 《装饰器模式》：模板 mixin——在编译期一层层继承，类型里写清楚套了哪几层，没有虚函数，也不用分配内存
#include <cstdio>

struct Packet { int index = 0; bool key = false; int size = 0; };

class MemorySource {                             // 不需要虚函数，也不需要共同的基类
public:
    bool read(Packet& p) {
        if (i_ >= 240) return false;
        p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
        ++i_;
        return true;
    }
private:
    int i_ = 0;
};

template <class Inner> class Validate : public Inner {      // 丢掉空包
public:
    bool read(Packet& p) {
        while (Inner::read(p)) if (p.size > 0) return true;
        return false;
    }
};

template <class Inner> class Logging : public Inner {
public:
    bool read(Packet& p) {
        if (!Inner::read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧\n", p.index);
        return true;
    }
};

template <class Inner> class Stats : public Inner {
public:
    bool read(Packet& p) {
        if (!Inner::read(p)) return false;
        ++packets_;
        bytes_ += p.size;
        return true;
    }
    int packets() const { return packets_; }    // 最外层的类型知道每一层，额外的接口可以直接调用
    long bytes() const { return bytes_; }
private:
    int packets_ = 0;
    long bytes_ = 0;
};

int main() {
    Stats<Logging<Validate<MemorySource>>> src;  // 读法和对象版一样：从右往左是从里往外
    Packet p;
    while (src.read(p)) {}
    std::printf("%d 个包，%ld 字节；sizeof(src) = %zu 字节\n", src.packets(), src.bytes(), sizeof src);

    Validate<Stats<MemorySource>> other;         // 顺序换一下：Stats 在里面，坏包也被数进去了
    while (other.read(p)) {}
    std::printf("Validate<Stats<...>> 数到 %d 个包\n", other.packets());
}
