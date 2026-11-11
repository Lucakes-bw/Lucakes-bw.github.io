// 《观察者模式》观察者版：加模块不用改这里
#include "player.h"
#include <algorithm>

void Player::attach(PlayerObserver* o) { observers_.push_back(o); }
void Player::detach(PlayerObserver* o) {
  auto it = std::find(observers_.begin(), observers_.end(), o);
  if (it != observers_.end()) observers_.erase(it);
}
void Player::tick(double pos) {
  for (auto* o : observers_) o->onProgress(pos, dur_);
}
void Player::setState(PlayState s) {
  state_ = s;
  for (auto* o : observers_) o->onStateChanged(s);
}
