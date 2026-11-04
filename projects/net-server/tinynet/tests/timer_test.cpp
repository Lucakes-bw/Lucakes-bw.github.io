#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include "tinynet/EventLoop.h"

using namespace std::chrono_literals;
using tinynet::EventLoop;

// 添加的顺序和到期的顺序不同，执行的顺序要按到期时间
TEST(Timer, RunsInOrderOfExpiration)
{
    EventLoop loop;
    std::string order;
    loop.runAfter(30ms, [&] { order += 'c'; });
    loop.runAfter(10ms, [&] { order += 'a'; });
    loop.runAfter(20ms, [&] { order += 'b'; });
    loop.runAfter(40ms, [&] { loop.quit(); });
    loop.loop();
    EXPECT_EQ(order, "abc");
}

TEST(Timer, Cancel)
{
    EventLoop loop;
    bool fired = false;
    tinynet::TimerId id = loop.runAfter(10ms, [&] { fired = true; });
    loop.cancel(id);
    loop.runAfter(30ms, [&] { loop.quit(); });
    loop.loop();
    EXPECT_FALSE(fired);
}

TEST(Timer, RunEveryUntilCancelled)
{
    EventLoop loop;
    int count = 0;
    tinynet::TimerId id = 0;
    id = loop.runEvery(5ms, [&] {
        if (++count == 3) loop.cancel(id);  // 在回调中取消自己
    });
    loop.runAfter(60ms, [&] { loop.quit(); });
    loop.loop();
    EXPECT_EQ(count, 3);
}

// 别的线程添加定时器：编号立即返回，添加在 loop 线程中进行
TEST(Timer, AddFromAnotherThread)
{
    EventLoop loop;
    std::atomic<bool> inLoopThread{false};
    std::thread t([&] {
        loop.runAfter(10ms, [&] {
            inLoopThread = loop.isInLoopThread();
            loop.quit();
        });
    });
    loop.loop();
    t.join();
    EXPECT_TRUE(inLoopThread);
}

// 回调中添加一个已经过期的定时器，不能丢，也不能让 loop 卡住
TEST(Timer, AddExpiredTimerInCallback)
{
    EventLoop loop;
    int count = 0;
    loop.runAfter(5ms, [&] {
        ++count;
        loop.runAt(tinynet::Clock::now() - 1s, [&] {
            ++count;
            loop.quit();
        });
    });
    loop.loop();
    EXPECT_EQ(count, 2);
}
