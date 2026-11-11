// 《观察者模式》：Player 存 weak_ptr，观察者析构后自动失效；通知时先复制一份快照，回调里增删观察者也安全
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(const std::shared_ptr<PlayerObserver>& o) { observers_.push_back(o); }
  void tick(double pos) {
    auto snapshot = observers_;                    // 快照：回调里 attach / detach 不影响这一轮的遍历
    for (auto& w : snapshot)
      if (auto o = w.lock()) o->onProgress(pos);   // lock 成功期间，观察者不会被析构
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [](const std::weak_ptr<PlayerObserver>& w) { return w.expired(); }),
                     observers_.end());            // 顺手清掉已经析构的
  }
  std::size_t size() const { return observers_.size(); }
private:
  std::vector<std::weak_ptr<PlayerObserver>> observers_;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  ~Logger() override { std::printf("  （%s 析构）\n", name_); }
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

int main() {
  Player player;
  auto bar = std::make_shared<Logger>("进度条");
  auto panel = std::make_shared<Logger>("字幕面板");
  player.attach(bar);
  player.attach(panel);
  player.tick(1.0);
  panel.reset();                                   // 关掉字幕面板，不用 detach
  std::printf("面板关掉后，列表里还有 %zu 项\n", player.size());
  player.tick(2.0);
  std::printf("通知一轮后，列表里剩 %zu 项\n", player.size());
}
