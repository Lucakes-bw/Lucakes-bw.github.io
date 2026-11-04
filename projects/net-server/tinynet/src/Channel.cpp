#include "tinynet/Channel.h"

#include <sys/epoll.h>

#include <cassert>

#include "tinynet/EventLoop.h"

namespace tinynet {

const std::uint32_t Channel::kNoneEvent = 0;
const std::uint32_t Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const std::uint32_t Channel::kWriteEvent = EPOLLOUT;

// 析构时不访问 loop_：TcpConnection 的最后一个 shared_ptr 可能在别的线程中释放，
// 那时它的 EventLoop 甚至可能已经不存在了（服务器退出时）。它早已从 Poller 中删除，不需要再做什么
Channel::~Channel() { assert(!eventHandling_); }

void Channel::update() { loop_->updateChannel(this); }

void Channel::remove() { loop_->removeChannel(this); }

void Channel::handleEvent()
{
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();      // 处理事件期间，owner 不会被析构
        if (guard) handleEventWithGuard();
    } else {
        handleEventWithGuard();
    }
}

void Channel::handleEventWithGuard()
{
    eventHandling_ = true;
    // 对方关闭了连接，并且没有数据可读了
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
    eventHandling_ = false;
}

}  // namespace tinynet
