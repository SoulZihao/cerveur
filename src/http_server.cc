#include <sys/socket.h>
#include <netinet/in.h>
#include <ranges>
#include "utils.h"
#include "http_server.h"
#include <netinet/tcp.h>
int hc = std::thread::hardware_concurrency();
const unsigned int HttpServer::kThreadNum = (hc > 4) ? (hc - 4) : 1; 

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
    // HttpConn::epollfd_ = this->epollfd_;

	// 添加监听端socket
    epoll_event event;
    event.data.fd = listen_fd_;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    check(HttpConn::set_nonblocking(listen_fd_));
    check(epoll_ctl(epollfd_, EPOLL_CTL_ADD, listen_fd_, &event));
    // HttpConn::addfd(epollfd_,listen_fd_,false);
    // 3. 初始化连接池
    users_ = new HttpConn[MAX_FD];
    for (int i = 0; i < kThreadNum; ++i) {
        sub_reactors_.emplace_back(std::make_unique<SubReactor>());
    }
    for (auto [i, sub_reactor] : std::views::enumerate(sub_reactors_)) {
        sub_reactor->Start(users_,i);
    }
	spdlog::info("Listening socket fd: {}, epoll fd: {}", listen_fd_, epollfd_);
	return true;
}
// 4. 事件循环
void HttpServer::EventLoop() {
    epoll_event events[MAX_EVENT_NUMBER];
    int next_worker = 0;
    int worker_count = sub_reactors_.size();

    while (true) {
        // 主线程只监听 listen_fd_，压力极小
        int nfds = epoll_wait(epollfd_, events, MAX_EVENT_NUMBER, -1);
        
        for (int i = 0; i < nfds; ++i) {
            int sockfd = events[i].data.fd;
            
            if (sockfd == listen_fd_) {
                while (true) {
                    int client_fd = accept(listen_fd_, nullptr, nullptr);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        continue;
                    }

                    // 1. 禁用 Nagle 算法：解决 40ms 延迟问题
                    int flag = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

                    // 2. 负载均衡：Round-Robin 算法
                    int current_worker = next_worker;
                    int target_fd = sub_reactors_[next_worker]->get_epoll_fd();
                    next_worker = (next_worker + 1) % worker_count;

                    // 3. 派发给子 Reactor
                    users_[client_fd].Init(client_fd, target_fd);
                    SPDLOG_DEBUG("Dispatching fd {} to Reactor {}", client_fd, current_worker);
                }
            }
        }
    }
}