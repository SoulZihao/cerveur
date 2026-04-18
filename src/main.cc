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
#include <string>
#include "HTTP_Server.hh"
#include "Routes.hh"
//#include "Response.hh"

int main() {
    // 1. 获取服务器单例
    // 这样全局只有一个控制中心，方便资源管理
    auto& server = HttpServer::getInstance();

    // 2. 初始化服务器
    // 内部完成 socket, bind, listen 以及 users 数组的分配
    if (!server.init(6969)) {
        return EXIT_FAILURE;
    }

    // 3. 注册路由
    // 建议 Router 也采用单例模式，或者作为 server 的一个成员
    auto& router = Router::getInstance();
    router.add("/", "index.html");
    router.add("/about", "about.html");
    router.printAll();

    // 4. 开启上帝模式：进入 epoll 事件循环
    // 这个函数会一直运行，直到服务器关闭
    server.event_loop();
    return EXIT_SUCCESS;
}
