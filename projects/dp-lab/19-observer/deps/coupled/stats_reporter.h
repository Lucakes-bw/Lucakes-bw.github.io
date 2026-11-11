// 《观察者模式》硬编码版：统计上报
#pragma once
#include "play_state.h"
class StatsReporter {
public:
  void onTick(double pos);
  void onState(PlayState s);
private:
  int ticks_ = 0;
};
