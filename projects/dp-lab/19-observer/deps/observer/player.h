// 《观察者模式》观察者版：Player 不知道有哪些模块
#pragma once
#include "player_observer.h"
#include <vector>

class Player {
public:
  void attach(PlayerObserver* o);
  void detach(PlayerObserver* o);
  void tick(double pos);
  void setState(PlayState s);
private:
  std::vector<PlayerObserver*> observers_;
  double dur_ = 10.0;
  PlayState state_ = PlayState::Stopped;
};
