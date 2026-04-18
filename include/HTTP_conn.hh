#pragma once

#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <string_view>

class alignas(64) http_conn {
public:
    static const int READ_BUFFER_SIZE = 8192;
    static const int FILENAME_LEN = 256;

    http_conn() : m_sockfd(-1), m_file_fd(-1) {}
    ~http_conn() { close_conn(); }

    // 初始化连接：由 accept 成功后调用
    void init(int sockfd);
    void init();
    // 关闭连接
    void close_conn();

    // --- 核心 IO 函数 ---
    // 非阻塞读：ET 模式下循环读取直到 EAGAIN
    bool read_once();
    // 逻辑处理：解析 HTTP 并匹配路由
    void process();
    // 非阻塞写：执行 sendfile 逻辑
    bool write();

    static int m_epollfd;         // 共享同一个 epollfd
    static void addfd(int epollfd, int fd, bool one_shot);
    static void modfd(int epollfd, int fd, int ev);
    // static void removefd(int epollfd, int fd);
    static int set_nonblocking(int fd);

private:
        // 解析状态：主状态机
    enum CHECK_STATE { 
        CHECK_STATE_REQUESTLINE = 0, // 解析请求行
        CHECK_STATE_HEADER,          // 解析请求头
        CHECK_STATE_BODY             // 解析消息体（静态网页暂时用不到）
    };

    // HTTP 响应状态码（简化版）
    enum HTTP_CODE { 
        NO_REQUEST, 
        GET_REQUEST, 
        BAD_REQUEST, 
    };
    enum RESOURCE_STATUS {
        RES_FOUND,     // 找到了（200）
        RES_NOT_FOUND, // 没找到（404）
        RES_FORBIDDEN, // 没权限（403）
        RES_ERROR      // 内部处理出错（500）
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    // --- 内部解析函数 ---
    HTTP_CODE parse_request();    // 总入口
    LINE_STATUS parse_line();       // 更新 m_checked_idx
    RESOURCE_STATUS do_request();     // 处理路由逻辑并打开文件

    const char* get_mime_type(const std::string_view& path);
    bool process_write(RESOURCE_STATUS ret);

    int m_sockfd;                 // 该连接的 socket
    int m_start_line;             // 记录当前行的位置
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;               // 读缓冲区中已存入数据的末尾索引
    int m_checked_idx;            // 当前正在解析的字节位置

    CHECK_STATE m_check_state;    // 主状态机当前位置

    // --- 解析结果（建议用 string_view 零拷贝） ---
    std::string_view m_url;
    // std::string_view m_version;
    bool m_linger;                // Keep-Alive 标志

    // --- 发送文件相关 (sendfile 专用) ---
    std::string m_header;
    size_t bytes_have_send;
    size_t bytes_to_send;

    std::string_view m_real_path;// 路由匹配后的物理路径
    int m_file_fd;
    off_t m_file_offset;          // 记录 sendfile 发送进度
    size_t m_file_size;           // 文件总大小
    struct stat m_file_stat;
};