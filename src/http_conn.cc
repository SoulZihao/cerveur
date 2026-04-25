#include <sys/sendfile.h>
#include <cstring>
#include <cassert>
#include "routes.h"
#include "http_conn.h"
#include "utils.h"

// thread_local 变量用于异步过程必须缓存

//static thread_local char header_buffer[HttpConn::kFileNameLen];
// 专门给 accept 后的新连接用
void HttpConn::Init(int sockfd,int target_epoll_fd) {
    // acquire to read
    while (lock_.test_and_set(std::memory_order_acquire)) {}
    Init(); // 调用私有的无参 Init 清空状态
    // epollfd_ 是每个线程独有的
    this->epollfd_ = target_epoll_fd;
    a_sockfd_.store(sockfd);
    // 放在后面的话，在init()时有可能sockfd就被分发到别的线程中了
    check(set_nonblocking(sockfd));
    check(addfd(epollfd_, sockfd));
    // finish write,so release it 
    lock_.clear(std::memory_order_release);
}
// 专门给长连接重置状态用
void HttpConn::Init() {
    read_idx_ = 0;
    checked_index_ = 0;
    start_line_ = 0; // 记录当前行的起始位置
    check_state_ = kRequestLine;
    linger_ = false;
    file_fd_ = -1;
    m_file_offset = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
}

void HttpConn::close_conn() {
    while (lock_.test_and_set(std::memory_order_acquire)) {
        // spin
    }
    int sockfd = a_sockfd_.exchange(-1);
    int fd_to_close = file_fd_.exchange(-1);
    if (fd_to_close != -1) {
        close(fd_to_close);
    }
    if (sockfd != -1) {
        // 从 epoll 中移除
        check(epoll_ctl(epollfd_, EPOLL_CTL_DEL, sockfd, 0));
        // 从 linux 内核列表中移除，从而可以被再次 accept
        close(sockfd);
    }
    lock_.clear(std::memory_order_release);
}

int HttpConn::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 添加 FD 到 epoll
int HttpConn::addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    // 基础事件：读、边缘触发、对端断开挂起、只能同时被一个线程处理
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    return epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

// 修改 FD 事件状态，确保此函数执行后此线程中不会再执行任何事务
int HttpConn::modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    // 关键点：重置时必须再次带上 EPOLLET 和 EPOLLONESHOT
    event.events = ev | EPOLLET | EPOLLRDHUP;
    return epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

HttpConn::LineStatus HttpConn::parse_line() {
    // 从当前检查的位置开始寻找 \r\n
    char temp = backup_buff_[checked_index_];
    char* cr_pos = (char*)memchr(backup_buff_ + checked_index_, '\r', read_idx_ - checked_index_);
    if (!cr_pos || cr_pos + 1 >= backup_buff_ + read_idx_) {
        return LineStatus::kOpen;
    }
    if (*(cr_pos + 1) == '\n') {
        // 仅当检查成功时更新 checked_index_
        checked_index_ = cr_pos - backup_buff_ + 2;
        return LineStatus::kOK;
        // 如果 \r 是当前缓冲区的最后一个字符，说明行还没传完
        // 只有 \r 没有 \n，不符合 HTTP 规范
    }else return LineStatus::kBad;
    // 遍历完还没找到 \r，说明数据不全
    return LineStatus::kOpen;
}

