#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace tinynet {

class Buffer;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using Clock = std::chrono::steady_clock;

using TimerCallback = std::function<void()>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;          // 连接建立和断开时各调用一次
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;   // 收到数据
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;      // 输出缓冲区发送完了
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, std::size_t)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;               // 内部使用

}  // namespace tinynet
