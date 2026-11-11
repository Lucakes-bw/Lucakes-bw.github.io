// 《观察者模式》：多线程下的通知——在锁内回调会死锁；锁住整个通知、锁内复制快照、写时复制三种做法的开销；退订返回后还会不会被调用
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Fn = std::function<void(double)>;

// ---------------- 1. 在锁内回调：回调里等别的线程订阅，那个线程拿不到锁 ----------------
class LockedSubject {
public:
  void subscribe(Fn f) {
    std::lock_guard<std::mutex> lk(m_);
    fns_.push_back(std::move(f));
  }
  void notify(double pos) {
    std::lock_guard<std::mutex> lk(m_);              // 整个通知过程都拿着锁
    for (std::size_t i = 0; i < fns_.size(); ++i) fns_[i](pos);
  }
private:
  std::mutex m_;
  std::vector<Fn> fns_;
};

void deadlock_demo() {
  std::puts("1. 在锁内回调：");
  LockedSubject player;
  std::future<void> pending;
  player.subscribe([&](double) {
    // 回调里让 UI 线程去订阅一个新模块，并等它完成（很多框架的「同步投递到 UI 线程」就是这样）
    pending = std::async(std::launch::async, [&] { player.subscribe([](double) {}); });
    if (pending.wait_for(300ms) == std::future_status::timeout)
      std::puts("   等了 300 ms，UI 线程还拿不到锁：它在等通知线程放锁，通知线程在等它——如果用 get() 一直等，就是死锁");
  });
  player.notify(1.0);
  pending.get();                                      // 通知结束放了锁，UI 线程这才订阅成功
  std::puts("   notify 返回、放锁之后，UI 线程才订阅成功");
}

// ---------------- 2. 三种线程安全的通知，开销对比 ----------------
double work(double x) {                               // 观察者干的一点活（约 100 ns）
  for (int i = 0; i < 40; ++i) x = std::sqrt(x + i);
  return x;
}
thread_local double sink = 0;

class LockAll {                                       // 做法一：锁住整个通知
public:
  void subscribe(std::shared_ptr<Fn> f) { std::lock_guard<std::mutex> lk(m_); fns_.push_back(std::move(f)); }
  void notify(double p) {
    std::lock_guard<std::mutex> lk(m_);
    for (auto& f : fns_) (*f)(p);
  }
private:
  std::mutex m_;
  std::vector<std::shared_ptr<Fn>> fns_;
};

class SnapshotCopy {                                  // 做法二：锁内复制一份，锁外回调
public:
  void subscribe(std::shared_ptr<Fn> f) { std::lock_guard<std::mutex> lk(m_); fns_.push_back(std::move(f)); }
  void unsubscribe(const std::shared_ptr<Fn>& f) {
    std::lock_guard<std::mutex> lk(m_);
    fns_.erase(std::find(fns_.begin(), fns_.end(), f));
  }
  void notify(double p) {
    std::vector<std::shared_ptr<Fn>> copy;
    {
      std::lock_guard<std::mutex> lk(m_);
      copy = fns_;                                    // 分配一次内存，每个元素原子加一
    }
    for (auto& f : copy) (*f)(p);
  }
private:
  std::mutex m_;
  std::vector<std::shared_ptr<Fn>> fns_;
};

class CopyOnWrite {                                   // 做法三：写时复制，通知只复制一个 shared_ptr
public:
  using List = std::vector<std::shared_ptr<Fn>>;
  void subscribe(std::shared_ptr<Fn> f) {
    std::lock_guard<std::mutex> lk(m_);
    auto next = std::make_shared<List>(*list_);       // 订阅、退订很少：复制整张表
    next->push_back(std::move(f));
    list_ = std::move(next);
  }
  void unsubscribe(const std::shared_ptr<Fn>& f) {
    std::lock_guard<std::mutex> lk(m_);
    auto next = std::make_shared<List>(*list_);
    next->erase(std::find(next->begin(), next->end(), f));
    list_ = std::move(next);
  }
  void notify(double p) {
    std::shared_ptr<const List> cur;
    {
      std::lock_guard<std::mutex> lk(m_);
      cur = list_;                                    // 通知很多：只复制一个指针
    }
    for (auto& f : *cur) (*f)(p);
  }
private:
  std::mutex m_;
  std::shared_ptr<const List> list_ = std::make_shared<List>();
};

template <class S>
double bench(int threads) {
  S s;
  for (int i = 0; i < 8; ++i) s.subscribe(std::make_shared<Fn>([](double p) { sink += work(p); }));
  constexpr int kN = 200'000;
  std::vector<double> times;
  for (int r = -1; r < 7; ++r) {                      // 第 -1 轮热身，不计入
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
      ts.emplace_back([&] { for (int i = 0; i < kN; ++i) s.notify(i); });
    for (auto& t : ts) t.join();
    auto t1 = std::chrono::steady_clock::now();
    if (r >= 0) times.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / (double(kN) * threads));
  }
  std::sort(times.begin(), times.end());
  return times[3];
}

// ---------------- 3. 锁外回调的代价：退订返回之后，回调还可能被调用 ----------------
int late_calls() {
  CopyOnWrite s;
  std::atomic<bool> stop{false};
  std::atomic<bool> unsubscribed{false};
  std::atomic<int> late{0};
  std::thread decoder([&] { while (!stop) s.notify(1.0); });
  for (int trial = 0; trial < 1000; ++trial) {
    unsubscribed = false;
    auto f = std::make_shared<Fn>([&](double) { if (unsubscribed) ++late; });
    s.subscribe(f);
    std::this_thread::sleep_for(20us);
    s.unsubscribe(f);
    unsubscribed = true;                              // 从这一刻起，按理说不该再被调用
    std::this_thread::sleep_for(20us);
  }
  stop = true;
  decoder.join();
  return late;
}

int main() {
  deadlock_demo();

  std::printf("\n2. 8 个观察者，平均每次通知占用的时间（ns，总耗时 ÷ 总通知次数，7 轮中位数）\n");
  std::printf("                    1 线程   2 线程\n");
  std::printf("   锁住整个通知  %8.1f %8.1f\n", bench<LockAll>(1), bench<LockAll>(2));
  std::printf("   锁内复制快照  %8.1f %8.1f\n", bench<SnapshotCopy>(1), bench<SnapshotCopy>(2));
  std::printf("   写时复制      %8.1f %8.1f\n", bench<CopyOnWrite>(1), bench<CopyOnWrite>(2));

  std::printf("\n3. 写时复制，订阅 / 退订 1000 次：退订返回之后又被调用了 %d 次\n", late_calls());
}
