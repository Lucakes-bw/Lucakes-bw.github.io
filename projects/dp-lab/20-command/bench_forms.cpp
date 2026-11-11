// 《命令模式》：三种写法的命令，创建 + 执行 + 入栈一条、撤销一条各要多少纳秒（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

struct Clip { int id; int start; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip& find(int id) {
        for (auto& c : clips)
            if (c.id == id) return c;
        return clips.front();
    }
};

struct Command {
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
};
struct MoveCmd : Command {
    int id, to, from = 0;
    MoveCmd(int i, int t) : id(i), to(t) {}
    void execute(Timeline& tl) override { auto& c = tl.find(id); from = c.start; c.start = to; }
    void undo(Timeline& tl) override { tl.find(id).start = from; }
};
struct VolumeCmd : Command {
    int id; double v, old = 0;
    VolumeCmd(int i, double x) : id(i), v(x) {}
    void execute(Timeline& tl) override { auto& c = tl.find(id); old = c.volume; c.volume = v; }
    void undo(Timeline& tl) override { tl.find(id).volume = old; }
};

struct Action { std::function<void()> execute, undo; };
Action moveAction(Timeline& tl, int id, int to) {
    int from = tl.find(id).start;
    return {[&tl, id, to] { tl.find(id).start = to; }, [&tl, id, from] { tl.find(id).start = from; }};
}
Action volumeAction(Timeline& tl, int id, double v) {
    double old = tl.find(id).volume;
    return {[&tl, id, v] { tl.find(id).volume = v; }, [&tl, id, old] { tl.find(id).volume = old; }};
}

struct Move { int id, to, from; };
struct Volume { int id; double v, old; };
using Cmd = std::variant<Move, Volume>;
template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;
void execute(Timeline& tl, Cmd& c) {
    std::visit(overloaded{[&](Move& m) { auto& x = tl.find(m.id); m.from = x.start; x.start = m.to; },
                          [&](Volume& v) { auto& x = tl.find(v.id); v.old = x.volume; x.volume = v.v; }}, c);
}
void undo(Timeline& tl, const Cmd& c) {
    std::visit(overloaded{[&](const Move& m) { tl.find(m.id).start = m.from; },
                          [&](const Volume& v) { tl.find(v.id).volume = v.old; }}, c);
}

using Clock = std::chrono::steady_clock;
constexpr int N = 1'000'000, REPS = 7;

struct Cost { double run, undo; };   // 每条命令：执行+入栈、撤销+出栈的纳秒数

template <class Run, class Undo>
Cost bench(Run run, Undo undoAll) {
    std::vector<double> a, b;
    for (int r = 0; r < REPS; ++r) {
        auto t0 = Clock::now();
        run();
        auto t1 = Clock::now();
        undoAll();
        auto t2 = Clock::now();
        a.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / N);
        b.push_back(std::chrono::duration<double, std::nano>(t2 - t1).count() / N);
    }
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return {a[REPS / 2], b[REPS / 2]};
}

int main() {
    Timeline tl{{{1, 0, 1.0}, {2, 3000, 1.0}, {3, 8000, 1.0}}};
    std::vector<std::unique_ptr<Command>> h1;
    std::vector<Action> h2;
    std::vector<Cmd> h3;
    h1.reserve(N); h2.reserve(N); h3.reserve(N);
    auto r1 = bench([&] {
        for (int i = 0; i < N; ++i) {
            std::unique_ptr<Command> c;
            if (i & 1) c = std::make_unique<VolumeCmd>(2, i * 1e-6);
            else c = std::make_unique<MoveCmd>(3, i);
            c->execute(tl);
            h1.push_back(std::move(c));
        }
    }, [&] { while (!h1.empty()) { h1.back()->undo(tl); h1.pop_back(); } });
    auto r2 = bench([&] {
        for (int i = 0; i < N; ++i) {
            h2.push_back((i & 1) ? volumeAction(tl, 2, i * 1e-6) : moveAction(tl, 3, i));
            h2.back().execute();
        }
    }, [&] { while (!h2.empty()) { h2.back().undo(); h2.pop_back(); } });
    auto r3 = bench([&] {
        for (int i = 0; i < N; ++i) {
            if (i & 1) h3.push_back(Volume{2, i * 1e-6, 0});
            else h3.push_back(Move{3, i, 0});
            execute(tl, h3.back());
        }
    }, [&] { while (!h3.empty()) { undo(tl, h3.back()); h3.pop_back(); } });
    std::printf("%d 条命令（移动和调音量交替），%d 轮取中位数，单位 ns/条\n", N, REPS);
    std::printf("%-26s %10s %10s\n", "写法", "执行+入栈", "撤销+出栈");
    std::printf("%-26s %10.2f %10.2f\n", "unique_ptr<Command>", r1.run, r1.undo);
    std::printf("%-26s %10.2f %10.2f\n", "两个 std::function", r2.run, r2.undo);
    std::printf("%-26s %10.2f %10.2f\n", "variant + visit", r3.run, r3.undo);
    std::printf("最后状态：#3 在 %dms，#2 音量 %.1f（都撤销完，应为 8000 和 1.0）\n", tl.find(3).start, tl.find(2).volume);
}
