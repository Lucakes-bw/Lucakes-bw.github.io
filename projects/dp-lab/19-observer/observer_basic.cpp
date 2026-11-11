// 《观察者模式》：Player 只认识 PlayerObserver 接口，各个模块自己订阅、自己取消
#include <algorithm>
#include <cstdio>
#include <vector>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class PlayerObserver {                       // 观察者接口：想知道播放器变化的模块都实现它
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double /*pos*/, double /*dur*/) {}
  virtual void onStateChanged(PlayState) {}
};

class Player {                               // 被观察者（Subject）
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it != observers_.end()) observers_.erase(it);
  }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos, dur_);
  }
  void setState(PlayState s) {
    state_ = s;
    for (auto* o : observers_) o->onStateChanged(s);
  }

private:
  std::vector<PlayerObserver*> observers_;
  double dur_ = 10.0;
  PlayState state_ = PlayState::Stopped;
};

class ProgressBar : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); }
};
class SubtitleView : public PlayerObserver {
public:
  void onProgress(double pos, double) override { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
};
class LyricsView : public PlayerObserver {
public:
  void onProgress(double pos, double) override { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
};
class StatsReporter : public PlayerObserver {
public:
  void onProgress(double, double) override { ++ticks_; }
  void onStateChanged(PlayState s) override { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
private:
  int ticks_ = 0;
};

int main() {
  ProgressBar bar;
  SubtitleView sub;
  LyricsView lyrics;
  StatsReporter stats;
  Player player;
  player.attach(&stats);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);

  player.setState(PlayState::Playing);
  player.tick(0);
  std::puts("-- 切到视频模式，歌词面板取消订阅");
  player.detach(&lyrics);
  player.tick(5);
  player.setState(PlayState::Paused);
}
