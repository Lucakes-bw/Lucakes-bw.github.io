// 《观察者模式》硬编码版：字幕
#include "subtitle_view.h"
#include <cstdio>
void SubtitleView::seek(double pos) { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
