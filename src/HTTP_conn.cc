#include "HTTP_conn.hh"
#include <sys/sendfile.h>
#include <cstring>
#include <assert.h>
#include <Routes.hh>

int http_conn::m_epollfd = -1;
static thread_local char path_buffer[http_conn::FILENAME_LEN];
static thread_local char header[http_conn::FILENAME_LEN];
static thread_local int header_len;
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
    bytes_to_send = 0;
    bytes_have_send = 0;
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
    char temp = m_read_buf[m_checked_idx];
    char* cr_pos = (char*)memchr(m_read_buf + m_checked_idx, '\r', m_read_idx - m_checked_idx);
    if (!cr_pos || cr_pos + 1 >= m_read_buf + m_read_idx) {
        return LINE_OPEN;
    }
    if (*(cr_pos + 1) == '\n') {
        // 仅当检查成功时更新 m_checked_idx
        m_checked_idx = cr_pos - m_read_buf + 2;
        return LINE_OK;
        // 如果 \r 是当前缓冲区的最后一个字符，说明行还没传完
        // 只有 \r 没有 \n，不符合 HTTP 规范
    }else return LINE_BAD;
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
    if(line_status == LINE_BAD)return BAD_REQUEST;
    // LINE_OPEN
    return NO_REQUEST;
}

// 解析路由
http_conn::RESOURCE_STATUS http_conn::do_request() {
    const char* doc_root = ".";
    auto& router = Router::getInstance();
    auto mapped_file = router.find(std::string(m_url));
    
    bool is_404 = false;
    int len = 0;

    // 1. 尝试定位原始资源
    if (mapped_file) {
        len = snprintf(path_buffer, FILENAME_LEN, "%s/templates/%s", doc_root, mapped_file->c_str());
    } else if (m_url.find("/static/") == 0) {
        len = snprintf(path_buffer, FILENAME_LEN, "%s%.*s", doc_root,(int)m_url.size(), m_url.data());
    } else {
        is_404 = true;
    }

    // 2. 检查原始资源是否存在（如果目前还不是 404 的话）
    if (!is_404 && stat(path_buffer, &m_file_stat) < 0) {
        is_404 = true;
    }

    // 3. Fallback 逻辑：如果确定是 404，强行改道去拿 404.html
    if (is_404) {
        snprintf(path_buffer, FILENAME_LEN, "%stemplates/404.html", doc_root);
        if (stat(path_buffer, &m_file_stat) < 0) {
            // 如果连 404.html 都没有，那只能返回彻底的错误
            return RES_ERROR; 
        }
        // 标记我们要返回 404 状态码，但下面会继续打开文件
    }

    // 4. 通用的权限和类型检查
    if (!(m_file_stat.st_mode & S_IROTH)) return RES_FORBIDDEN;
    if (S_ISDIR(m_file_stat.st_mode)) return RES_ERROR;

    // 5. 统一打开文件（无论是目标文件还是 404 页面）
    m_file_fd = open(path_buffer, O_RDONLY);
    if (m_file_fd < 0) return RES_ERROR;

    m_file_size = m_file_stat.st_size;
    m_file_offset = 0;

    // 6. 返回对应的状态码，指导 process_write 写 Header
    return is_404 ? RES_NOT_FOUND : RES_FOUND;
}

// 主要处理逻辑,执行modfd
void http_conn::process() {
    // 1. 调用主状态机进行解析
    // printf("\n[DEBUG] Received a Request from Client %d:\n", m_sockfd);
    // printf("---------- START ----------\n");
    // // 注意：因为缓冲区里可能包含之前的旧数据，
    // // 我们只打印从开头到 m_read_idx 之间的内容
    // printf("%.*s", m_read_idx, m_read_buf);
    // printf("\n----------  END  ----------\n");
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
    switch (read_ret) {
        case GET_REQUEST: {
            // 解析成功，去查找文件、映射内存或准备 sendfile 路径
            RESOURCE_STATUS write_ret = do_request();
            // 请求头的构造逻辑
            process_write(write_ret);
            modfd(m_epollfd, m_sockfd, EPOLLOUT);
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
    // 触发 EPOLLOUT 之后，event_loop 会调用 write_once() 函数
    
}

// 只在http_server中调用一次
// true：需要等待；false：需要重置
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) return false;

    while (true) {
        // 从当前写位置开始读
        ssize_t bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        
        if (bytes_read == -1) {
            // EAGAIN: 内核缓冲区已经读空，但依然返回true，让process()决定是否继续读
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        } else if (bytes_read == 0) {
            return false; // 对方关闭连接
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// 辅助函数获取 MIME 类型
const char* http_conn::get_mime_type(const char* path) {
    if (strstr(path,".html") != nullptr) return "text/html";
    if (strstr(path,".css") != nullptr)  return "text/css";
    if (strstr(path,".js") != nullptr)   return "text/javascript";
    if (strstr(path,".jpg") != nullptr)  return "image/jpeg";
    if (strstr(path,".png") != nullptr)  return "image/png";
    return "text/plain";
}

bool http_conn::process_write(RESOURCE_STATUS ret) {
    // header.clear();
    switch (ret) {
        case RES_FOUND: {
            // snprintf 会自动在结尾补 \0
            header_len = snprintf(header, FILENAME_LEN, 
                           "HTTP/1.1 200 OK\r\n"
                           "Server: Cerveur/1.0\r\n"
                           "Content-Length: %ld\r\n"
                           "Content-Type: %s\r\n"
                           "Connection: %s\r\n"
                           "\r\n", 
                           m_file_size, 
                           get_mime_type(path_buffer), 
                           m_linger ? "keep-alive" : "close");
            break;
        }
        case RES_NOT_FOUND: {
            // 注意：这里通常需要一个 404 页面，m_file_size 应该是 404 文件的长度
            header_len = snprintf(header, FILENAME_LEN, 
                           "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: %ld\r\n"
                           "Content-Type: %s\r\n"
                           "Connection: close\r\n"
                           "\r\n", 
                           m_file_size, 
                           get_mime_type(path_buffer));
            break;
        }
        case RES_FORBIDDEN: {
            header_len = snprintf(header, FILENAME_LEN, 
                           "HTTP/1.1 403 Forbidden\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n"
                           "\r\n");
            break;
        }
        default:
            return false;
    }
    if (header_len >= FILENAME_LEN || header_len < 0) {
        return false;
    }
    return true;
}

// true：需要等待；false：需要重置
bool http_conn::write_once() {
    // 1. 发送 Header
    ssize_t temp = 0;
    bytes_to_send = header_len - bytes_have_send;
    while (bytes_to_send > 0) {
        temp = send(m_sockfd, header + bytes_have_send, bytes_to_send, 0);
        if (temp <= -1) {
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
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