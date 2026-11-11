// 《观察者模式》：通知顺序——用 std::set 存观察者（为了去重），顺序取决于对象的地址，换个创建顺序就变了
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

class PlayerObserver {
public:
  explicit PlayerObserver(const char* name) : name_(name) {}
  virtual ~PlayerObserver() = default;
  void onProgress() { std::printf(" %s", name_); }
private:
  const char* name_;
};

void notifyAll(const std::set<PlayerObserver*>& observers) {
  for (auto* o : observers) o->onProgress();       // 按指针大小排序遍历
  std::puts("");
}

int main() {
  std::set<PlayerObserver*> a, b;
  // 第一次：按 进度条、字幕、统计 的顺序创建，按同样的顺序订阅
  auto bar1 = std::make_unique<PlayerObserver>("进度条");
  auto sub1 = std::make_unique<PlayerObserver>("字幕");
  auto stats1 = std::make_unique<PlayerObserver>("统计");
  a.insert(bar1.get()); a.insert(sub1.get()); a.insert(stats1.get());
  std::printf("创建顺序 进度条→字幕→统计，通知顺序：");
  notifyAll(a);

  // 第二次：订阅顺序不变，只是对象创建的先后变了
  auto stats2 = std::make_unique<PlayerObserver>("统计");
  auto sub2 = std::make_unique<PlayerObserver>("字幕");
  auto bar2 = std::make_unique<PlayerObserver>("进度条");
  b.insert(bar2.get()); b.insert(sub2.get()); b.insert(stats2.get());
  std::printf("创建顺序 统计→字幕→进度条，通知顺序：");
  notifyAll(b);
}
