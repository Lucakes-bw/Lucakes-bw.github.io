// 《观察者模式》观察者版：Player 只认识这个接口
#pragma once
#include "play_state.h"
class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double /*pos*/, double /*dur*/) {}
  virtual void onStateChanged(PlayState) {}
};
