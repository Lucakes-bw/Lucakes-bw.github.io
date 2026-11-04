#include "tinynet/TimerQueue.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cstdint>

#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"

namespace tinynet {

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      timerfdChannel_(loop, timerfd_)
{
    if (timerfd_ < 0) LOG_SYSFATAL("timerfd_create");
    timerfdChannel_.setReadCallback([this] { handleRead(); });
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
}

void TimerQueue::add(TimerId id, TimerCallback cb, Clock::time_point when, Clock::duration interval)
{
    loop_->assertInLoopThread();
    bool earliest = heap_.empty() || when < heap_.top().when;
    heap_.push(Timer{when, interval, id, std::move(cb)});
    if (earliest) resetTimerfd();           // 新的定时器比原来最早的还早，timerfd 要提前
}

void TimerQueue::cancel(TimerId id)
{
    loop_->assertInLoopThread();
    // 堆不支持删除中间的元素：只记下来，等它到达堆顶时再丢掉
    if (id > 0) cancelled_.insert(id);
}

void TimerQueue::handleRead()
{
    std::uint64_t expirations = 0;
    if (::read(timerfd_, &expirations, sizeof expirations) != sizeof expirations) {
        LOG_SYSERR("read timerfd");
    }
    const auto now = Clock::now();
    std::vector<Timer> expired;             // 先全部取出来再执行：回调中可能会添加新的定时器
    while (!heap_.empty() && heap_.top().when <= now) {
        expired.push_back(heap_.top());
        heap_.pop();
    }
    for (Timer& t : expired) {
        if (cancelled_.erase(t.id) > 0) continue;       // 已经取消了
        t.callback();
        if (t.interval > Clock::duration::zero()) {     // 重复的定时器：算出下一次的时间，放回堆中
            if (cancelled_.erase(t.id) > 0) continue;   // 在回调中取消了自己
            t.when = now + t.interval;
            heap_.push(std::move(t));
        }
    }
    resetTimerfd();
}

void TimerQueue::resetTimerfd()
{
    while (!heap_.empty() && cancelled_.count(heap_.top().id) > 0) {   // 堆顶已经取消了，顺便丢掉
        cancelled_.erase(heap_.top().id);
        heap_.pop();
    }
    itimerspec spec{};                      // 全部为 0 表示停止 timerfd
    if (!heap_.empty()) {
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(heap_.top().when - Clock::now());
        if (delta < std::chrono::microseconds(100)) delta = std::chrono::microseconds(100);   // 已经过期的，尽快触发
        spec.it_value.tv_sec = static_cast<time_t>(delta.count() / 1000000000);
        spec.it_value.tv_nsec = static_cast<long>(delta.count() % 1000000000);
    }
    if (::timerfd_settime(timerfd_, 0, &spec, nullptr) < 0) LOG_SYSERR("timerfd_settime");
}

}  // namespace tinynet
