#pragma once
#include <sys/epoll.h>
#include <thread>
#include "http_conn.h"
#include "utils.h"

class SubReactor
{
    int epoll_fd_;
    std::jthread thread_;
    static constexpr int kMaxSubEvents = 512;
    
public:
    // 启动这个工作线程的专属事件循环
    void Start(HttpConn *users, int core_id){

        epoll_fd_ = epoll_create1(0); // 创建专属的 epoll 实例
        check(epoll_fd_);
        thread_ = std::jthread([epoll_fd_ = epoll_fd_, users, core_id](){
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            spdlog::error("Error calling pthread_setaffinity_np: {}", rc);
        }
         epoll_event events[kMaxSubEvents]; // 每个线程自己的事件数组
         spdlog::info("Jthread has started,epoll_fd_ : {}",epoll_fd_);
          while (true) {
             // 这个 epoll_wait 只监听绑定到这个线程的 client_fd,具体逻辑在主线程http_server文件中实现
           int nfds = epoll_wait(epoll_fd_, events, kMaxSubEvents, -1);
                for (int i = 0; i < nfds; ++i) {
                   int sockfd = events[i].data.fd;
                   check(sockfd);
                 if (events[i].events & EPOLLIN) {
                    if (users[sockfd].read_once()) {
                        users[sockfd].process();
                    } else {
                        users[sockfd].close_conn();
                    }
                } else if (events[i].events & EPOLLOUT) {
                    if (!users[sockfd].write_once()) {
                        users[sockfd].close_conn();
                    }
                } else /*if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))*/ {
                    users[sockfd].close_conn();
                    const uint32_t ev = events[i].events;
                    spdlog::error("Error event on socket {}: events={:#x}", sockfd, ev);
                }
               }
           } });
    }
    int get_epoll_fd() const { return epoll_fd_; }
};