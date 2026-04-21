#include <iostream>
#include <source_location>
#include <string_view>
#include <cstring>
#include <spdlog/spdlog.h>
#include "spdlog/sinks/stdout_color_sinks.h" // 彩色控制台

// 统一检查函数：如果 condition 为假（比如 -1 == res），则报错
template <typename T>
inline void check(T val, 
           std::source_location loc = std::source_location::current(),std::string_view msg = "") {
    #ifndef NDEBUG
    // 对于大多数系统调用，返回 -1 表示失败
    if (val == -1) {
        std::cerr << "[-] Error in " << loc.file_name() << ":" 
                  << loc.line() << " in function " << loc.function_name() 
                  << "\n    Context: " << msg 
                  << "\n    Reason: " << std::strerror(errno) << " (errno: " << errno << ")\n"; 
        exit(EXIT_FAILURE); 
    }
    #else
    val;
    #endif
}

inline void init_logger() {
    // 创建一个带颜色的控制台日志器
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("Cerveur", console_sink);
    
    // 设置日志格式：[时间] [日志等级] [线程ID] 内容
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%^%l%$]  %v");
    
    // 设置默认日志器
    spdlog::set_default_logger(logger);

    // 设置日志等级：Debug 模式下设为 debug，Release 下设为 info
#ifdef NDEBUG
    spdlog::set_level(spdlog::level::info);
#else
    spdlog::set_level(spdlog::level::debug);
#endif
}