#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tinynet {

class EventLoop;

// 一个线程，线程中运行一个 EventLoop
class EventLoopThread {
public:
    EventLoopThread() = default;
    ~EventLoopThread();                     // 让 loop 退出，等待线程结束
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* start();                     // 启动线程，等 EventLoop 创建好以后返回它

private:
    void threadFunc();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    EventLoop* loop_ = nullptr;             // 受 mutex_ 保护
};

// 若干个 IO 线程，每个线程一个 EventLoop。新连接按轮转的方式分给它们
class EventLoopThreadPool {
public:
    explicit EventLoopThreadPool(EventLoop* baseLoop) : baseLoop_(baseLoop) {}
    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void setThreadNum(int n) { numThreads_ = n; }
    void start();
    EventLoop* getNextLoop();               // 没有 IO 线程时，返回 baseLoop

private:
    EventLoop* baseLoop_;
    int numThreads_ = 0;
    std::size_t next_ = 0;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

}  // namespace tinynet
