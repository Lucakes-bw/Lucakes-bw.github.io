// 《策略模式》：四种写法下，持有一个策略的上下文对象有多大；std::function 什么时候要分配堆内存
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <variant>

static int g_news = 0;                           // 数一数 operator new 被调用了几次
void* operator new(std::size_t n) {
    ++g_news;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct Plane;                                    // 这里只看大小，不需要真的缩放
struct Scaler { virtual ~Scaler() = default; virtual void scale(const Plane&) const = 0; };
struct Bilinear : Scaler { void scale(const Plane&) const override {} };
struct BilinearFn { void operator()(const Plane&) const {} };      // 空的函数对象：没有数据成员
struct LanczosFn { int a = 3; void operator()(const Plane&) const {} };

// 四种上下文，都另有两个 int（目标宽高）
struct CtxVirtual  { int w, h; std::unique_ptr<Scaler> s; };
struct CtxFunction { int w, h; std::function<void(const Plane&)> s; };
template <class S> struct CtxTemplate { int w, h; S s; };
template <class S> struct CtxTemplateEbo : private S { int w, h; };  // 空基类优化：空策略不占空间
struct CtxVariant  { int w, h; std::variant<BilinearFn, LanczosFn> s; };

// unique_ptr 的删除器也是一个编译期策略
struct Frame { int dummy; };
void frameFree(Frame* f) { delete f; }
struct FrameDeleter { void operator()(Frame* f) const { delete f; } };

int main() {
    std::printf("sizeof(CtxVirtual)             = %2zu  (+ 堆上的策略对象 %zu)\n", sizeof(CtxVirtual), sizeof(Bilinear));
    std::printf("sizeof(CtxFunction)            = %2zu\n", sizeof(CtxFunction));
    std::printf("sizeof(CtxTemplate<Bilinear>)  = %2zu\n", sizeof(CtxTemplate<BilinearFn>));
    std::printf("sizeof(CtxTemplateEbo<...>)    = %2zu\n", sizeof(CtxTemplateEbo<BilinearFn>));
    std::printf("sizeof(CtxVariant)             = %2zu\n", sizeof(CtxVariant));

    std::printf("\nunique_ptr<Frame>                       = %2zu\n", sizeof(std::unique_ptr<Frame>));
    std::printf("unique_ptr<Frame, FrameDeleter>         = %2zu\n", sizeof(std::unique_ptr<Frame, FrameDeleter>));
    std::printf("unique_ptr<Frame, void(*)(Frame*)>      = %2zu\n", sizeof(std::unique_ptr<Frame, void (*)(Frame*)>));
    std::printf("unique_ptr<Frame, function<...>>        = %2zu\n", sizeof(std::unique_ptr<Frame, std::function<void(Frame*)>>));
    std::unique_ptr<Frame, void (*)(Frame*)> f(new Frame{}, frameFree);

    std::printf("\nstd::function 捕获不同大小的数据时 new 了几次：\n");
    struct B8 { char d[8]; }; struct B16 { char d[16]; }; struct B24 { char d[24]; }; struct B32 { char d[32]; };
    auto test = [](const char* name, auto blob) {
        int before = g_news;
        std::function<int(const Plane&)> fn = [blob](const Plane&) { return blob.d[0] + static_cast<int>(sizeof blob); };
        std::printf("  捕获 %-8s → %d 次\n", name, g_news - before);
    };
    test("8 字节", B8{});
    test("16 字节", B16{});
    test("24 字节", B24{});
    test("32 字节", B32{});
}
