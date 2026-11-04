#include "tinynet/Poller.h"

#include <unistd.h>

#include <cassert>

#include "tinynet/Channel.h"
#include "tinynet/EventLoop.h"
#include "tinynet/Log.h"

namespace tinynet {

namespace {
// Channel::index() 的三种取值
constexpr int kNew = -1;        // 从来没有加入过 epoll
constexpr int kAdded = 1;       // 在 epoll 中
constexpr int kDeleted = 2;     // 不关心任何事件，暂时从 epoll 中删掉了，但还在 channels_ 中
}  // namespace

Poller::Poller(EventLoop* loop) : loop_(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(16)
{
    if (epollfd_ < 0) LOG_SYSFATAL("epoll_create1");
}

Poller::~Poller() { ::close(epollfd_); }

void Poller::poll(int timeoutMs, std::vector<Channel*>* active)
{
    int n = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    if (n < 0) {
        if (errno != EINTR) LOG_SYSERR("epoll_wait");
        return;
    }
    for (int i = 0; i < n; ++i) {
        auto* channel = static_cast<Channel*>(events_[static_cast<std::size_t>(i)].data.ptr);
        channel->setRevents(events_[static_cast<std::size_t>(i)].events);
        active->push_back(channel);
    }
    if (static_cast<std::size_t>(n) == events_.size()) events_.resize(events_.size() * 2);   // 装满了，下次多准备一些
}

void Poller::updateChannel(Channel* channel)
{
    loop_->assertInLoopThread();
    const int index = channel->index();
    if (index == kNew || index == kDeleted) {
        if (index == kNew) channels_[channel->fd()] = channel;
        channel->setIndex(kAdded);
        update(EPOLL_CTL_ADD, channel);
    } else if (channel->isNoneEvent()) {
        update(EPOLL_CTL_DEL, channel);                 // 不关心任何事件了，从 epoll 中删掉
        channel->setIndex(kDeleted);
    } else {
        update(EPOLL_CTL_MOD, channel);
    }
}

void Poller::removeChannel(Channel* channel)
{
    loop_->assertInLoopThread();
    assert(channel->isNoneEvent());
    channels_.erase(channel->fd());
    if (channel->index() == kAdded) update(EPOLL_CTL_DEL, channel);
    channel->setIndex(kNew);
}

bool Poller::hasChannel(const Channel* channel) const
{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

void Poller::update(int operation, Channel* channel)
{
    epoll_event ev{};
    ev.events = channel->events();
    ev.data.ptr = channel;
    if (::epoll_ctl(epollfd_, operation, channel->fd(), &ev) < 0) {
        if (operation == EPOLL_CTL_DEL) LOG_SYSERR("epoll_ctl DEL fd=%d", channel->fd());
        else LOG_SYSFATAL("epoll_ctl fd=%d", channel->fd());
    }
}

}  // namespace tinynet
