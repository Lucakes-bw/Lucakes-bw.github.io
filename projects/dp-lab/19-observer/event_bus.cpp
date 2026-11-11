// 《观察者模式》：事件总线（发布-订阅）——发布者和订阅者只认识总线和事件类型，互相不知道对方是谁
#include <cstdio>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EventBus {
public:
  template <class E>
  void subscribe(std::function<void(const E&)> fn) {
    handlers_[std::type_index(typeid(E))].push_back(
        [fn = std::move(fn)](const void* e) { fn(*static_cast<const E*>(e)); });
  }
  template <class E>
  void publish(const E& e) {
    auto it = handlers_.find(std::type_index(typeid(E)));
    if (it == handlers_.end()) return;               // 没人订阅：发出去也没事
    auto copy = it->second;                          // 快照：回调里再订阅也安全
    for (auto& h : copy) h(&e);
  }
private:
  std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> handlers_;
};

// 事件只是普通的结构体
struct ProgressEvent { double pos; };
struct NetworkSlowEvent { int kbps; };

// 发布者：只依赖 EventBus 和事件类型
class Player {
public:
  explicit Player(EventBus& bus) : bus_(bus) {}
  void tick(double pos) { bus_.publish(ProgressEvent{pos}); }
private:
  EventBus& bus_;
};
class Downloader {
public:
  explicit Downloader(EventBus& bus) : bus_(bus) {}
  void measure(int kbps) { if (kbps < 800) bus_.publish(NetworkSlowEvent{kbps}); }
private:
  EventBus& bus_;
};

int main() {
  EventBus bus;
  // 订阅者：同样只认识总线和事件类型
  bus.subscribe<ProgressEvent>([](const ProgressEvent& e) { std::printf("  [弹幕] 滚到 %.1f s\n", e.pos); });
  bus.subscribe<ProgressEvent>([](const ProgressEvent& e) { std::printf("  [统计] 进度 %.1f s\n", e.pos); });
  bus.subscribe<NetworkSlowEvent>([](const NetworkSlowEvent& e) {
    std::printf("  [清晰度] 网速 %d kbps，切到 480p\n", e.kbps);
  });

  Player player(bus);
  Downloader downloader(bus);
  player.tick(3.0);
  downloader.measure(650);
  downloader.measure(2400);                          // 网速够：不发事件
}
