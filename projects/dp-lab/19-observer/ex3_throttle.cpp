// 《观察者模式》练习 3 答案：Player 每帧都通知；节流、只在跨过边界时转发，都做成包在回调外面的一层，Player 不用知道
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

using ProgressFn = std::function<void(double)>;

class Player {
public:
  void onProgress(ProgressFn f) { fns_.push_back(std::move(f)); }
  void tick(double pos) { for (auto& f : fns_) f(pos); }
private:
  std::vector<ProgressFn> fns_;
};

// 每隔 interval 秒最多转发一次
ProgressFn throttle(double interval, ProgressFn f) {
  return [interval, f = std::move(f), last = -1e9](double pos) mutable {
    if (pos - last >= interval - 1e-9) { last = pos; f(pos); }
  };
}

// 只在跨过某个边界（比如下一句字幕的开始时间）时转发
ProgressFn on_crossing(std::vector<double> marks, ProgressFn f) {
  return [marks = std::move(marks), f = std::move(f), next = std::size_t{0}](double pos) mutable {
    bool crossed = false;
    while (next < marks.size() && pos >= marks[next]) { ++next; crossed = true; }
    if (crossed) f(pos);
  };
}

int main() {
  Player player;
  int bar = 0, stats = 0, subtitle = 0;
  player.onProgress([&](double) { ++bar; });                                   // 进度条：每帧都要
  player.onProgress(throttle(1.0, [&](double) { ++stats; }));                  // 统计：每秒一次
  player.onProgress(on_crossing({0.0, 1.2, 2.5}, [&](double pos) {            // 字幕：换句时才要
    ++subtitle;
    std::printf("  [字幕] %.3f s 换下一句\n", pos);
  }));
  for (int frame = 0; frame < 72; ++frame) player.tick(frame / 24.0);          // 24 fps 播 3 秒
  std::printf("72 帧：进度条收到 %d 次，统计收到 %d 次，字幕收到 %d 次\n", bar, stats, subtitle);
}
