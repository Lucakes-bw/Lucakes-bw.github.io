#include "tinynet/EventLoopThreadPool.h"

#include <cassert>

#include "tinynet/EventLoop.h"

namespace tinynet {

EventLoopThread::~EventLoopThread()
{
    EventLoop* loop = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop = loop_;
    }
    if (loop) loop->quit();
    if (thread_.joinable()) thread_.join();
}

EventLoop* EventLoopThread::start()
{
    thread_ = std::thread([this] { threadFunc(); });
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });   // 等新线程把 EventLoop 创建好
    return loop_;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;                         // EventLoop 在新线程的栈上，生命期和线程一样长
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    cond_.notify_one();
    loop.loop();
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}

void EventLoopThreadPool::start()
{
    baseLoop_->assertInLoopThread();
    for (int i = 0; i < numThreads_; ++i) {
        threads_.push_back(std::make_unique<EventLoopThread>());
        loops_.push_back(threads_.back()->start());
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    baseLoop_->assertInLoopThread();
    if (loops_.empty()) return baseLoop_;
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}

}  // namespace tinynet
