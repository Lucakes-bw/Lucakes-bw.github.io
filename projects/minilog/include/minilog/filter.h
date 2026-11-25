#pragma once

#include "minilog/level.h"
#include "minilog/record.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace minilog {

// 过滤器签名：返回 true 放行（允许就地脱敏改写），返回 false 拦截丢弃
using FilterFn = std::function<bool(LogRecord&)>;

class FilterPipeline {
 public:
  void add(FilterFn fn);
  bool apply(LogRecord& rec) const;
  std::size_t size() const noexcept { return filters_.size(); }
  bool empty() const noexcept { return filters_.empty(); }

 private:
  std::vector<FilterFn> filters_;
};

// 1. 级别过滤
FilterFn makeLevelFilter(Level minLevel);

// 2. 模块名过滤（allowList=true 白名单；allowList=false 黑名单）
FilterFn makeModuleFilter(std::vector<std::string> modules, bool allowList);

// 3. 确定性时间窗限流（基于 LogRecord::timestampMs 计数）
FilterFn makeRateLimitFilter(uint32_t maxPerWindow, uint64_t windowMs);

// 4. 敏感信息脱敏（11 位手机号脱敏为 138****5678，token=<值> 脱敏为 token=***）
FilterFn makeRedactFilter();

}  // namespace minilog
