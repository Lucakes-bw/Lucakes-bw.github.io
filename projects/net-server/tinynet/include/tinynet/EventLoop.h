#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "tinynet/Callbacks.h"
#include "tinynet/TimerQueue.h"

namespace tinynet {

class Channel;
class Poller;

// 事件循环：one loop per thread，每个线程最多一个 EventLoop。
// loop() 不断地等待事件、处理事件；别的线程想让它做什么，用 runInLoop / queueInLoop 把函数交给它。
class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();                            // 必须在创建它的线程中调用
    void quit();                            // 任何线程都可以调用

    // 在 loop 的线程中执行 cb：如果当前就在那个线程，立即执行；否则放进队列，唤醒 loop
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);           // 总是放进队列，在本轮事件处理完以后执行

    // 定时器：任何线程都可以调用
    TimerId runAt(Clock::time_point when, TimerCallback cb);
    TimerId runAfter(Clock::duration delay, TimerCallback cb);
    TimerId runEvery(Clock::duration interval, TimerCallback cb);
    void cancel(TimerId id);

    // 给 Channel 用
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }
    void assertInLoopThread() const;
    std::uint64_t iterations() const { return iterations_; }   // 已经循环了几轮，测试用

    static EventLoop* currentThreadLoop();  // 当前线程的 EventLoop，没有返回 nullptr

private:
    void wakeup();                          // 往 eventfd 写 8 个字节，让 epoll_wait 返回
    void handleWakeup();
    void doPendingFunctors();
    TimerId addTimer(TimerCallback cb, Clock::time_point when, Clock::duration interval);

    std::atomic<bool> quit_{false};
    bool looping_ = false;
    bool callingPendingFunctors_ = false;
    const std::thread::id threadId_;
    std::uint64_t iterations_ = 0;
    std::atomic<TimerId> nextTimerId_{1};
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timerQueue_;
    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;
    std::vector<Channel*> activeChannels_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;  // 别的线程交过来的函数，受 mutex_ 保护
};

}  // namespace tinynet
