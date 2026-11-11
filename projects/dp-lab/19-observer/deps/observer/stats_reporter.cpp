// 《观察者模式》观察者版：统计上报
#include "stats_reporter.h"
#include <cstdio>
void StatsReporter::onProgress(double, double) { ++ticks_; }
void StatsReporter::onStateChanged(PlayState s) { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
