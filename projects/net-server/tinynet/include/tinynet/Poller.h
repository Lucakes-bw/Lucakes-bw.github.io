#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <vector>

namespace tinynet {

class Channel;
class EventLoop;

// 对 epoll 的封装：哪些 fd 关心哪些事件，等待事件发生
class Poller {
public:
    explicit Poller(EventLoop* loop);
    ~Poller();
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    // 等待事件，最多 timeoutMs 毫秒，把发生了事件的 Channel 放进 active
    void poll(int timeoutMs, std::vector<Channel*>* active);
    void updateChannel(Channel* channel);   // 新增或修改关心的事件
    void removeChannel(Channel* channel);
    bool hasChannel(const Channel* channel) const;

private:
    void update(int operation, Channel* channel);

    EventLoop* loop_;
    int epollfd_;
    std::vector<epoll_event> events_;                   // epoll_wait 的结果，不够用时翻倍
    std::unordered_map<int, Channel*> channels_;        // fd -> Channel
};

}  // namespace tinynet
