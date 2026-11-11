// 《状态模式》：每个状态一个类，由状态对象决定下一个状态（6 个状态），main 逐格对照规格表检查
#include <cstdio>
#include <string>

struct Devices { bool file = false, audio = false, spinner = false; };
class Player;

class State {   // 每个事件一个虚函数：默认什么也不做；fail 和 stop 每个状态都必须自己写
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void enter(Player&) {}   // 进入动作
  virtual void exit(Player&) {}    // 退出动作
  virtual void open(Player&, const std::string&) {}
  virtual void loaded(Player&) {}
  virtual void fail(Player&) = 0;
  virtual void play(Player&) {}
  virtual void pause(Player&) {}
  virtual void stop(Player&) = 0;
  virtual void eof(Player&) {}
};
// 状态对象没有成员变量，所有播放器共用一份
State& idle(); State& loading(); State& playing(); State& paused(); State& ended(); State& error();

class Player {
 public:
  Devices dev;
  bool verbose = false;
  std::string url;

  void open(const std::string& u) { state_->open(*this, u); }
  void onLoaded() { state_->loaded(*this); }
  void onFail() { state_->fail(*this); }
  void play() { state_->play(*this); }
  void pause() { state_->pause(*this); }
  void stop() { state_->stop(*this); }
  void onEof() { state_->eof(*this); }

  void transitionTo(State& next) {   // 换状态：先退出旧的，再进入新的
    state_->exit(*this);
    if (verbose) std::printf("  %s → %s\n", state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
  const State& state() const { return *state_; }
  void force(State& s) { state_ = &s; }   // 测试用：直接换成某个状态，不执行进入动作

 private:
  State* state_ = &idle();
};

class Idle : public State {
 public:
  const char* name() const override { return "空闲"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player&) override {}
};
State& idle() { static Idle s; return s; }

class Loading : public State {
 public:
  const char* name() const override { return "加载中"; }
  void enter(Player& p) override { p.dev.file = true; }
  void loaded(Player& p) override { p.transitionTo(playing()); }
  void fail(Player& p) override { p.transitionTo(error()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& loading() { static Loading s; return s; }

class Playing : public State {
 public:
  const char* name() const override { return "播放"; }
  void enter(Player& p) override { p.dev.audio = true; }
  void exit(Player& p) override { p.dev.audio = false; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
  void eof(Player& p) override { p.transitionTo(ended()); }
};
State& playing() { static Playing s; return s; }

class Paused : public State {
 public:
  const char* name() const override { return "暂停"; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void play(Player& p) override { p.transitionTo(playing()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& paused() { static Paused s; return s; }

class Ended : public State {
 public:
  const char* name() const override { return "结束"; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void play(Player& p) override { p.transitionTo(playing()); }   // 从头重播
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& ended() { static Ended s; return s; }

class Error : public State {
 public:
  const char* name() const override { return "出错"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& error() { static Error s; return s; }

// ---- 以下是测试：6 个状态 × 7 个事件，逐格和规格表对照 ----
const char* kEvents[] = {"open", "loaded", "fail", "play", "pause", "stop", "eof"};
void fire(Player& p, int e) {
  switch (e) {
    case 0: p.open("bbb-10s.mp4"); break;
    case 1: p.onLoaded(); break;
    case 2: p.onFail(); break;
    case 3: p.play(); break;
    case 4: p.pause(); break;
    case 5: p.stop(); break;
    case 6: p.onEof(); break;
  }
}

int main() {
  Player p;
  p.verbose = true;
  std::printf("演示：打开、播放、暂停、继续、播完、重播、停止\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.pause(); p.play(); p.onEof(); p.play(); p.stop();

  State* states[] = {&idle(), &loading(), &playing(), &paused(), &ended(), &error()};
  enum { I, L, P, Z, E, X, _ = -1 };   // 规格表：spec[状态][事件] = 下一个状态，_ 表示不变
  const int spec[6][7] = {
      // open loaded fail play pause stop eof
      {L, _, _, _, _, _, _},   // 空闲
      {_, P, X, _, _, I, _},   // 加载中
      {_, _, X, _, Z, I, E},   // 播放
      {_, _, X, P, _, I, _},   // 暂停
      {L, _, _, P, _, I, _},   // 结束
      {L, _, _, _, _, I, _},   // 出错
  };
  int bad = 0;
  for (int s = 0; s < 6; ++s)
    for (int e = 0; e < 7; ++e) {
      Player q;
      q.force(*states[s]);
      q.dev = {s != I && s != X, s == P, false};
      fire(q, e);
      int want = spec[s][e] == _ ? s : spec[s][e], got = 0;
      while (states[got] != &q.state()) ++got;
      bool devOk = q.dev.audio == (got == P) && q.dev.spinner == false &&
                   q.dev.file == (got != I && got != X);
      if (got != want || !devOk) {
        ++bad;
        std::printf("  ✗ %s + %s：得到 %s，应该是 %s%s\n", states[s]->name(), kEvents[e],
                    states[got]->name(), states[want]->name(), devOk ? "" : "（设备状态也不对）");
      }
    }
  std::printf("6 个状态 × 7 个事件 = 42 格，和规格不一致的：%d 格\n", bad);
}
