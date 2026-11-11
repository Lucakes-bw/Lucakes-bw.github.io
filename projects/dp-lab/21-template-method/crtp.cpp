// 《模板方法模式》：编译期的模板方法——CRTP：基类模板通过 static_cast 调子类的步骤，没有虚函数
#include <cstdio>

struct Frame { int index; int w, h; };

template <class Derived>
class ExportJob {
public:
    void run(int frames) {                       // 模板方法：流程写在基类模板里
        Derived& self = static_cast<Derived&>(*this);
        int kept = 0;
        long bytes = 0;
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720};
            if (!self.process(f)) continue;      // 子类写了 process 就调子类的，没写就调下面的默认版本
            bytes += self.encode(f);             // 子类必须写 encode，否则编译不过
            ++kept;
        }
        std::printf("%-4s %4d 帧 %9ld 字节\n", self.name(), kept, bytes);
    }

protected:
    bool process(Frame&) { return true; }        // 钩子的默认实现
};

class Mp4Export : public ExportJob<Mp4Export> {
    friend class ExportJob<Mp4Export>;           // 步骤是私有的，只让基类调用
    const char* name() const { return "MP4"; }
    int encode(const Frame& f) const { return f.w * f.h / 40; }
};

class GifExport : public ExportJob<GifExport> {
    friend class ExportJob<GifExport>;
    const char* name() const { return "GIF"; }
    bool process(Frame& f) {                     // 「重写」钩子：其实是隐藏了基类的同名函数
        if (f.index % 2 != 0) return false;
        f.w = 480; f.h = 270;
        return true;
    }
    int encode(const Frame& f) const { return f.w * f.h / 3; }
};

struct VirtualJob {                              // 对照：有虚函数的类，对象里多一个虚表指针
    virtual ~VirtualJob() = default;
    virtual int encode(const Frame& f) const = 0;
};

int main() {
    Mp4Export mp4;
    GifExport gif;
    mp4.run(240);
    gif.run(240);
    std::printf("sizeof(Mp4Export) = %zu，sizeof(VirtualJob) = %zu\n", sizeof(Mp4Export), sizeof(VirtualJob));
    // Mp4Export 和 GifExport 没有共同的基类：ExportJob<Mp4Export> 和 ExportJob<GifExport> 是两个不相关的类型，
    // 不能放进同一个 vector<ExportJob*> 里
}
