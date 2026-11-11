// 《观察者模式》硬编码版：统计上报
#include "stats_reporter.h"
#include <cstdio>
void StatsReporter::onTick(double) { ++ticks_; }
void StatsReporter::onState(PlayState s) { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
