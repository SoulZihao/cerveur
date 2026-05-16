#pragma once

#include <atomic>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string_view>
#include "routes.h"

class alignas(64) HttpConn {
public:
    static constexpr int kReadBufferSize = 8192;
    static constexpr int kFileNameLen = 256;

    HttpConn() : a_sockfd_(-1) {}
    ~HttpConn() { close_conn(); }

    // 初始化连接：由 accept 成功后调用
    void Init(int sockfd,int target_epoll_fd);
    
    // 关闭连接
    void close_conn();

    // --- 核心 IO 函数 ---
    // 非阻塞读：ET 模式下循环读取直到 EAGAIN
    bool read_once();
    // 逻辑处理：解析 HTTP 并匹配路由
    void process();
    // 非阻塞写：执行 sendfile 逻辑
    bool write_once();

    int epollfd_;         // 共享同一个 epollfd
    static int addfd(int epollfd, int fd);
    static int modfd(int epollfd, int fd, int ev);
    // static void removefd(int epollfd, int fd);
    static int set_nonblocking(int fd);

private:
    void Init();
        // 存储连接状态
        // 解析状态：主状态机
    enum CheckState { 
        kRequestLine = 0, // 解析请求行
        kHeader,          // 解析请求头
        kBody             // 解析消息体（静态网页暂时用不到）
    };

    // HTTP 响应状态码（简化版）
    enum class HttpCode { 
        kNoReq, 
        kGetReq, 
        kBadReq, 
    };
    enum class ResourceStatus {
        kFound,     // 找到了（200）
        kNotFound, // 没找到（404）
        kForbidden, // 没权限（403）
        kError      // 内部处理出错（500）
    };
    enum class LineStatus
    {
        kOK = 0,
        kBad,
        kOpen
    };

    // --- 内部解析函数 ---
    HttpCode parse_request();    // 总入口
    LineStatus parse_line();       // 更新 checked_index_
    ResourceStatus do_request();     // 处理路由逻辑并打开文件

    bool process_write(const ResourceStatus& ret);

    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    int a_sockfd_;       // 该连接 the socket
    int start_line_;                // 记录当前行的位置
    int checked_index_;             // 当前正在解析的字节位置
    char backup_buff_[kReadBufferSize];
    int read_idx_;               // 读缓冲区中已存入数据的末尾索引
    

    CheckState check_state_;    // 主状态机当前位置

    // --- 解析结果（建议用 string_view 零拷贝） ---
    std::string_view url_;
    // std::string_view m_version;
    bool linger_;                // Keep-Alive 标志
    //bool is_tls_;
    // --- 发送文件相关 ---
    int header_len_;
    size_t bytes_have_send;
    size_t bytes_to_send;

    int file_fd_;
    off_t m_file_offset;          // 记录 sendfile 发送进度
    const ResourceInfo* file_info;
};