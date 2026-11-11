// 《状态模式》：五种写法分发同一串事件的开销、状态占多大、堆分配几次（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <variant>
#include <vector>

static long g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

enum S : unsigned char { Idle, Loading, Playing, Paused, Buffering, Ended, Error, kStates };
enum E : unsigned char { Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered, kEvents };
struct Devices { bool file = false, audio = false, spinner = false; };

// 进入 / 退出动作，五种写法共用
inline void enterS(S s, Devices& d) {
  switch (s) {
    case Idle: case Error: d.file = false; break;
    case Loading: d.file = true; break;
    case Playing: d.audio = true; break;
    case Buffering: d.spinner = true; break;
    default: break;
  }
}
inline void exitS(S s, Devices& d) {
  if (s == Playing) d.audio = false;
  if (s == Buffering) d.spinner = false;
}

// ---- 1. enum + switch ----
struct SwitchPlayer {
  S s = Idle; Devices dev; long n = 0;
  void go(S to) { exitS(s, dev); s = to; enterS(s, dev); ++n; }
  void fire(E e) {
    switch (s) {
      case Idle: if (e == Open) go(Loading); break;
      case Loading: if (e == Loaded) go(Playing); else if (e == Fail) go(Error); else if (e == Stop) go(Idle); break;
      case Playing:
        switch (e) {
          case Pause: go(Paused); break; case Stall: go(Buffering); break; case Eof: go(Ended); break;
          case Fail: go(Error); break; case Stop: go(Idle); break; default: break;
        }
        break;
      case Paused: if (e == Play) go(Playing); else if (e == Fail) go(Error); else if (e == Stop) go(Idle); break;
      case Buffering:
        if (e == Buffered) go(Playing); else if (e == Pause) go(Paused);
        else if (e == Fail) go(Error); else if (e == Stop) go(Idle);
        break;
      case Ended: if (e == Play) go(Playing); else if (e == Open) go(Loading); else if (e == Stop) go(Idle); break;
      case Error: if (e == Open) go(Loading); else if (e == Stop) go(Idle); break;
      default: break;
    }
  }
  S state() const { return s; }
};

