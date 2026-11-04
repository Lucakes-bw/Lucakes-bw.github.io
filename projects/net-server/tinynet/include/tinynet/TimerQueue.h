#pragma once

#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

#include "tinynet/Callbacks.h"
#include "tinynet/Channel.h"

namespace tinynet {

class EventLoop;

using TimerId = std::uint64_t;              // 定时器的编号，用来取消，从 1 开始，0 表示无效

// 定时器队列：所有定时器放在一个按到期时间排列的最小堆中，只用一个 timerfd，
// 它的到期时间总是堆顶（最早到期的那个）的时间。到期时 timerfd 变成可读，和其他 fd 一样由 epoll 通知。
class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 只能在 loop 的线程中调用。编号由 EventLoop 分配；interval 为 0 表示只执行一次
    void add(TimerId id, TimerCallback cb, Clock::time_point when, Clock::duration interval);
    void cancel(TimerId id);
    std::size_t size() const { return heap_.size(); }   // 堆中的元素个数（包括已经取消、还没弹出的）

private:
    struct Timer {
        Clock::time_point when;
        Clock::duration interval;
        TimerId id;
        TimerCallback callback;
    };
    struct Later {                          // 让 priority_queue 成为最小堆：到期早的在堆顶
        bool operator()(const Timer& a, const Timer& b) const
        {
            return a.when != b.when ? a.when > b.when : a.id > b.id;
        }
    };

    void handleRead();                      // timerfd 可读：执行所有到期的定时器
    void resetTimerfd();                    // 把 timerfd 设为堆顶的到期时间

    EventLoop* loop_;
    const int timerfd_;
    Channel timerfdChannel_;
    std::priority_queue<Timer, std::vector<Timer>, Later> heap_;
    std::unordered_set<TimerId> cancelled_;   // 已经取消、但还在堆中的定时器（懒删除）
};

}  // namespace tinynet
