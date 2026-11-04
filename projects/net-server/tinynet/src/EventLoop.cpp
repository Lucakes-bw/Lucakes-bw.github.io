#include "tinynet/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>

#include "tinynet/Channel.h"
#include "tinynet/Log.h"
#include "tinynet/Poller.h"

namespace tinynet {

namespace {
thread_local EventLoop* t_loopInThisThread = nullptr;
constexpr int kPollTimeoutMs = 10000;
}  // namespace

EventLoop* EventLoop::currentThreadLoop() { return t_loopInThisThread; }

EventLoop::EventLoop()
    : threadId_(std::this_thread::get_id()),
      poller_(std::make_unique<Poller>(this)),
      wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (wakeupFd_ < 0) LOG_SYSFATAL("eventfd");
    if (t_loopInThisThread != nullptr) LOG_FATAL("一个线程中只能有一个 EventLoop");
    t_loopInThisThread = this;
    timerQueue_ = std::make_unique<TimerQueue>(this);
    wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
    wakeupChannel_->setReadCallback([this] { handleWakeup(); });
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    // 别的线程可能正在 quit() 或 queueInLoop() 中往 wakeupFd_ 写数据（它们写的时候持有 mutex_）。
    // 先拿到锁，保证没有人在写了，再关闭 wakeupFd_
    std::lock_guard<std::mutex> lock(mutex_);
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    timerQueue_.reset();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    assertInLoopThread();
    assert(!looping_);
    looping_ = true;
    while (!quit_) {
        activeChannels_.clear();
        poller_->poll(kPollTimeoutMs, &activeChannels_);
        ++iterations_;
        for (Channel* channel : activeChannels_) channel->handleEvent();   // 1. 处理 IO 事件（包括定时器）
        doPendingFunctors();                                               // 2. 执行别的线程交过来的函数
    }
    doPendingFunctors();                    // quit 之前交过来的函数也要执行，比如清理连接
    looping_ = false;
}

void EventLoop::quit()
{
    // 设置 quit_ 和唤醒必须在锁中一起完成：否则 loop 线程可能在两步之间看到 quit_，
    // 退出循环、析构 EventLoop、关闭 wakeupFd_，这边再写就写到了一个已经关闭（甚至被重新使用）的 fd
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    if (!isInLoopThread()) wakeup();        // loop 可能正阻塞在 epoll_wait 中
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) cb();
    else queueInLoop(std::move(cb));
}

void EventLoop::queueInLoop(Functor cb)
{
    std::lock_guard<std::mutex> lock(mutex_);   // 唤醒也在锁中，原因见 quit()
    pendingFunctors_.push_back(std::move(cb));
    // 别的线程：loop 可能在 epoll_wait 中睡着，要叫醒它。
    // 本线程、但正在执行 pendingFunctors：新加的要等下一轮，也要叫醒，否则下一轮的 epoll_wait 会一直等下去。
    // 本线程、loop 还没有开始：第一次 epoll_wait 之前也要有事件，否则要等到超时才执行。
    if (!isInLoopThread() || callingPendingFunctors_ || !looping_) wakeup();
}

// 定时器的编号在调用者的线程中分配（原子变量），立即返回；真正的添加在 loop 的线程中进行
TimerId EventLoop::addTimer(TimerCallback cb, Clock::time_point when, Clock::duration interval)
{
    TimerId id = nextTimerId_++;
    runInLoop([this, id, cb = std::move(cb), when, interval]() mutable {
        timerQueue_->add(id, std::move(cb), when, interval);
    });
    return id;
}

TimerId EventLoop::runAt(Clock::time_point when, TimerCallback cb)
{
    return addTimer(std::move(cb), when, Clock::duration::zero());
}

TimerId EventLoop::runAfter(Clock::duration delay, TimerCallback cb) { return runAt(Clock::now() + delay, std::move(cb)); }

TimerId EventLoop::runEvery(Clock::duration interval, TimerCallback cb)
{
    return addTimer(std::move(cb), Clock::now() + interval, interval);
}

void EventLoop::cancel(TimerId id)
{
    runInLoop([this, id] { timerQueue_->cancel(id); });
}

void EventLoop::updateChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
    assertInLoopThread();
    return poller_->hasChannel(channel);
}

void EventLoop::assertInLoopThread() const
{
    if (!isInLoopThread()) LOG_FATAL("EventLoop 只能在创建它的线程中使用");
}

void EventLoop::wakeup()
{
    std::uint64_t one = 1;
    if (::write(wakeupFd_, &one, sizeof one) != sizeof one) LOG_SYSERR("write eventfd");
}

void EventLoop::handleWakeup()
{
    std::uint64_t value = 0;
    if (::read(wakeupFd_, &value, sizeof value) != sizeof value) LOG_SYSERR("read eventfd");
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);    // 交换出来再执行：执行时不持有锁，函数中可以再调用 queueInLoop
    }
    for (const Functor& f : functors) f();
    callingPendingFunctors_ = false;
}

}  // namespace tinynet