HttpConn::HttpCode HttpConn::parse_request() {
    LineStatus line_status = LineStatus::kOK;

    while ((line_status = parse_line()) == LineStatus::kOK) {
        size_t line_len = checked_index_ - start_line_ - 2;
        std::string_view line_data(backup_buff_ + start_line_, line_len);
        start_line_ = checked_index_; // 更新下一行的起始位置

        switch (check_state_) {
            case kRequestLine: {
                // 简单示例：解析 GET /index.html HTTP/1.1
                // std::string_view line(text);
                size_t s1 = line_data.find(' ');
                size_t s2 = line_data.find(' ', s1 + 1);
                url_ = line_data.substr(s1 + 1, s2 - s1 - 1);
                check_state_ = kHeader;
                break;
            }
            case kHeader: {
                // if (line_data[0] == '\0') return HttpCode::kGetReq; // 空行说明 Header 结束
                // 可以在这里解析 Connection: keep-alive
                if (line_data.find("Connection: keep-alive")!=std::string_view::npos) linger_ = true;
                if (line_data.empty()) {
                    // 如果是 GET，直接去 do_request
                    // 如果有 Body (Content-Length > 0)，则转入下一个状态
                    return (url_.length()>0) ? HttpCode::kGetReq : HttpCode::kNoReq;
                }
                break;
            }
            default: return HttpCode::kBadReq;
        }
    }
    if(line_status == LineStatus::kBad)return HttpCode::kBadReq;
    // LineStatus::kOpen
    return HttpCode::kNoReq;
}

HttpConn::ResourceStatus HttpConn::do_request() {
    auto& router = Router::getInstance();
    
    // 1. 直接从路由缓存中获取资源元数据
    // 注意：这里的 url_ 应当是处理过尾部空格且以 / 开头的路径
    file_info = router.GetResource(std::string(url_));
    bool is_404 = false;

    // 2. 如果没找到，尝试获取预存的 404 页面
    if (!file_info) {
        is_404 = true;
        file_info = router.GetResource("/404.html");
        
        // 如果连 404 页面都没缓存（比如启动时扫描失败），返回彻底错误
        if (!file_info) return ResourceStatus::kError;
    }
    m_file_offset = 0;

    // 5. 打开文件
    // 虽然元数据在内存，但发送文件还是需要 FD（除非你用了内存映射缓存）
    file_fd_ = open(file_info->path.c_str(), O_RDONLY);
    if (file_fd_ < 0) return ResourceStatus::kError;

    // 6. 返回对应的状态码
    return is_404 ? ResourceStatus::kNotFound : ResourceStatus::kFound;
}

// 主要处理逻辑：执行modfd
void HttpConn::process() {
    // 1. 调用主状态机进行解析
        SPDLOG_DEBUG("Received a Request from Client {}:\n"
                    "{}\n",
                    a_sockfd_.load(),
                    std::string_view(backup_buff_, static_cast<size_t>(read_idx_)));
    HttpCode read_ret = parse_request();

    // 2. 如果请求还没收全 (HttpCode::kNoReq)，继续监听读事件
    if (read_ret == HttpCode::kNoReq) {
        modfd(epollfd_, a_sockfd_.load(), EPOLLIN);
        // 确保 modfd 之后本线程不会再执行任何事情
        return;
    }

    // 3. 根据解析结果决定响应逻辑
    switch (read_ret) {
        case HttpCode::kGetReq: {
            // 解析成功，去查找文件、映射内存或准备 sendfile 路径
            ResourceStatus write_ret = do_request();
            // 请求头的构造逻辑
            process_write(write_ret);
            // modfd(epollfd_, a_sockfd_.load(), EPOLLOUT);
            if(!write_once()){
                close_conn();
            }
            return;
        }
        // case HttpCode::HttpCode::kBadReq: {
        //     // 解析失败，准备 400 错误的 Header
        //     add_status_line(400, "Bad Request");
        //     // ... add_headers ...
        //     break;
        // }
        // case NO_RESOURCE: {
        //     // 404 错误处理
        //     add_status_line(404, "Not Found");
        //     break;
        // }
        default:
            // 500 内部错误处理
            break;
    }
    // 4. 无论成功还是失败，只要准备好了响应内容，就切换到写事件
    // 触发 EPOLLOUT 之后，event_loop 会调用 write_once() 函数
}