// ---- 2 / 3. 状态模式：共享的状态对象，或每次转换 new 一个 ----
struct VPlayer;
struct VState {
  virtual ~VState() = default;
  virtual S id() const = 0;
  virtual void open(VPlayer&) {} virtual void loaded(VPlayer&) {} virtual void fail(VPlayer&) {}
  virtual void play(VPlayer&) {} virtual void pause(VPlayer&) {} virtual void stop(VPlayer&) {}
  virtual void eof(VPlayer&) {} virtual void stall(VPlayer&) {} virtual void buffered(VPlayer&) {}
};
struct VPlayer {
  explicit VPlayer(bool alloc);
  bool alloc; VState* st; std::unique_ptr<VState> owned; Devices dev; long n = 0; int next = -1;
  void go(S to) { next = to; }   // 状态对象只登记下一个状态，由 fire() 在调用返回后替换
  void fire(E e) {
    switch (e) {
      case Open: st->open(*this); break; case Loaded: st->loaded(*this); break;
      case Fail: st->fail(*this); break; case Play: st->play(*this); break;
      case Pause: st->pause(*this); break; case Stop: st->stop(*this); break;
      case Eof: st->eof(*this); break; case Stall: st->stall(*this); break;
      case Buffered: st->buffered(*this); break; default: break;
    }
    if (next >= 0) { exitS(st->id(), dev); replace(S(next)); enterS(st->id(), dev); ++n; next = -1; }
  }
  void replace(S to);
  S state() const { return st->id(); }
};
struct VIdle : VState { S id() const override { return Idle; } void open(VPlayer& p) override { p.go(Loading); } };
struct VLoading : VState {
  S id() const override { return Loading; }
  void loaded(VPlayer& p) override { p.go(Playing); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VPlaying : VState {
  S id() const override { return Playing; }
  void pause(VPlayer& p) override { p.go(Paused); } void stall(VPlayer& p) override { p.go(Buffering); }
  void eof(VPlayer& p) override { p.go(Ended); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VPaused : VState {
  S id() const override { return Paused; }
  void play(VPlayer& p) override { p.go(Playing); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VBuffering : VState {
  S id() const override { return Buffering; }
  void buffered(VPlayer& p) override { p.go(Playing); } void pause(VPlayer& p) override { p.go(Paused); }
  void fail(VPlayer& p) override { p.go(Error); } void stop(VPlayer& p) override { p.go(Idle); }
};
struct VEnded : VState {
  S id() const override { return Ended; }
  void play(VPlayer& p) override { p.go(Playing); } void open(VPlayer& p) override { p.go(Loading); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VError : VState {
  S id() const override { return Error; }
  void open(VPlayer& p) override { p.go(Loading); } void stop(VPlayer& p) override { p.go(Idle); }
};
VIdle gIdle; VLoading gLoading; VPlaying gPlaying; VPaused gPaused; VBuffering gBuffering; VEnded gEnded; VError gError;
VState* const kShared[kStates] = {&gIdle, &gLoading, &gPlaying, &gPaused, &gBuffering, &gEnded, &gError};
std::unique_ptr<VState> make(S s) {
  switch (s) {
    case Idle: return std::make_unique<VIdle>(); case Loading: return std::make_unique<VLoading>();
    case Playing: return std::make_unique<VPlaying>(); case Paused: return std::make_unique<VPaused>();
    case Buffering: return std::make_unique<VBuffering>(); case Ended: return std::make_unique<VEnded>();
    default: return std::make_unique<VError>();
  }
}
VPlayer::VPlayer(bool a) : alloc(a), st(&gIdle) { if (alloc) { owned = make(Idle); st = owned.get(); } }
void VPlayer::replace(S to) {
  if (alloc) { owned = make(to); st = owned.get(); } else st = kShared[to];
}

// ---- 4. 表驱动 ----
struct TablePlayer {
  static constexpr S _ = kStates;   // 不变
  static constexpr S kNext[kStates][kEvents] = {
      // Open Loaded Fail Play Pause Stop Eof Stall Buffered
      {Loading, _, _, _, _, _, _, _, _},
      {_, Playing, Error, _, _, Idle, _, _, _},
      {_, _, Error, _, Paused, Idle, Ended, Buffering, _},
      {_, _, Error, Playing, _, Idle, _, _, _},
      {_, _, Error, _, Paused, Idle, _, _, Playing},
      {Loading, _, _, Playing, _, Idle, _, _, _},
      {Loading, _, _, _, _, Idle, _, _, _},
  };
  S s = Idle; Devices dev; long n = 0;
  void fire(E e) {
    S to = kNext[s][e];
    if (to == _) return;
    exitS(s, dev); s = to; enterS(s, dev); ++n;
  }
  S state() const { return s; }
};

// ---- 5. std::variant + std::visit ----
namespace v {
struct Idle {}; struct Loading {}; struct Playing {}; struct Paused {}; struct Buffering {}; struct Ended {}; struct Error {};
using State = std::variant<Idle, Loading, Playing, Paused, Buffering, Ended, Error>;
struct Open {}; struct Loaded {}; struct Fail {}; struct Play {}; struct Pause {}; struct Stop {}; struct Eof {};
struct Stall {}; struct Buffered {};
using Event = std::variant<Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered>;
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
using Next = const State*;   // 空指针：不变（状态没有数据，指向常量对象即可）
const State kIdle = Idle{}, kLoading = Loading{}, kPlaying = Playing{}, kPaused = Paused{},
            kBuffering = Buffering{}, kEnded = Ended{}, kError = Error{};
struct Player {
  State s; Devices dev; long n = 0;
  void fire(const Event& e) {
    Next to = std::visit(overloaded{
        [](Idle, Open) -> Next { return &kLoading; }, [](Ended, Open) -> Next { return &kLoading; },
        [](Error, Open) -> Next { return &kLoading; }, [](Loading, Loaded) -> Next { return &kPlaying; },
        [](Playing, Pause) -> Next { return &kPaused; }, [](Playing, Stall) -> Next { return &kBuffering; },
        [](Playing, Eof) -> Next { return &kEnded; }, [](Paused, Play) -> Next { return &kPlaying; },
        [](Ended, Play) -> Next { return &kPlaying; }, [](Buffering, Pause) -> Next { return &kPaused; },
        [](Buffering, Buffered) -> Next { return &kPlaying; },
        [](Idle, Fail) -> Next { return nullptr; }, [](Ended, Fail) -> Next { return nullptr; },
        [](Error, Fail) -> Next { return nullptr; }, [](auto, Fail) -> Next { return &kError; },
        [](Idle, Stop) -> Next { return nullptr; }, [](auto, Stop) -> Next { return &kIdle; },
        [](auto, auto) -> Next { return nullptr; }}, s, e);
    if (!to) return;
    exitS(S(s.index()), dev); s = *to; enterS(S(s.index()), dev); ++n;
  }
  S state() const { return S(s.index()); }
};
const Event kEv[kEvents] = {Open{}, Loaded{}, Fail{}, Play{}, Pause{}, Stop{}, Eof{}, Stall{}, Buffered{}};
}  // namespace v

// ---- 测量 ----
template <class F>
double median_ns(F&& run, long events) {
  double t[7];
  for (double& x : t) {
    auto a = std::chrono::steady_clock::now();
    run();
    auto b = std::chrono::steady_clock::now();
    x = std::chrono::duration<double, std::nano>(b - a).count() / events;
  }
  std::sort(t, t + 7);
  return t[3];
}

int main() {
  // 事件串：伪随机，偏向「当前状态下有效」的事件，让一半左右的事件真的引起转换
  const int N = 1 << 16;
  std::vector<E> evs(N);
  unsigned x = 12345;
  for (auto& e : evs) { x = x * 1103515245u + 12345u; e = E((x >> 16) % kEvents); }
  const int R = 300;   // 每次测量分发 N × R ≈ 2000 万个事件
  const long total = long(N) * R;
  std::vector<v::Event> vevs(N);
  for (int i = 0; i < N; ++i) vevs[i] = v::kEv[evs[i]];

  long chk[5]; int fin[5]; long allocs[5];
  double ns[5];
  { SwitchPlayer p; long a0 = g_allocs;
    ns[0] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[0] = p.n; fin[0] = p.state(); allocs[0] = g_allocs - a0; }
  { VPlayer p(false); long a0 = g_allocs;
    ns[1] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[1] = p.n; fin[1] = p.state(); allocs[1] = g_allocs - a0; }
  { VPlayer p(true); long a0 = g_allocs;
    ns[2] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[2] = p.n; fin[2] = p.state(); allocs[2] = g_allocs - a0; }
  { TablePlayer p; long a0 = g_allocs;
    ns[3] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[3] = p.n; fin[3] = p.state(); allocs[3] = g_allocs - a0; }
  { v::Player p; long a0 = g_allocs;
    ns[4] = median_ns([&] { for (int r = 0; r < R; ++r) for (const auto& e : vevs) p.fire(e); }, total);
    chk[4] = p.n; fin[4] = p.state(); allocs[4] = g_allocs - a0; }

  const char* names[] = {"enum + switch", "状态模式（共享状态对象）", "状态模式（每次 new）", "表驱动", "variant + visit"};
  std::size_t holder[] = {sizeof(S), sizeof(VState*), sizeof(std::unique_ptr<VState>), sizeof(S), sizeof(v::State)};
  std::size_t heap[] = {0, 0, sizeof(VIdle), 0, 0};
  std::printf("每次测量分发 %ld 个事件，其中 %ld 个引起转换；7 次取中位数\n", total, chk[0] / 7);
  std::printf("  ns/事件  当前状态占  堆上  每次测量的堆分配  写法\n");
  for (int i = 0; i < 5; ++i) {
    if (chk[i] != chk[0] || fin[i] != fin[0]) std::printf("  结果不一致！\n");
    std::printf("  %6.2f   %4zu 字节  %2zu 字节  %10ld  %s\n", ns[i], holder[i], heap[i], allocs[i] / 7, names[i]);
  }
}
