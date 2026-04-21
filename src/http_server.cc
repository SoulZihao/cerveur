#include <sys/socket.h>
#include <netinet/in.h>
#include "utils.h"
#include "http_server.h"

bool HttpServer::Init(int port) {
	port_ = port;
	listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	check(listen_fd_);

	// 设置端口复用，防止重启服务器时出现 Address already in use
	int reuse = 1;
	check(setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port_);

	check(bind(listen_fd_, (struct sockaddr*)&address, sizeof(address)));

	check(listen(listen_fd_, SOMAXCONN));
	this->epollfd_ = epoll_create1(0);
    HttpConn::epollfd_ = this->epollfd_;

	// 添加监听端socket
    epoll_event event;
    event.data.fd = listen_fd_;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    check(epoll_ctl(epollfd_, EPOLL_CTL_ADD, listen_fd_, &event));
    // HttpConn::addfd(epollfd_,listen_fd_,false);
    check(HttpConn::set_nonblocking(listen_fd_));
    // 3. 初始化连接池
    users_ = new HttpConn[MAX_FD];
	spdlog::debug("Listening socket fd: {}, epoll fd: {}", listen_fd_, epollfd_);
	return true;
}
// 4. 事件循环
void HttpServer::EventLoop(ThreadPool & pool){
    epoll_event events[MAX_EVENT_NUMBER];
    spdlog::debug("Event loop started");
    while (true) {
        int nfds = epoll_wait(epollfd_, events, MAX_EVENT_NUMBER, -1);
        spdlog::trace("epoll_wait returned {} events", nfds);
        for (int i = 0; i < nfds; ++i) {
            int sockfd = events[i].data.fd;
            check(sockfd);
            // 情况 1：新连接 (New Connection)
            if (sockfd == listen_fd_) {
                spdlog::debug("New connection event on listen socket {}", listen_fd_);
                pool.enqueue([users_ = users_,listen_fd_ = listen_fd_]{
                    int accept_count = 0;
                    while (true) { // ET 模式下 accept 也要循环读完
                        int client_fd = accept(listen_fd_, nullptr, nullptr);
                        if(client_fd == -1 && errno == EAGAIN)break;
                        check(client_fd);
                        spdlog::info("Accepted new connection, fd: {}", client_fd);
                        users_[client_fd].Init(client_fd);
                        accept_count++;
                    }
                    spdlog::debug("Accepted {} connections in this batch", accept_count);
                });
            }else if (events[i].events & EPOLLIN) {// 情况 2：客户端发来数据 (Read)
                spdlog::debug("Read event on socket {}", sockfd);
                pool.enqueue([users_ = users_,sockfd] {
                    // 这个 Lambda 就在工作线程运行了
                    if (users_[sockfd].read_once()) {
                        users_[sockfd].process();
                    } else {
                        spdlog::error("Because of read failed, close connection {}", sockfd);
                        users_[sockfd].close_conn();
                    }
                });
                // if (users_[sockfd].read_once()) {
                //     // 读完后立刻进行逻辑解析
                //     users_[sockfd].process();
                //     // 解析完后，我们要写数据（发送文件），所以改为监听写事件
                //     // modfd(epollfd_, sockfd, EPOLLOUT);
                // } else {
                //     // 读取失败（如对端关闭），关闭连接
                //     users_[sockfd].close_conn();
                // }
            }
            
            // 情况 3：可以向客户端发数据了 (Write)
            else if (events[i].events & EPOLLOUT) {
                spdlog::debug("Write event on socket {}", sockfd);
                pool.enqueue([users_ = users_, sockfd] {
                    if (!users_[sockfd].write_once()) {
                        spdlog::error("Because of write failed, close connection {}", sockfd);
                        users_[sockfd].close_conn();
                    }
                });
                // 如果 write_once 返回 true，说明要么发完了，要么还在等待缓冲区，
                // 内部已经处理好了 modfd 或 keep-alive 的逻辑。
            }
            
            // 情况 4：错误处理
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                const uint32_t ev = events[i].events;
                spdlog::error("Error event on socket {}: events={:#x}", sockfd, ev);
                users_[sockfd].close_conn();
            }
        }
    }
}
