// 《命令模式》：200 个片段的时间线做 1000 次操作，比较三种撤销方式的内存、每次操作的耗时和撤销 1000 步的耗时（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
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

struct Clip {
    int id;
    std::string source;
    int start, in, out;
    double volume = 1.0;
    std::vector<std::string> filters;
    bool operator==(const Clip& o) const {
        return id == o.id && source == o.source && start == o.start && in == o.in && out == o.out &&
               volume == o.volume && filters == o.filters;
    }
};

constexpr int N = 200, OPS = 1000, REPS = 7;
const char* kFilter = "eq=brightness=0.06:saturation=1.2";

std::vector<Clip> makeClips() {
    std::vector<Clip> v;
    for (int i = 0; i < N; ++i)
        v.push_back({i + 1, "bbb-10s.mp4", i * 2000, i * 40 % 8000, i * 40 % 8000 + 2000, 1.0,
                     {"scale=1920:1080:flags=lanczos"}});
    return v;
}
int opId(int k) { return k * 37 % N + 1; }

// 第 k 次操作：0、2 移动，1 调音量，3 加滤镜
template <class Clipish>
void applyOp(Clipish& c, int k) {
    switch (k % 4) {
    case 0: case 2: c.start += 10; break;
    case 1: c.volume = 0.1 * (k % 7 + 1); break;
    default: c.filters.push_back(kFilter);
    }
}

// ---------- A. 整份快照 ----------
struct TimelineA {
    std::vector<Clip> clips;
    Clip& find(int id) { return *std::find_if(clips.begin(), clips.end(), [&](auto& c) { return c.id == id; }); }
};

// ---------- B. 共享快照：片段不可变，改哪个复制哪个 ----------
struct TimelineB {
    std::vector<std::shared_ptr<const Clip>> clips;
    void modify(int id, int k) {
        auto it = std::find_if(clips.begin(), clips.end(), [&](auto& c) { return c->id == id; });
        auto copy = std::make_shared<Clip>(**it);
        applyOp(*copy, k);
        *it = std::move(copy);
    }
};

// ---------- C. 命令 ----------
struct TimelineC {
    std::vector<Clip> clips;
    Clip& find(int id) { return *std::find_if(clips.begin(), clips.end(), [&](auto& c) { return c.id == id; }); }
};
class Command {
public:
    virtual ~Command() = default;
    virtual void execute(TimelineC&) = 0;
    virtual void undo(TimelineC&) = 0;
};
class MoveClip : public Command {
    int id_, to_, from_ = 0;
public:
    MoveClip(int id, int to) : id_(id), to_(to) {}
    void execute(TimelineC& tl) override { auto& c = tl.find(id_); from_ = c.start; c.start = to_; }
    void undo(TimelineC& tl) override { tl.find(id_).start = from_; }
};
class SetVolume : public Command {
    int id_; double v_, old_ = 0;
public:
    SetVolume(int id, double v) : id_(id), v_(v) {}
    void execute(TimelineC& tl) override { auto& c = tl.find(id_); old_ = c.volume; c.volume = v_; }
    void undo(TimelineC& tl) override { tl.find(id_).volume = old_; }
};
class AddFilter : public Command {
    int id_; std::string f_;
public:
    AddFilter(int id, std::string f) : id_(id), f_(std::move(f)) {}
    void execute(TimelineC& tl) override { tl.find(id_).filters.push_back(f_); }
    void undo(TimelineC& tl) override { tl.find(id_).filters.pop_back(); }
};
std::unique_ptr<Command> makeCmd(TimelineC& tl, int k) {
    int id = opId(k);
    switch (k % 4) {
    case 0: case 2: return std::make_unique<MoveClip>(id, tl.find(id).start + 10);
    case 1: return std::make_unique<SetVolume>(id, 0.1 * (k % 7 + 1));
    default: return std::make_unique<AddFilter>(id, kFilter);
    }
}

using Clock = std::chrono::steady_clock;
double us(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::micro>(b - a).count(); }

struct Result { double opUs, undoUs, kb, allocs; bool same; };

// 历史占多少内存：做完 1000 次操作后把历史销毁，看释放了多少字节
template <class H>
double freedKB(H& hist) {
    std::size_t before = g_live;
    H().swap(hist);
    return (before - g_live) / 1024.0;
}

