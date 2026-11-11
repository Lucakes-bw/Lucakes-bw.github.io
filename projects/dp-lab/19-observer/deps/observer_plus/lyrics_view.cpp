// 《观察者模式》观察者版：歌词
#include "lyrics_view.h"
#include <cstdio>
void LyricsView::onProgress(double pos, double) { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
