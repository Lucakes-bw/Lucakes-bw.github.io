// 《状态模式》：改法：事件处理函数只返回下一个状态，由 Player 在调用返回之后再替换
#include <cstdio>
#include <memory>

class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual std::unique_ptr<State> pause() { return nullptr; }   // 空指针：状态不变
};

class Paused : public State {
 public:
  explicit Paused(double pos) : pos_(pos) {}
  const char* name() const override { return "暂停"; }
  double pos() const { return pos_; }

 private:
  double pos_;
};

class Playing : public State {
 public:
  explicit Playing(double pos) : pos_(pos) {}
  const char* name() const override { return "播放"; }
  std::unique_ptr<State> pause() override {
    std::printf("  在 %.1f 秒处暂停\n", pos_);    // this 还活着
    return std::make_unique<Paused>(pos_);
  }

 private:
  double pos_;
};

class Player {
 public:
  void pause() { replace(state_->pause()); }
  const State& state() const { return *state_; }

 private:
  void replace(std::unique_ptr<State> next) {    // 状态的成员函数已经返回，这时候 delete 旧状态是安全的
    if (next) state_ = std::move(next);
  }
  std::unique_ptr<State> state_ = std::make_unique<Playing>(3.5);
};

int main() {
  Player p;
  p.pause();
  auto& z = static_cast<const Paused&>(p.state());
  std::printf("现在的状态：%s，停在 %.1f 秒\n", z.name(), z.pos());
}
