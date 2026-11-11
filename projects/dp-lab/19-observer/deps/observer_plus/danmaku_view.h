// 《观察者模式》观察者版：新加的弹幕
#pragma once
#include "player_observer.h"
class DanmakuView : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override;
};
