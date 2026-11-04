#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <chrono>
#include <thread>
#include <vector>

#include "tinynet/EventLoop.h"
#include "tinynet/EventLoopThreadPool.h"

using namespace std::chrono_literals;
using tinynet::EventLoop;

TEST(EventLoop, CurrentThreadLoop)
{
    EXPECT_EQ(EventLoop::currentThreadLoop(), nullptr);
    {
        EventLoop loop;
        EXPECT_EQ(EventLoop::currentThreadLoop(), &loop);
        EXPECT_TRUE(loop.isInLoopThread());
    }
    EXPECT_EQ(EventLoop::currentThreadLoop(), nullptr);
}

// 别的线程交给 loop 的函数，在 loop 的线程中执行，并且能把阻塞在 epoll_wait 中的 loop 唤醒
TEST(EventLoop, RunInLoopFromAnotherThreadWakesUp)
{
    EventLoop loop;
    std::atomic<int> ran{0};
    std::thread t([&] {
        std::this_thread::sleep_for(20ms);  // 这时 loop 正阻塞在 epoll_wait 中（超时是 10 秒）
        for (int i = 0; i < 100; ++i) {
            loop.runInLoop([&] {
                EXPECT_TRUE(loop.isInLoopThread());
                ++ran;
            });
        }
        loop.runInLoop([&] { loop.quit(); });
    });
    auto start = std::chrono::steady_clock::now();
    loop.loop();
    t.join();
    EXPECT_EQ(ran, 100);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);   // 没有等到 epoll_wait 超时
}

// queueInLoop 在执行 pending 函数期间被调用：新的函数要在下一轮执行，loop 不能睡着
TEST(EventLoop, QueueInLoopDuringPendingFunctors)
{
    EventLoop loop;
    std::vector<int> order;
    loop.queueInLoop([&] {
        order.push_back(1);
        loop.queueInLoop([&] {
            order.push_back(2);
            loop.quit();
        });
    });
    auto start = std::chrono::steady_clock::now();
    loop.loop();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST(EventLoop, QuitFromAnotherThread)
{
    EventLoop loop;
    std::thread t([&] {
        std::this_thread::sleep_for(20ms);
        loop.quit();
    });
    auto start = std::chrono::steady_clock::now();
    loop.loop();
    t.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

// 线程池：每个 IO 线程一个 EventLoop，getNextLoop 轮流返回它们
TEST(EventLoopThreadPool, RoundRobin)
{
    EventLoop base;
    tinynet::EventLoopThreadPool pool(&base);
    pool.setThreadNum(3);
    pool.start();
    EventLoop* a = pool.getNextLoop();
    EventLoop* b = pool.getNextLoop();
    EventLoop* c = pool.getNextLoop();
    EXPECT_NE(a, &base);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_EQ(pool.getNextLoop(), a);

    std::atomic<bool> ok{false};
    std::promise<void> done;
    a->runInLoop([&] {
        ok = a->isInLoopThread() && !base.isInLoopThread();
        done.set_value();
    });
    done.get_future().wait();
    EXPECT_TRUE(ok);
}

TEST(EventLoopThreadPool, NoThreadsUsesBaseLoop)
{
    EventLoop base;
    tinynet::EventLoopThreadPool pool(&base);
    pool.start();
    EXPECT_EQ(pool.getNextLoop(), &base);
}
