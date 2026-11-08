// 只用到求平均值，却包含了整个 ReportService
#include "report_all.h"
double weeklyAverage(const std::vector<double>& v) { return ReportService().average(v); }
