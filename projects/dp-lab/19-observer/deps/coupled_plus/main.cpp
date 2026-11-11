// 《观察者模式》硬编码版：组装
#include "player.h"
#include "progress_bar.h"
#include "subtitle_view.h"
#include "lyrics_view.h"
#include "stats_reporter.h"
#include "danmaku_view.h"

int main() {
  ProgressBar bar;
  SubtitleView sub;
  LyricsView lyrics;
  StatsReporter stats;
  DanmakuView danmaku;
  Player player(bar, sub, lyrics, stats, danmaku);
  player.setState(PlayState::Playing);
  for (double t = 0; t <= 10; t += 5) player.tick(t);
  player.setState(PlayState::Paused);
}
