// 《观察者模式》观察者版：统计上报
#pragma once
#include "player_observer.h"
class StatsReporter : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override;
  void onStateChanged(PlayState s) override;
private:
  int ticks_ = 0;
};
