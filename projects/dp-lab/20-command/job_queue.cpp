// 《命令模式》：命令队列——界面线程只把转码任务放进队列就返回，后台线程逐个取出来执行；还没开始的任务可以取消
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Job {
    int id;
    std::string name;
    std::function<void()> run;           // 命令：要做的事
};

class JobQueue {
public:
    explicit JobQueue(int workers) {
        for (int i = 0; i < workers; ++i)
            threads_.emplace_back([this, i] { loop(i); });
    }
    ~JobQueue() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    int submit(std::string name, std::function<void()> run) {
        std::lock_guard<std::mutex> lk(m_);
        int id = ++nextId_;
        pending_.push_back({id, std::move(name), std::move(run)});
        cv_.notify_one();
        return id;
    }
    bool cancel(int id) {                // 只能取消还在排队的
        std::lock_guard<std::mutex> lk(m_);
        auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Job& j) { return j.id == id; });
        if (it == pending_.end()) return false;
        pending_.erase(it);
        idle_.notify_all();
        return true;
    }
    void waitIdle() {
        std::unique_lock<std::mutex> lk(m_);
        idle_.wait(lk, [&] { return pending_.empty() && running_ == 0; });
    }
    std::vector<std::string> log() {
        std::lock_guard<std::mutex> lk(m_);
        return log_;
    }

private:
    void loop(int worker) {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || !pending_.empty(); });
                if (stop_ && pending_.empty()) return;
                job = std::move(pending_.front());
                pending_.pop_front();
                ++running_;
            }
            job.run();                   // 在锁外执行命令
            {
                std::lock_guard<std::mutex> lk(m_);
                --running_;
                log_.push_back("线程" + std::to_string(worker) + " 完成 #" + std::to_string(job.id));
            }
            idle_.notify_all();
        }
    }
    std::mutex m_;
    std::condition_variable cv_, idle_;
    std::deque<Job> pending_;
    std::vector<std::thread> threads_;
    std::vector<std::string> log_;
    int nextId_ = 0, running_ = 0;
    bool stop_ = false;
};

// 模拟转码一段视频：占满一个 CPU 核 30ms
void fakeTranscode() {
    auto end = Clock::now() + std::chrono::milliseconds(30);
    volatile unsigned x = 0;
    while (Clock::now() < end) x = x * 1664525u + 1013904223u;
}

int main() {
    for (int workers : {1, 2}) {
        JobQueue q(workers);
        std::vector<double> submitNs;
        std::vector<int> ids;
        auto t0 = Clock::now();
        for (int k = 1; k <= 8; ++k) {
            auto s = Clock::now();
            ids.push_back(q.submit("转码 bbb-10s.mp4 第 " + std::to_string(k) + " 段 → 720p", fakeTranscode));
            submitNs.push_back(std::chrono::duration<double, std::nano>(Clock::now() - s).count());
        }
        bool cancelled = q.cancel(ids.back());   // 用户点了最后一个任务的「取消」
        q.waitIdle();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::sort(submitNs.begin(), submitNs.end());
        std::printf("%d 个后台线程：提交一个任务 %.0f ns（中位数），取消 #8 %s，7 个任务共 %.0f ms\n", workers,
                    submitNs[submitNs.size() / 2], cancelled ? "成功" : "失败（已经开始）", ms);
        std::printf("   ");
        for (auto& s : q.log()) std::printf("%s；", s.c_str());
        std::printf("\n");
    }
}
