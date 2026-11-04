#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace tinynet {

class EventLoop;

// 一个文件描述符和它关心的事件。Channel 不拥有 fd，只负责「事件来了调用哪个函数」。
// 每个 Channel 只属于一个 EventLoop，只在那个 EventLoop 的线程中使用。
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}
    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void handleEvent();                     // EventLoop 在 poll 返回后调用

    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 处理事件期间，保证 owner 还活着（TcpConnection 可能在回调中被删除）
    void tie(const std::shared_ptr<void>& owner)
    {
        tie_ = owner;
        tied_ = true;
    }

    int fd() const { return fd_; }
    std::uint32_t events() const { return events_; }
    void setRevents(std::uint32_t revents) { revents_ = revents; }   // Poller 设置实际发生的事件

    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }
    bool isWriting() const { return (events_ & kWriteEvent) != 0; }
    bool isReading() const { return (events_ & kReadEvent) != 0; }
    bool isNoneEvent() const { return events_ == kNoneEvent; }

    void remove();                          // 从 Poller 中删除，析构之前必须调用

    // Poller 用来记录这个 Channel 是否已经加入了 epoll
    int index() const { return index_; }
    void setIndex(int index) { index_ = index; }

    EventLoop* ownerLoop() const { return loop_; }

private:
    static const std::uint32_t kNoneEvent;
    static const std::uint32_t kReadEvent;
    static const std::uint32_t kWriteEvent;

    void update();
    void handleEventWithGuard();

    EventLoop* loop_;
    const int fd_;
    std::uint32_t events_ = 0;              // 关心的事件
    std::uint32_t revents_ = 0;             // 实际发生的事件
    int index_ = -1;                        // 见 Poller
    std::weak_ptr<void> tie_;
    bool tied_ = false;
    bool eventHandling_ = false;
    EventCallback readCallback_, writeCallback_, closeCallback_, errorCallback_;
};

}  // namespace tinynet
