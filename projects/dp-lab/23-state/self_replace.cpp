// 《状态模式》：状态对象在自己的成员函数里把自己换掉（unique_ptr 被重置），之后再用 this 就是释放后使用
#include <cstdio>
#include <memory>

class Player;
class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void pause(Player&) {}
};

class Player {
 public:
  Player();
  void pause() { state_->pause(*this); }
  void setState(std::unique_ptr<State> s) { state_ = std::move(s); }   // 旧状态对象在这里被 delete
  const State& state() const { return *state_; }

 private:
  std::unique_ptr<State> state_;
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
  void pause(Player& p) override {
    p.setState(std::make_unique<Paused>(pos_));   // 这一行返回时，*this 已经被 delete 了
    std::printf("  在 %.1f 秒处暂停\n", pos_);    // 读的是已经释放的内存
  }

 private:
  double pos_;
};

Player::Player() : state_(std::make_unique<Playing>(3.5)) {}

int main() {
  Player p;
  p.pause();
  auto& z = static_cast<const Paused&>(p.state());
  std::printf("现在的状态：%s，停在 %.1f 秒\n", z.name(), z.pos());
}
