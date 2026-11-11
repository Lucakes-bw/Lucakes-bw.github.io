// 《观察者模式》观察者版：字幕
#include "subtitle_view.h"
#include <cstdio>
void SubtitleView::onProgress(double pos, double) { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
