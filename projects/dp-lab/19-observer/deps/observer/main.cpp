// 《观察者模式》观察者版：组装时决定谁订阅
#include "player.h"
#include "progress_bar.h"
#include "subtitle_view.h"
#include "lyrics_view.h"
#include "stats_reporter.h"

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
  for (double t = 0; t <= 10; t += 5) player.tick(t);
  player.setState(PlayState::Paused);
}
