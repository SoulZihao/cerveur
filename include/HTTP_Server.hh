#pragma once
#include "HTTP_conn.hh"

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
    bool init(int port);

    // 4. 事件循环
    void event_loop();

private:
    // 私有构造函数
    HttpServer() : m_listenfd(-1), m_port(0), m_users(nullptr) {}
    ~HttpServer() {
        if (m_users) delete[] m_users;
        if (m_listenfd != -1) close(m_listenfd);
    }

private:
    int m_listenfd;
    int m_port;
    int m_epollfd;
    http_conn* m_users; // 管理所有 socket 的档案
    static const int MAX_FD = 65536;
	static const int MAX_EVENT_NUMBER = 10;
};

