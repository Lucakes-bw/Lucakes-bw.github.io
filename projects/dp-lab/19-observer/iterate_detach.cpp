// 《观察者模式》：通知过程中有观察者 detach 自己——range-for 的迭代器失效，后面的观察者被跳过或收到两次
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
    if (it != observers_.end()) observers_.erase(it);
  }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos);   // 边遍历，回调里边 erase
  }
private:
  std::vector<PlayerObserver*> observers_;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

class FirstFrameStats : public PlayerObserver {        // 只关心第一次进度：上报首帧耗时后就退订
public:
  explicit FirstFrameStats(Player& p) : player_(p) {}
  void onProgress(double pos) override {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    player_.detach(this);
  }
private:
  Player& player_;
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  FirstFrameStats first(player);
  Logger bar("进度条"), sub("字幕"), lyrics("歌词");
  player.attach(&first);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);
  std::puts("tick(0)：");
  player.tick(0);
  std::puts("tick(1)：");
  player.tick(1);
}
