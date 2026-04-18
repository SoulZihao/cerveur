#include "HTTP_conn.hh"
#include <sys/sendfile.h>
#include <cstring>
#include <assert.h>
#include <Routes.hh>

int http_conn::m_epollfd = -1;

// 专门给 accept 后的新连接用
void http_conn::init(int sockfd) {
    m_sockfd = sockfd;
    addfd(m_epollfd, sockfd, true);
    assert(set_nonblocking(sockfd)!=-1);
    init(); // 调用私有的无参 init 清空状态
}
// 专门给长连接重置状态用
void http_conn::init() {
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0; // 记录当前行的起始位置
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_file_fd = -1;
    m_file_offset = 0;
    // 初始化缓冲区
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
}

void http_conn::close_conn() {
    if (m_sockfd != -1) {
        // 从 epoll 中移除
        epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_sockfd, 0);
        close(m_sockfd);
        m_sockfd = -1;
    }
    if (m_file_fd != -1) {
        close(m_file_fd);
        m_file_fd = -1;
    }
}

int http_conn::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 添加 FD 到 epoll
void http_conn::addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    // 基础事件：读、边缘触发、对端断开挂起
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

// 修改 FD 事件状态
void http_conn::modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    // 关键点：重置时必须再次带上 EPOLLET 和 EPOLLONESHOT
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

http_conn::LINE_STATUS http_conn::parse_line() {
    // 从当前检查的位置开始寻找 \r\n
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        char temp = m_read_buf[m_checked_idx];
        
        // 发现 \r，说明可能到行尾了
        if (temp == '\r') {
            // 如果 \r 是当前缓冲区的最后一个字符，说明行还没传完
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            // 如果后面跟着 \n，说明找到了完整的行
            if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 此时 m_checked_idx 指向 \r
                // 我们不修改缓冲区，只是跳过这两个字符，让下次调用从新的一行开始
                m_checked_idx += 2; 
                return LINE_OK;
            }
            // 只有 \r 没有 \n，不符合 HTTP 规范
            return LINE_BAD;
        }
    }
    // 遍历完还没找到 \r，说明数据不全
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request() {
    LINE_STATUS line_status = LINE_OK;

    while ((line_status = parse_line()) == LINE_OK) {
        size_t line_len = m_checked_idx - m_start_line - 2;
        std::string_view line_data(m_read_buf + m_start_line, line_len);
        m_start_line = m_checked_idx; // 更新下一行的起始位置

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                // 简单示例：解析 GET /index.html HTTP/1.1
                // std::string_view line(text);
                size_t s1 = line_data.find(' ');
                size_t s2 = line_data.find(' ', s1 + 1);
                m_url = line_data.substr(s1 + 1, s2 - s1 - 1);
                m_check_state = CHECK_STATE_HEADER;
                break;
            }
            case CHECK_STATE_HEADER: {
                // if (line_data[0] == '\0') return GET_REQUEST; // 空行说明 Header 结束
                // 可以在这里解析 Connection: keep-alive
                if (line_data.find("Connection: keep-alive")!=std::string_view::npos) m_linger = true;
                if (line_data.empty()) {
                    // 如果是 GET，直接去 do_request
                    // 如果有 Body (Content-Length > 0)，则转入下一个状态
                    return (m_url.length()>0) ? GET_REQUEST : NO_REQUEST;
                }
                break;
            }
            default: return BAD_REQUEST;
        }
    }
    return NO_REQUEST;
}

