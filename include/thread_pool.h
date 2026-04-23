#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>
#include <stdexcept>
#include <utility>

class ThreadPool {
public:
    // 构造函数：初始化 N 个线程并立即启动
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()):stop_(false){
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        // 条件变量：无任务且不停止时挂起，避免信号量过载
                        this->cond_.wait(lock, [this] { return this->stop_ || !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty()) return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task(); // 执行任务
                }
            });
        }
    }

    // 万能添加：支持 Lambda、函数指针、成员函数
    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args) /*-> std::future<typename std::invoke_result_t<F, Args...>>*/ {
        {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
        
        // 直接将函数和参数通过 std::bind 绑定，并存入任务队列
        // std::function<void()> 会利用“小对象优化”(SSO) 尽量避免堆分配
        tasks_.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
    cond_.notify_one();
    }

    ~ThreadPool() {
        { std::unique_lock<std::mutex> lock(queue_mutex_); stop_ = true; }
        cond_.notify_all();
        for (std::thread &worker : workers_) worker.join();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cond_;
    bool stop_;
};