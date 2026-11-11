// 《状态模式》练习 4：找问题：两个播放器同时缓冲，B 第一次超时就是「第 2 次重试」，第二次超时就报错了
#include <cstdio>

class Player;
class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void enter(Player&) {}
  virtual void timeout(Player&) {}
};
State& idle(); State& buffering(); State& error();

class Player {
 public:
  explicit Player(const char* n) : name(n) {}
  const char* name;
  void onTimeout() { state_->timeout(*this); }
  void transitionTo(State& next) {
    std::printf("  [%s] %s → %s\n", name, state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
  void startBuffering() { transitionTo(buffering()); }

 private:
  State* state_ = &idle();
};

class Idle : public State {
 public:
  const char* name() const override { return "空闲"; }
};
State& idle() { static Idle s; return s; }

class Buffering : public State {   // 缓冲超时就重试，最多 3 次
 public:
  const char* name() const override { return "缓冲中"; }
  void enter(Player&) override { retries_ = 0; }
  void timeout(Player& p) override {
    if (++retries_ >= 3) p.transitionTo(error());
    else std::printf("  [%s] 超时，第 %d 次重试\n", p.name, retries_);
  }

 private:
  int retries_ = 0;
};
State& buffering() { static Buffering s; return s; }

class Error : public State {
 public:
  const char* name() const override { return "出错"; }
};
State& error() { static Error s; return s; }

int main() {
  Player a("A"), b("B");
  a.startBuffering();
  a.onTimeout(); a.onTimeout();
  b.startBuffering();
  a.onTimeout();
  b.onTimeout();
  b.onTimeout();
}
