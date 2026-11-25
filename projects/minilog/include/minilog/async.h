#pragma once

#include <cstddef>
#include <cstdint>

namespace minilog {

enum class OverflowPolicy : uint8_t {
  Block = 0,           // 队列满时阻塞等待后台消费，确保零丢失
  DropNewest = 1,      // 队列满时丢弃新日志并计数，保证前端亚微秒延迟
  OverwriteOldest = 2  // 队列满时覆写最旧未消费槽位，保留最新现场
};

struct AsyncConfig {
  bool enabled            = false;
  std::size_t queueCapacity = 4096;
  OverflowPolicy overflow = OverflowPolicy::Block;
};

}  // namespace minilog
