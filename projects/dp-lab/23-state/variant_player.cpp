// 《状态模式》：std::variant + std::visit 写状态机：状态、事件都是普通结构体，可以各带各的数据
#include <cstdio>
#include <optional>
#include <string>
#include <variant>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Idle {};                                   // 状态
struct Loading { std::string url; };
struct Playing {};
struct Paused {};
struct Buffering { int percent = 0; };
struct Ended {};
struct Error { std::string why; };
using State = std::variant<Idle, Loading, Playing, Paused, Buffering, Ended, Error>;

struct Open { std::string url; };                 // 事件
struct Loaded {};
struct Fail { std::string why; };
struct Play {};
struct Pause {};
struct Stop {};
struct Eof {};
struct Stall {};
struct Progress { int percent; };
struct Buffered {};
using Event = std::variant<Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Progress, Buffered>;

using Next = std::optional<State>;   // 空：状态不变

// 转换函数：按 (状态, 事件) 的类型重载；没列出的组合落到最后的泛型版本
Next transition(const State& s, const Event& e) {
  return std::visit(overloaded{
      [](const Idle&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Ended&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Error&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Loading&, const Loaded&) -> Next { return Playing{}; },
      [](const Playing&, const Pause&) -> Next { return Paused{}; },
      [](const Playing&, const Stall&) -> Next { return Buffering{}; },
      [](const Playing&, const Eof&) -> Next { return Ended{}; },
      [](const Paused&, const Play&) -> Next { return Playing{}; },
      [](const Ended&, const Play&) -> Next { return Playing{}; },
      [](const Buffering&, const Pause&) -> Next { return Paused{}; },
      [](const Buffering&, const Progress& p) -> Next { return Buffering{p.percent}; },
      [](const Buffering&, const Buffered&) -> Next { return Playing{}; },
      [](const Idle&, const Fail&) -> Next { return {}; },
      [](const Ended&, const Fail&) -> Next { return {}; },
      [](const Error&, const Fail&) -> Next { return {}; },
      [](const auto&, const Fail& f) -> Next { return Error{f.why}; },   // 其余状态出错都进 Error
      [](const Idle&, const Stop&) -> Next { return {}; },
      [](const auto&, const Stop&) -> Next { return Idle{}; },           // 其余状态都能停止
      [](const auto&, const auto&) -> Next { return {}; },
  }, s, e);
}

struct Devices { bool file = false, audio = false, spinner = false; };

void enter(const State& s, Devices& d) {
  std::visit(overloaded{
      [&](const Idle&) { d.file = false; },
      [&](const Loading&) { d.file = true; },
      [&](const Playing&) { d.audio = true; },
      [&](const Buffering&) { d.spinner = true; },
      [&](const Error&) { d.file = false; },
      [](const auto&) {},
  }, s);
}
void exit(const State& s, Devices& d) {
  std::visit(overloaded{
      [&](const Playing&) { d.audio = false; },
      [&](const Buffering&) { d.spinner = false; },
      [](const auto&) {},
  }, s);
}

const char* name(const State& s) {
  static const char* n[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};
  return n[s.index()];
}

class Player {
 public:
  Devices dev;
  void fire(const Event& e) {
    Next n = transition(state_, e);
    if (!n) return;
    exit(state_, dev);
    std::printf("  %s → %s", name(state_), name(*n));
    state_ = std::move(*n);
    enter(state_, dev);
    if (auto* b = std::get_if<Buffering>(&state_)) std::printf("（%d%%）", b->percent);
    if (auto* x = std::get_if<Error>(&state_)) std::printf("（%s）", x->why.c_str());
    std::printf("\n");
  }

 private:
  State state_;
};

int main() {
  Player p;
  for (const Event& e : {Event{Open{"bbb-10s.mp4"}}, Event{Loaded{}}, Event{Stall{}},
                         Event{Progress{40}}, Event{Play{}}, Event{Progress{100}}, Event{Buffered{}},
                         Event{Stall{}}, Event{Fail{"网络断开"}}, Event{Open{"bbb-10s.mp4"}}})
    p.fire(e);
  std::printf("sizeof(State) = %zu，sizeof(Event) = %zu，sizeof(Player) = %zu\n",
              sizeof(State), sizeof(Event), sizeof(Player));
}
