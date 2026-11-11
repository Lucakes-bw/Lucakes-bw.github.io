// 《观察者模式》：硬编码版——播放器自己挨个调用进度条、字幕、歌词、统计上报
#include <cstdio>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class ProgressBar {
public:
  void update(double pos, double dur) { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); }
};
class SubtitleView {
public:
  void seek(double pos) { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
};
class LyricsView {
public:
  void highlight(double pos) { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
};
class StatsReporter {
public:
  void onTick(double) { ++ticks_; }
  void onState(PlayState s) { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
private:
  int ticks_ = 0;
};

class Player {
public:
  Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats)
      : bar_(bar), sub_(sub), lyrics_(lyrics), stats_(stats) {}

  void tick(double pos) {          // 播放推进到 pos 秒：要更新的模块全写死在这里
    bar_.update(pos, dur_);
    sub_.seek(pos);
    lyrics_.highlight(pos);
    stats_.onTick(pos);
  }
  void setState(PlayState s) {
    state_ = s;
    stats_.onState(s);
  }

private:
  ProgressBar& bar_;
  SubtitleView& sub_;
  LyricsView& lyrics_;
  StatsReporter& stats_;
  double dur_ = 10.0;              // bbb-10s.mp4 时长 10 秒
  PlayState state_ = PlayState::Stopped;
};

int main() {
  ProgressBar bar;
  SubtitleView sub;
  LyricsView lyrics;
  StatsReporter stats;
  Player player(bar, sub, lyrics, stats);
  player.setState(PlayState::Playing);
  for (double t = 0; t <= 10; t += 5) player.tick(t);
  player.setState(PlayState::Paused);
}
