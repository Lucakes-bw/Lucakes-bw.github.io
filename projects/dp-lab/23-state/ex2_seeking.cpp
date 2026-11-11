// 《状态模式》练习 2：加「跳转中」状态，跳转完回到跳转前的状态（播放或暂停）
#include <cstdio>

class Player;
class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void play(Player&) {}
  virtual void pause(Player&) {}
  virtual void seek(Player&, double) {}
  virtual void seekDone(Player&) {}
};
State& playing(); State& paused(); State& seeking();

class Player {
 public:
  double pos = 0;
  State* resumeTo = nullptr;   // 跳转完回到哪个状态：状态对象是共享的，这份数据要放在 Player 里
  void play() { state_->play(*this); }
  void pause() { state_->pause(*this); }
  void seek(double t) { state_->seek(*this, t); }
  void onSeekDone() { state_->seekDone(*this); }
  void transitionTo(State& next) {
    std::printf("  %s → %s\n", state_->name(), next.name());
    state_ = &next;
  }
  // 播放和暂停都可以跳转：记下当前状态，再进入「跳转中」
  void beginSeek(double t) { pos = t; resumeTo = state_; transitionTo(seeking()); }

 private:
  State* state_ = &paused();
};

class Playing : public State {
 public:
  const char* name() const override { return "播放"; }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void seek(Player& p, double t) override { p.beginSeek(t); }
};
State& playing() { static Playing s; return s; }

class Paused : public State {
 public:
  const char* name() const override { return "暂停"; }
  void play(Player& p) override { p.transitionTo(playing()); }
  void seek(Player& p, double t) override { p.beginSeek(t); }
};
State& paused() { static Paused s; return s; }

class Seeking : public State {
 public:
  const char* name() const override { return "跳转中"; }
  void play(Player& p) override { p.resumeTo = &playing(); }   // 跳转中按播放 / 暂停：只改「跳完去哪」
  void pause(Player& p) override { p.resumeTo = &paused(); }
  void seek(Player& p, double t) override { p.pos = t; }        // 连续拖动进度条：只更新目标位置
  void seekDone(Player& p) override { p.transitionTo(*p.resumeTo); }
};
State& seeking() { static Seeking s; return s; }

int main() {
  Player p;
  std::printf("暂停时跳转：\n");
  p.seek(3); p.onSeekDone();
  std::printf("播放时跳转，跳转中又拖了一次、按了暂停：\n");
  p.play(); p.seek(5); p.seek(7.5); p.pause(); p.onSeekDone();
  std::printf("最后停在 %.1f 秒\n", p.pos);
}
