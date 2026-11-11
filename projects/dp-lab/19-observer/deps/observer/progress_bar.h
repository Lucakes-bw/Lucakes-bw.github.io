// 《观察者模式》观察者版：进度条
#pragma once
#include "player_observer.h"
class ProgressBar : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override;
};
