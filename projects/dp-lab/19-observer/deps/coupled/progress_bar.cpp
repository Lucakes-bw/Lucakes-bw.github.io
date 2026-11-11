// 《观察者模式》硬编码版：进度条
#include "progress_bar.h"
#include <cstdio>
void ProgressBar::update(double pos, double dur) { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); }
