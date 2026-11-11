// 《观察者模式》：Signal + RAII 的 Connection——订阅返回一个句柄，句柄析构就自动退订；通知中途增删也安全
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Connection {                                 // 订阅句柄：只能移动，析构时退订
public:
  Connection() = default;
  explicit Connection(std::function<void()> off) : off_(std::move(off)) {}
  Connection(Connection&& o) noexcept : off_(std::exchange(o.off_, nullptr)) {}
  Connection& operator=(Connection&& o) noexcept {
    if (this != &o) { disconnect(); off_ = std::exchange(o.off_, nullptr); }
    return *this;
  }
  ~Connection() { disconnect(); }
  void disconnect() {
    if (auto f = std::exchange(off_, nullptr)) f();
  }
private:
  std::function<void()> off_;
};

template <class... Args>
class Signal {
public:
  [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
    auto slot = std::make_shared<Slot>(Slot{std::move(fn), true});
    st_->slots.push_back(slot);
    std::weak_ptr<State> ws = st_;                 // 句柄只拿弱引用：Signal 先析构也没关系
    std::weak_ptr<Slot> wslot = slot;
    return Connection([ws, wslot] {
      auto st = ws.lock();
      auto sl = wslot.lock();
      if (!st || !sl) return;
      sl->alive = false;                           // 先标记，不马上从 vector 里删
      st->compact();
    });
  }

  void emit(Args... args) {
    auto st = st_;
    ++st->depth;
    const std::size_t n = st->slots.size();        // 这一轮只通知已有的；回调里新连上的下一轮才收到
    for (std::size_t i = 0; i < n; ++i) {          // 用下标：push_back 扩容也不会让它失效
      auto slot = st->slots[i];                    // 复制 shared_ptr：回调里断开自己，function 也不会被析构
      if (slot->alive) slot->fn(args...);
    }
    --st->depth;
    st->compact();
  }

  std::size_t size() const { return st_->slots.size(); }

private:
  struct Slot {
    std::function<void(Args...)> fn;
    bool alive;
  };
  struct State {
    std::vector<std::shared_ptr<Slot>> slots;
    int depth = 0;                                 // 正在 emit 的层数（重入时大于 1）
    void compact() {                               // 延迟删除：没人在遍历时才真正删
      if (depth > 0) return;
      slots.erase(std::remove_if(slots.begin(), slots.end(), [](auto& s) { return !s->alive; }), slots.end());
    }
  };
  std::shared_ptr<State> st_ = std::make_shared<State>();
};

// ---------------- 用起来 ----------------

class Player {
public:
  Signal<double, double> progressChanged;          // (pos, dur)
  void tick(double pos) { progressChanged.emit(pos, 10.0); }
};

class SubtitlePanel {                              // 面板持有自己的 Connection：面板析构，订阅自动断开
public:
  explicit SubtitlePanel(Player& p)
      : conn_(p.progressChanged.connect([this](double pos, double) { show(pos); })) {}
private:
  void show(double pos) { std::printf("  [字幕] %.1f s：%s\n", pos, line_.c_str()); }
  std::string line_ = "Big Buck Bunny wakes up.";
  Connection conn_;                                // 放在最后：最先析构，先断开再销毁 line_
};

int main() {
  Player player;
  Connection bar = player.progressChanged.connect([](double pos, double dur) {
    std::printf("  [进度条] %.1f / %.1f s\n", pos, dur);
  });

  Connection first;                                // 只要第一次进度：回调里断开自己
  first = player.progressChanged.connect([&first](double pos, double) {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    first.disconnect();
  });

  std::optional<SubtitlePanel> panel;
  panel.emplace(player);
  std::printf("tick(0)，%zu 个订阅：\n", player.progressChanged.size());
  player.tick(0);

  panel.reset();                                   // 关掉字幕面板：不用手动退订
  std::printf("tick(1)，%zu 个订阅：\n", player.progressChanged.size());
  player.tick(1);

  {
    Signal<int> temp;
    Connection c = temp.connect([](int) {});
  }                                                // 先析构 c（断开），再析构 temp
  Connection late;
  {
    Signal<int> temp;
    late = temp.connect([](int) {});
  }                                                // temp 先没了，late 还在
  late.disconnect();                               // 弱引用 lock 失败，什么也不做
  std::puts("Signal 先析构，Connection 后析构：没有问题");
}
