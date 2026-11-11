// 《观察者模式》：通知过程中有观察者 attach 新观察者——push_back 扩容，正在用的迭代器指向已经释放的内存
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
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos);
  }
  std::size_t capacity() const { return observers_.capacity(); }
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

class StallDetector : public PlayerObserver {          // 第一次收到进度时，顺手再挂一个「卡顿检测」
public:
  StallDetector(Player& p, PlayerObserver& extra) : player_(p), extra_(extra) {}
  void onProgress(double pos) override {
    std::printf("  [统计] %.1f s，开始监听卡顿\n", pos);
    if (!armed_) { armed_ = true; player_.attach(&extra_); }
  }
private:
  Player& player_;
  PlayerObserver& extra_;
  bool armed_ = false;
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  Logger bar("进度条"), sub("字幕"), lyrics("歌词"), stall("卡顿检测");
  StallDetector stats(player, stall);
  player.attach(&stats);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);
  std::printf("4 个观察者，capacity = %zu\n", player.capacity());
  player.tick(0);
  std::printf("capacity = %zu\n", player.capacity());
}