// 只在http_server中调用一次
// true：需要等待；false：需要重置
bool HttpConn::read_once() {
    if (read_idx_ >= kReadBufferSize) return false;
    //char* recvd_buff = is_tls_ ? read_buffer : backup_buff_;
    while (true) {
        // 从当前位置开始读
        ssize_t bytes_read = recv(a_sockfd_.load(), backup_buff_ + read_idx_, kReadBufferSize - read_idx_, 0);
        
        if (bytes_read == -1) {
            // EAGAIN: 内核缓冲区已经读空，但依然返回true，让process()决定是否继续读
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            spdlog::error("Errno: {}, msg: {},read error on fd {}", 
                errno, strerror(errno),a_sockfd_.load());
            // check(bytes_read);
            return false;
        } else if (bytes_read == 0) {
            spdlog::error("Client {} closed connection (EOF)", a_sockfd_.load());
            return false; // 对方关闭连接
        }
        read_idx_ += bytes_read;
        // 对方发送的请求信息太大，直接交由后续处理
        if (read_idx_ >= kReadBufferSize) {
            return true;
        }
    }
    return true;
}

bool HttpConn::process_write(const ResourceStatus& ret) {
    // header_buffer.clear();
    switch (ret) {
        case ResourceStatus::kFound: {
            // snprintf 会自动在结尾补 \0
            header_len_ = snprintf(backup_buff_, kFileNameLen, 
                           "HTTP/1.1 200 OK\r\n"
                           "Server: Cerveur/1.0\r\n"
                           "Content-Length: %ld\r\n"
                           "Content-Type: %s\r\n"
                           "Connection: %s\r\n"
                           "\r\n", 
                           file_info->file_size, 
                           file_info->mime_type, 
                           linger_ ? "keep-alive" : "close");
            break;
        }
        case ResourceStatus::kNotFound: {
            // 注意：这里通常需要一个 404 页面，m_file_size 应该是 404 文件的长度
            header_len_ = snprintf(backup_buff_, kFileNameLen, 
                           "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: %ld\r\n"
                           "Content-Type: %s\r\n"
                           "Connection: close\r\n"
                           "\r\n", 
                           file_info->file_size, 
                           file_info->mime_type);
            break;
        }
        case ResourceStatus::kForbidden: {
            header_len_ = snprintf(backup_buff_, kFileNameLen, 
                           "HTTP/1.1 403 Forbidden\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n"
                           "\r\n");
            break;
        }
        default:
            return false;
    }
    if (header_len_ >= kFileNameLen || header_len_ < 0) {
        return false;
    }
    return true;
}

// true：需要等待；false：需要重置
bool HttpConn::write_once() {
    // 1. 发送 Header
    ssize_t temp = 0;
    bytes_to_send = header_len_ - bytes_have_send;
    SPDLOG_DEBUG("send header to socket {}:\n{}",a_sockfd_.load(), backup_buff_);
    while (bytes_to_send > 0) {
        temp = send(a_sockfd_.load(), backup_buff_ + bytes_have_send, bytes_to_send, MSG_MORE);
        if (temp <= -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                modfd(epollfd_, a_sockfd_.load(), EPOLLOUT);
                return true;
            }
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
    }

    // 2. 循环发送文件
    while (true) {
        ssize_t temp = sendfile(a_sockfd_.load(), file_fd_, &m_file_offset, file_info->file_size - m_file_offset);
        
        if (temp == -1) {
            // 情况 A：缓冲区满了
            if (errno == EAGAIN) {
                // 虽然没发完，但因为开启了 ONESHOT，必须再次注册写事件，保证下次缓冲区空了能被唤醒
                modfd(epollfd_, a_sockfd_.load(), EPOLLOUT); 
                return true; // 注意：这里返回 true，表示当前处理正常（仅仅是需要等待）
            }
            // 真正报错
            spdlog::error("Failed to sendfile on socket {}",a_sockfd_.load());
            return false;
        }

        if (m_file_offset >= file_info->file_size) break; // 发送成功完成
    }
    SPDLOG_DEBUG("Response have send to socket {}",a_sockfd_.load());
    // 3. 发送完毕后的清理
    close(file_fd_);
    file_fd_ = -1;

    // 4. 处理后续：发完后该怎么办？
    if (linger_) {
        // 如果是 Keep-Alive 长连接：
        Init(); // 调用私有的无参 Init()，重置缓冲区索引和状态机，但保留 sockfd
        modfd(epollfd_, a_sockfd_.load(), EPOLLIN); // 切换回读模式，等待下一个请求
        return true;
    }
    // normally exit
    return false;
}