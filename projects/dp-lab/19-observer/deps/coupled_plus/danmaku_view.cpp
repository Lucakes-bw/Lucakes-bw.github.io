// 《观察者模式》硬编码版：新加的弹幕
#include "danmaku_view.h"
#include <cstdio>
void DanmakuView::scrollTo(double pos) { std::printf("  [弹幕]   滚到 %.1f s\n", pos); }