// 解析路由
http_conn::HTTP_CODE http_conn::do_request() {
    const char* doc_root = "./";

    // 使用路由器查找映射
    auto& router = Router::getInstance();
    auto mapped_file = router.find(std::string(m_url));
    std::string filename;
    if (mapped_file) {
        // 使用路由器返回的文件名
        filename = "templates/" + mapped_file.value_or("404.html");
    } else if (m_url.find("/static/") == 0) {
        // 静态资源
        filename = "static/index.css";
    }
    // 安全构建路径
    m_real_path = doc_root + filename;

    // 4. 获取文件状态
    if (stat(m_real_path.c_str(), &m_file_stat) < 0) {
        return NO_RESOURCE; // 404
    }

    // 5. 权限检查：是否可读
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST; // 403
    }

    // 6. 类型检查：确保不是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST; // 400
    }
    m_file_fd = open(m_real_path.c_str(), O_RDONLY);
    if (m_file_fd < 0) return INTERNAL_ERROR; // 打开失败，返回 500
    
    m_file_size = m_file_stat.st_size;
    m_file_offset = 0;
    // 7. 到这里说明文件一切正常
    // 在之后的 process_write 中将使用 m_real_file 进行 sendfile
    return FILE_REQUEST;
}

// 主要处理逻辑,执行modfd
void http_conn::process() {
    // 1. 调用主状态机进行解析
    printf("\n[DEBUG] Received a Request from Client %d:\n", m_sockfd);
    printf("---------- START ----------\n");
    // 注意：因为缓冲区里可能包含之前的旧数据，
    // 我们只打印从开头到 m_read_idx 之间的内容
    printf("%.*s", m_read_idx, m_read_buf);
    printf("\n----------  END  ----------\n\n");
    HTTP_CODE read_ret = parse_request();

    // 2. 如果请求还没收全 (NO_REQUEST)，继续监听读事件
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // bool prepare_ret = process_write(read_ret);
    // if (!prepare_ret) {
    //     close_conn();
    // }
    // 此时 m_write_buf 已经装满了 Header，准备切换到写模式
    // modfd(m_epollfd, m_sockfd, EPOLLOUT);

    // 3. 根据解析结果决定响应逻辑
    HTTP_CODE write_ret;
    switch (read_ret) {
        case GET_REQUEST: {
            // 解析成功，去查找文件、映射内存或准备 sendfile 路径
            // do_request 处理路由逻辑
            write_ret = do_request();
            // 应在此处添加请求头的构造逻辑
            if(write_ret == FILE_REQUEST) modfd(m_epollfd, m_sockfd, EPOLLOUT);
            break;
        }
        // case BAD_REQUEST: {
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
    // 触发 EPOLLOUT 之后，event_loop 会调用 write() 函数
    
}

// 只在http_server中调用一次
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) return false;

    while (true) {
        // 从当前写位置开始读
        ssize_t bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        
        if (bytes_read == -1) {
            // EAGAIN 说明内核缓冲区已经读空了
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        } else if (bytes_read == 0) {
            return false; // 对方关闭连接
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// true：需要等待；false：需要重置
bool http_conn::write() {
    // 1. 发送 Header
    if (m_file_offset == 0) {
        std::string header = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(m_file_size) + "\r\n\r\n";
        send(m_sockfd, header.data(), header.size(), 0);
    }

    // 2. 循环发送文件
    while (true) {
        ssize_t temp = sendfile(m_sockfd, m_file_fd, &m_file_offset, m_file_size - m_file_offset);
        
        if (temp == -1) {
            // 情况 A：缓冲区满了
            if (errno == EAGAIN) {
                // 虽然没发完，但因为开启了 ONESHOT，必须再次注册写事件，保证下次缓冲区空了能被唤醒
                modfd(m_epollfd, m_sockfd, EPOLLOUT); 
                return true; // 注意：这里返回 true，表示当前处理正常（仅仅是需要等待）
            }
            // 真正报错
            return false;
        }

        if (m_file_offset >= m_file_size) break; // 发送成功完成
    }
    
    // 3. 发送完毕后的清理
    close(m_file_fd);
    m_file_fd = -1;

    // 4. 处理后续：发完后该怎么办？
    if (m_linger) {
        // 如果是 Keep-Alive 长连接：
        init(); // 调用私有的无参 init()，重置缓冲区索引和状态机，但保留 sockfd
        modfd(m_epollfd, m_sockfd, EPOLLIN); // 切换回读模式，等待下一个请求
        return true;
    } else {
        return false;
    }
}