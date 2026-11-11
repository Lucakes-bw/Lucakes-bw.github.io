// 《观察者模式》练习 2 答案：只改 Player——下标遍历 + 延迟删除，回调里 detach 自己、attach 新观察者都正确
#include <algorithm>
#include <cstdio>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it == observers_.end()) return;
    if (depth_ > 0) *it = nullptr;                 // 正在通知：只置空，不动 vector 的结构
    else observers_.erase(it);
  }
  void tick(double pos) {
    ++depth_;
    const std::size_t n = observers_.size();       // 新 attach 的下一轮才通知
    for (std::size_t i = 0; i < n; ++i)            // 下标：扩容后 observers_[i] 照样有效
      if (auto* o = observers_[i]) o->onProgress(pos);
    if (--depth_ == 0)
      observers_.erase(std::remove(observers_.begin(), observers_.end(), nullptr), observers_.end());
  }
private:
  std::vector<PlayerObserver*> observers_;
  int depth_ = 0;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

class FirstFrameStats : public PlayerObserver {    // 回调里 detach 自己
public:
  explicit FirstFrameStats(Player& p) : player_(p) {}
  void onProgress(double pos) override {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    player_.detach(this);
  }
private:
  Player& player_;
};

class StallDetector : public PlayerObserver {      // 回调里 attach 新观察者
public:
  StallDetector(Player& p, PlayerObserver& extra) : player_(p), extra_(extra) {}
  void onProgress(double) override {
    if (!armed_) { armed_ = true; player_.attach(&extra_); }
  }
private:
  Player& player_;
  PlayerObserver& extra_;
  bool armed_ = false;
};

int main() {
  Player player;
  FirstFrameStats first(player);
  Logger bar("进度条"), sub("字幕"), stall("卡顿检测");
  StallDetector detector(player, stall);
  player.attach(&first);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&detector);                        // 4 个，capacity 正好用满
  std::puts("tick(0)：");
  player.tick(0);
  std::puts("tick(1)：");
  player.tick(1);
}
