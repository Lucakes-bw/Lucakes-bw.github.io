// 《观察者模式》观察者版：字幕
#pragma once
#include "player_observer.h"
class SubtitleView : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override;
};
