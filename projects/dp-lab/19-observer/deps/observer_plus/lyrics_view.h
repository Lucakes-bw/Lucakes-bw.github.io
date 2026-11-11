// 《观察者模式》观察者版：歌词
#pragma once
#include "player_observer.h"
class LyricsView : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override;
};
