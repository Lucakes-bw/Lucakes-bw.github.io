// 《观察者模式》硬编码版：每加一个模块，这里都要改
#include "player.h"
#include "progress_bar.h"
#include "subtitle_view.h"
#include "lyrics_view.h"
#include "stats_reporter.h"
#include "danmaku_view.h"

Player::Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats,
               DanmakuView& danmaku)
    : bar_(bar), sub_(sub), lyrics_(lyrics), stats_(stats), danmaku_(danmaku) {}

void Player::tick(double pos) {
  bar_.update(pos, dur_);
  sub_.seek(pos);
  lyrics_.highlight(pos);
  stats_.onTick(pos);
  danmaku_.scrollTo(pos);
}

void Player::setState(PlayState s) {
  state_ = s;
  stats_.onState(s);
}
