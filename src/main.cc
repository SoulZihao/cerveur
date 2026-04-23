#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include "utils.h"
#include "http_server.h"
#include "routes.h"
#include"thread_pool.h"
//#include "Response.h"

int main() {
    init_logger();
    #ifdef DEBUG
    spdlog::info("DEBUG mode");
    #else
    spdlog::info("RELEASE mode");
    #endif
    spdlog::info("Cerveur HTTP Server starting...");
    auto& server = HttpServer::getInstance();

    // 2. 初始化服务器
    // 内部完成 socket, bind, listen 以及 users 数组的分配
    if (!server.Init(6969)) {
        return EXIT_FAILURE;
    }

    // 3. 注册路由
    // 建议 Router 也采用单例模式，或者作为 server 的一个成员
    auto& router = Router::getInstance();
    router.ScanAndCache("../frontend");
    router.printAll();
    auto pool = std::make_shared<ThreadPool>();
    
    spdlog::info("Server event loop started on port {}", 6969);
    server.EventLoop(*pool);
    return EXIT_SUCCESS;
}
