// 只负责统计
#pragma once
#include <numeric>
#include <vector>

inline double average(const std::vector<double>& v) {
    return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}
