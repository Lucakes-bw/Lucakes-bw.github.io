// 《模板方法模式》：CRTP 版的子类漏写了必须实现的步骤 encode——编译期报错，而不是运行时才发现
#include <cstdio>

struct Frame { int index; int w, h; };

template <class Derived>
class ExportJob {
public:
    long run(int frames) {
        Derived& self = static_cast<Derived&>(*this);
        long bytes = 0;
        for (int i = 0; i < frames; ++i) bytes += self.encode(Frame{i, 1280, 720});
        return bytes;
    }
};

class WavExport : public ExportJob<WavExport> {
public:
    int encodeAudio(const Frame&) const { return 4096; }   // 名字写错了
};

int main() { std::printf("%ld\n", WavExport{}.run(470)); }
