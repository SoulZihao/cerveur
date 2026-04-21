#pragma once
#include "http_conn.h"
#include "thread_pool.h"

class HttpServer {
public:
    // 1. 单例模式入口
    static HttpServer& getInstance() {
        static HttpServer instance;
        return instance;
    }

    // 禁止拷贝和赋值
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // 2. 替代原本的 init_server，负责硬件资源准备
    bool Init(int port);

    // 4. 事件循环
    void EventLoop(ThreadPool & pool);

private:
    // 私有构造函数
    HttpServer() : listen_fd_(-1), port_(0), users_(nullptr) {}
    ~HttpServer() {
        if (users_) delete[] users_;
        if (listen_fd_ != -1) close(listen_fd_);
    }

private:
    int listen_fd_;
    int port_;
    int epollfd_;
    HttpConn* users_; // 管理所有 socket 的档案
    static const int MAX_FD = 65536;
	static const int MAX_EVENT_NUMBER = 1024;
};

