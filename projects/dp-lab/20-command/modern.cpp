// 《命令模式》：三种写法的命令——虚函数类、一对 std::function（lambda）、std::variant——各占多少内存
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <variant>
#include <vector>

static std::size_t g_live = 0, g_allocs = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    ++g_allocs;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

struct Clip { int id; int start; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip& find(int id) {
        for (auto& c : clips)
            if (c.id == id) return c;
        return clips.front();
    }
};

// ---- 写法 1：虚函数类（前面一直在用的）----
struct Command {
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
};
struct SetVolume : Command {
    int id; double v, old = 0;
    SetVolume(int i, double x) : id(i), v(x) {}
    void execute(Timeline& tl) override { auto& c = tl.find(id); old = c.volume; c.volume = v; }
    void undo(Timeline& tl) override { tl.find(id).volume = old; }
};

// ---- 写法 2：一对 lambda ----
struct Action {
    std::function<void()> execute, undo;
};
Action setVolume(Timeline& tl, int id, double v) {
    double old = tl.find(id).volume;               // 构造时就把旧值记下
    // 捕获 id 而不是 Clip&：vector 扩容后引用会悬空（见 dangling_capture.cpp）
    return {[&tl, id, v] { tl.find(id).volume = v; },
            [&tl, id, old] { tl.find(id).volume = old; }};
}

// ---- 写法 3：std::variant，命令是普通的值 ----
struct Move { int id, to, from; };
struct Volume { int id; double v, old; };
using Cmd = std::variant<Move, Volume>;

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

void execute(Timeline& tl, Cmd& c) {
    std::visit(overloaded{
        [&](Move& m) { auto& x = tl.find(m.id); m.from = x.start; x.start = m.to; },
        [&](Volume& v) { auto& x = tl.find(v.id); v.old = x.volume; x.volume = v.v; }}, c);
}
void undo(Timeline& tl, const Cmd& c) {
    std::visit(overloaded{
        [&](const Move& m) { tl.find(m.id).start = m.from; },
        [&](const Volume& v) { tl.find(v.id).volume = v.old; }}, c);
}

template <class F>
void measure(const char* name, std::size_t elem, F build) {
    std::size_t live0 = g_live, a0 = g_allocs;
    std::size_t n = build();
    std::size_t heap = (g_live - live0) / n;
    std::printf("%-28s 数组元素 %2zu + 堆上 %2zu = %2zu 字节，%.0f 次堆分配\n", name, elem, heap, elem + heap,
                double(g_allocs - a0) / n);
}

int main() {
    Timeline tl{{{1, 0, 1.0}, {2, 3000, 1.0}, {3, 8000, 1.0}}};
    constexpr int N = 1000;

    std::vector<std::unique_ptr<Command>> h1;
    std::vector<Action> h2;
    std::vector<Cmd> h3;
    h1.reserve(N); h2.reserve(N); h3.reserve(N);   // 先把三个历史的数组分配好，只量命令本身
    std::printf("每条命令占的内存（历史数组已经预先 reserve）：\n");
    measure("unique_ptr<Command>", sizeof(std::unique_ptr<Command>), [&] {
        for (int i = 0; i < N; ++i) { h1.push_back(std::make_unique<SetVolume>(2, 0.5)); h1.back()->execute(tl); }
        return h1.size();
    });
    measure("Action（两个 std::function）", sizeof(Action), [&] {
        for (int i = 0; i < N; ++i) { h2.push_back(setVolume(tl, 2, 0.5)); h2.back().execute(); }
        return h2.size();
    });
    measure("variant<Move, Volume>", sizeof(Cmd), [&] {
        for (int i = 0; i < N; ++i) { h3.push_back(Volume{2, 0.5, 0}); execute(tl, h3.back()); }
        return h3.size();
    });

    // 三种都能撤销
    tl.find(2).volume = 1.0;
    h3.clear();
    h3.push_back(Move{3, 9000, 0});
    h3.push_back(Volume{2, 0.3, 0});
    for (auto& c : h3) execute(tl, c);
    std::printf("variant 版执行后：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
    for (auto it = h3.rbegin(); it != h3.rend(); ++it) undo(tl, *it);
    std::printf("倒着撤销后    ：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
}
