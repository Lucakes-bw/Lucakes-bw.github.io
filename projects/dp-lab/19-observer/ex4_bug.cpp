// 《观察者模式》练习 4：找问题——进度条一次也没更新，字幕面板读到了已经释放的内存
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Connection {                                 // 订阅句柄：析构时退订
public:
  Connection() = default;
  explicit Connection(std::function<void()> off) : off_(std::move(off)) {}
  Connection(Connection&& o) noexcept : off_(std::exchange(o.off_, nullptr)) {}
  Connection& operator=(Connection&& o) noexcept {
    if (this != &o) { disconnect(); off_ = std::exchange(o.off_, nullptr); }
    return *this;
  }
  ~Connection() { disconnect(); }
  void disconnect() { if (auto f = std::exchange(off_, nullptr)) f(); }
private:
  std::function<void()> off_;
};

class ProgressSignal {                             // 简化版 Signal：通知时复制一份槽表
public:
  using Map = std::map<int, std::function<void(double)>>;
  [[nodiscard]] Connection connect(std::function<void(double)> f) {
    int id = next_++;
    slots_->emplace(id, std::move(f));
    std::weak_ptr<Map> w = slots_;
    return Connection([w, id] { if (auto m = w.lock()) m->erase(id); });
  }
  void emit(double pos) {
    auto copy = *slots_;
    for (auto& kv : copy) kv.second(pos);
  }
private:
  std::shared_ptr<Map> slots_ = std::make_shared<Map>();
  int next_ = 0;
};

class Player {
public:
  ProgressSignal progressChanged;
  void tick(double pos) { progressChanged.emit(pos); }
};

class SubtitlePanel {
public:
  SubtitlePanel(Player& p, std::string lang)
      : lang_(std::move(lang)), conn_(p.progressChanged.connect([this](double pos) { show(pos); })) {}
private:
  void show(double pos) { std::printf("  [字幕·%s] %.1f s\n", lang_.c_str(), pos); }
  std::string lang_;
  Connection conn_;
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Player player;
  player.progressChanged.connect([](double pos) { std::printf("  [进度条] %.1f s\n", pos); });

  std::vector<SubtitlePanel> panels;
  panels.emplace_back(player, "中文");
  panels.emplace_back(player, "English");
  player.tick(1.0);
}
