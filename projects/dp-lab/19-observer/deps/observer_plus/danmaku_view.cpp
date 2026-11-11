// 《观察者模式》观察者版：新加的弹幕
#include "danmaku_view.h"
#include <cstdio>
void DanmakuView::onProgress(double pos, double) { std::printf("  [弹幕]   滚到 %.1f s\n", pos); }
