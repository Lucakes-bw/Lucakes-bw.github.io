// 《状态模式》：表驱动的状态机：转换写成一张表，进入 / 退出动作按状态查表
#include <array>
#include <cstdio>

enum S : unsigned char { Idle, Loading, Playing, Paused, Buffering, Ended, Error, kStates };
enum E { Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered, kEvents };
const char* kNames[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};

struct Devices { bool file = false, audio = false, spinner = false; };
struct Transition { S from; E event; S to; };

constexpr Transition kTransitions[] = {   // 这张表就是状态图：一行一条边
    {Idle, Open, Loading},
    {Loading, Loaded, Playing}, {Loading, Fail, Error},   {Loading, Stop, Idle},
    {Playing, Pause, Paused},   {Playing, Stall, Buffering}, {Playing, Eof, Ended},
    {Playing, Fail, Error},     {Playing, Stop, Idle},
    {Paused, Play, Playing},    {Paused, Fail, Error},    {Paused, Stop, Idle},
    {Buffering, Buffered, Playing}, {Buffering, Pause, Paused},
    {Buffering, Fail, Error},   {Buffering, Stop, Idle},
    {Ended, Play, Playing},     {Ended, Open, Loading},   {Ended, Stop, Idle},
    {Error, Open, Loading},     {Error, Stop, Idle},
};

// 编译期把边的列表展开成 [状态][事件] 的二维数组，运行时一次下标就查到
constexpr auto buildTable() {
  std::array<std::array<S, kEvents>, kStates> t{};
  for (int s = 0; s < kStates; ++s)
    for (int e = 0; e < kEvents; ++e) t[s][e] = S(s);   // 默认：不变
  for (const auto& tr : kTransitions) t[tr.from][tr.event] = tr.to;
  return t;
}
constexpr auto kNext = buildTable();

using Action = void (*)(Devices&);
const Action kEnter[kStates] = {
    [](Devices& d) { d.file = false; },    // 空闲
    [](Devices& d) { d.file = true; },     // 加载中
    [](Devices& d) { d.audio = true; },    // 播放
    nullptr,                               // 暂停
    [](Devices& d) { d.spinner = true; },  // 缓冲中
    nullptr,                               // 结束
    [](Devices& d) { d.file = false; },    // 出错
};
const Action kExit[kStates] = {
    nullptr, nullptr,
    [](Devices& d) { d.audio = false; },   // 离开播放
    nullptr,
    [](Devices& d) { d.spinner = false; }, // 离开缓冲中
    nullptr, nullptr,
};

struct Player {
  S state = Idle;
  Devices dev;
  void fire(E e) {
    S next = kNext[state][e];
    if (next == state) return;
    if (kExit[state]) kExit[state](dev);
    std::printf("  %s → %s\n", kNames[state], kNames[next]);
    state = next;
    if (kEnter[state]) kEnter[state](dev);
  }
};

int main() {
  Player p;
  for (E e : {Open, Loaded, Stall, Play, Buffered, Pause, Play, Eof, Play, Stop}) p.fire(e);
  std::printf("转换表 %zu 条边，展开后 %d × %d = %d 格，占 %zu 字节\n",
              sizeof kTransitions / sizeof kTransitions[0], kStates, kEvents, kStates * kEvents,
              sizeof kNext);
}
