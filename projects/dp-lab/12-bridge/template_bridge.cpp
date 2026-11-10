// 《桥接模式》：编译期的桥接，后端是模板参数。写的是 M + N 个类，组合由编译器按需要生成
#include <cstdio>
#include <string>

struct SoftBackend {
    static const char* name() { return "软件"; }
    void frame(int w, int h) { std::printf("    画 %d×%d 的字符画\n", w, h); }
};
struct MetalBackend {
    static const char* name() { return "Metal"; }
    void frame(int w, int h) { std::printf("    drawPrimitives %d×%d\n", w, h); }
};

template <class Backend> class MainWindow {           // 没有虚函数，后端是成员，不是指针
public:
    void draw() { std::printf("  主窗口（%s）\n", Backend::name()); be_.frame(30, 5); }
private:
    Backend be_;
};
template <class Backend> class Thumbnail {
public:
    void draw() { std::printf("  缩略图（%s）\n", Backend::name()); be_.frame(10, 3); }
private:
    Backend be_;
};

#ifdef __APPLE__
using PlatformBackend = MetalBackend;                 // 后端在编译时按平台选定
#else
using PlatformBackend = SoftBackend;
#endif

int main() {
    MainWindow<PlatformBackend> main;
    Thumbnail<SoftBackend> thumb;
    main.draw();
    thumb.draw();
    // thumb = Thumbnail<MetalBackend>();             // 编译错误：Thumbnail<SoftBackend> 和 Thumbnail<MetalBackend> 是两个不同的类型
}
