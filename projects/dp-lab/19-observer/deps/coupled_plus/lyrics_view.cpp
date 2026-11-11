// 《观察者模式》硬编码版：歌词
#include "lyrics_view.h"
#include <cstdio>
void LyricsView::highlight(double pos) { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
