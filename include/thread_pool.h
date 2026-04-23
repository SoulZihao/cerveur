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
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
        using return_type = typename std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            // restore the types of incoming parameter,currently,f and args are lvalue.
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks_.emplace([task]() { (*task)(); });
        }
        cond_.notify_one();
        return res;
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