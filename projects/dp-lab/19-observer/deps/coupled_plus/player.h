// 《观察者模式》硬编码版：Player 直接认识每一个模块
#pragma once
#include "play_state.h"
class ProgressBar;
class SubtitleView;
class LyricsView;
class StatsReporter;
class DanmakuView;

class Player {
public:
  Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats,
         DanmakuView& danmaku);
  void tick(double pos);           // 播放推进到 pos 秒
  void setState(PlayState s);
private:
  ProgressBar& bar_;
  SubtitleView& sub_;
  LyricsView& lyrics_;
  StatsReporter& stats_;
  DanmakuView& danmaku_;
  double dur_ = 10.0;
  PlayState state_ = PlayState::Stopped;
};