Result runA(const std::vector<Clip>& init) {
    TimelineA tl{init};
    std::vector<TimelineA> hist;
    auto a0 = g_allocs;
    auto t0 = Clock::now();
    for (int k = 0; k < OPS; ++k) {
        hist.push_back(tl);                       // 每次操作前存一份整条时间线
        applyOp(tl.find(opId(k)), k);
    }
    auto t1 = Clock::now();
    double allocs = double(g_allocs - a0);
    auto t2 = Clock::now();
    while (!hist.empty()) { tl = std::move(hist.back()); hist.pop_back(); }
    auto t3 = Clock::now();
    bool same = tl.clips == init;
    TimelineA tl2{init};                          // 再做一遍，只为量历史的大小
    std::vector<TimelineA> h2;
    for (int k = 0; k < OPS; ++k) { h2.push_back(tl2); applyOp(tl2.find(opId(k)), k); }
    return {us(t0, t1) / OPS, us(t2, t3), freedKB(h2), allocs, same};
}

Result runB(const std::vector<Clip>& init) {
    auto make = [&] {
        TimelineB tl;
        for (auto& c : init) tl.clips.push_back(std::make_shared<const Clip>(c));
        return tl;
    };
    TimelineB tl = make();
    std::vector<TimelineB> hist;
    auto a0 = g_allocs;
    auto t0 = Clock::now();
    for (int k = 0; k < OPS; ++k) {
        hist.push_back(tl);                       // 只复制 200 个 shared_ptr
        tl.modify(opId(k), k);                    // 被改的那个片段复制一份
    }
    auto t1 = Clock::now();
    double allocs = double(g_allocs - a0);
    auto t2 = Clock::now();
    while (!hist.empty()) { tl = std::move(hist.back()); hist.pop_back(); }
    auto t3 = Clock::now();
    bool same = tl.clips.size() == init.size();
    for (std::size_t i = 0; same && i < init.size(); ++i) same = *tl.clips[i] == init[i];
    TimelineB tl2 = make();
    std::vector<TimelineB> h2;
    for (int k = 0; k < OPS; ++k) { h2.push_back(tl2); tl2.modify(opId(k), k); }
    return {us(t0, t1) / OPS, us(t2, t3), freedKB(h2), allocs, same};
}

Result runC(const std::vector<Clip>& init) {
    TimelineC tl{init};
    std::vector<std::unique_ptr<Command>> hist;
    auto a0 = g_allocs;
    auto t0 = Clock::now();
    for (int k = 0; k < OPS; ++k) {
        auto c = makeCmd(tl, k);
        c->execute(tl);
        hist.push_back(std::move(c));
    }
    auto t1 = Clock::now();
    double allocs = double(g_allocs - a0);
    auto t2 = Clock::now();
    while (!hist.empty()) { hist.back()->undo(tl); hist.pop_back(); }
    auto t3 = Clock::now();
    bool same = tl.clips == init;
    TimelineC tl2{init};
    std::vector<std::unique_ptr<Command>> h2;
    for (int k = 0; k < OPS; ++k) { auto c = makeCmd(tl2, k); c->execute(tl2); h2.push_back(std::move(c)); }
    return {us(t0, t1) / OPS, us(t2, t3), freedKB(h2), allocs, same};
}

template <class F>
Result median(F f, const std::vector<Clip>& init) {
    std::vector<Result> rs;
    for (int r = 0; r < REPS; ++r) rs.push_back(f(init));
    auto med = [&](auto get) {
        std::vector<double> v;
        for (auto& r : rs) v.push_back(get(r));
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    return {med([](auto& r) { return r.opUs; }), med([](auto& r) { return r.undoUs; }),
            med([](auto& r) { return r.kb; }), med([](auto& r) { return r.allocs; }), rs[0].same};
}

int main() {
    auto init = makeClips();
    std::printf("%d 个片段，%d 次操作（移动 / 调音量 / 加滤镜），%d 轮取中位数\n", N, OPS, REPS);
    std::printf("%-14s %12s %12s %14s %16s %s\n", "方式", "历史(KB)", "堆分配次数", "每次操作(us)", "撤销1000步(us)", "撤销完");
    struct { const char* name; Result r; } rows[] = {
        {"整份快照", median(runA, init)}, {"共享快照", median(runB, init)}, {"命令", median(runC, init)}};
    for (auto& x : rows)
        std::printf("%-14s %12.1f %12.0f %14.3f %16.1f %s\n", x.name, x.r.kb, x.r.allocs, x.r.opUs, x.r.undoUs,
                    x.r.same ? "和初始一致" : "不一致！");
}
