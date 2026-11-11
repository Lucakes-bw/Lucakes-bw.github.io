// 《观察者模式》：字幕面板关掉（析构）时忘了 detach，Player 手里留下悬空指针；用 -fsanitize=address 编译能抓到
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
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
    for (auto* o : observers_) o->onProgress(pos);
  }
private:
  std::vector<PlayerObserver*> observers_;
};

class ProgressBar : public PlayerObserver {
public:
  void onProgress(double pos) override { std::printf("  [进度条] %.1f s\n", pos); }
};

class SubtitlePanel : public PlayerObserver {
public:
  void onProgress(double pos) override { std::printf("  [字幕]   %.1f s：%s\n", pos, line_.c_str()); }
private:
  std::string line_ = "Big Buck Bunny wakes up in the meadow.";
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  ProgressBar bar;
  player.attach(&bar);
  {
    auto panel = std::make_unique<SubtitlePanel>();   // 用户打开字幕面板
    player.attach(panel.get());
    player.tick(1.0);
  }                                                   // 用户关掉面板：panel 析构了，没有 detach
  std::puts("-- 字幕面板已经关掉");
  player.tick(2.0);                                   // 对已经释放的对象调用虚函数：未定义行为
}
