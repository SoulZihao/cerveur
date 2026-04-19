#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include "HTTP_conn.hh"
#include "HTTP_Server.hh"


bool HttpServer::init(int port) {
	m_port = port;
	m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_listenfd == -1) return false;

	// 设置端口复用，防止重启服务器时出现 Address already in use
	int reuse = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(m_port);

	if (bind(m_listenfd, (struct sockaddr*)&address, sizeof(address)) == -1) {
		return false;
	}

	if (listen(m_listenfd, SOMAXCONN) == -1) return false;
	this->m_epollfd = epoll_create1(0);
    http_conn::m_epollfd = this->m_epollfd;
	// 3. 初始化连接池
	m_users = new http_conn[MAX_FD];
	// 添加服务端socket
	http_conn::addfd(m_epollfd, m_listenfd, false);
    http_conn::set_nonblocking(m_listenfd);
	std::cout << "HTTP Server Initialized on Port: " << m_port << std::endl;
	return true;
}
// 4. 事件循环
void HttpServer::event_loop(){
    epoll_event events[MAX_EVENT_NUMBER];
    while (true) {
        int nfds = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        
        for (int i = 0; i < nfds; ++i) {
            int sockfd = events[i].data.fd;

            // 情况 1：新连接 (New Connection)
            if (sockfd == m_listenfd) {
                while (true) { // ET 模式下 accept 也要循环读完
                    int client_fd = accept(m_listenfd, nullptr, nullptr);
                    if (client_fd < 0) break;
                    
                    // 初始化该 fd 对应的对象
                    m_users[client_fd].init(client_fd);
                    // 将新 fd 加入监听，初始为读事件
                    // addfd(m_epollfd, client_fd, true);
                }
            }
            
            // 情况 2：客户端发来数据 (Read)
            else if (events[i].events & EPOLLIN) {
                // 调用封装好的读函数
                if (m_users[sockfd].read_once()) {
                    // 读完后立刻进行逻辑解析
                    m_users[sockfd].process();
                    // 解析完后，我们要写数据（发送文件），所以改为监听写事件
                    // modfd(m_epollfd, sockfd, EPOLLOUT);
                } else {
                    // 读取失败（如对端关闭），关闭连接
                    m_users[sockfd].close_conn();
                }
            }
            
            // 情况 3：可以向客户端发数据了 (Write)
            else if (events[i].events & EPOLLOUT) {
                // 执行非阻塞写
                if (!m_users[sockfd].write_once()) {
                    // 如果写失败（比如对端突然断开，或者文件读取异常）
                    // 这里调用的 close_conn 就是所谓“析构”
                    m_users[sockfd].close_conn();
                }
                // 如果 write_once 返回 true，说明要么发完了，要么还在等待缓冲区，
                // 内部已经处理好了 modfd 或 keep-alive 的逻辑。
            }
            
            // 情况 4：错误处理
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                m_users[sockfd].close_conn();
            }
        }
    }
}
