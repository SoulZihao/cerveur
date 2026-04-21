#include <iostream>
#include <filesystem>
#include "routes.h"
#include "utils.h"
namespace fs = std::filesystem;
    // 打印所有路由
void Router::printAll() const {
    std::cout << "========== ALL AVAILABLE ROUTES ==========" << std::endl;
    for (const auto& [path, file] : route_map) {
        std::cout << path << " -> " << file.path << std::endl;
    }
    std::cout << "==========================================" << std::endl;
}

void Router::ScanAndCache(const std::string& root_path){
    spdlog::debug("Current Working Directory: {}", fs::current_path().string());
    spdlog::debug("Target Absolute Path: {}", fs::absolute(root_path).string());

    if (!fs::exists(root_path)) {
        spdlog::error("Path {} does not exist!", root_path);
        return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root_path)) {
        if (entry.is_regular_file()) {
            std::string physical_path = entry.path().string();
            std::string url = physical_path.substr(root_path.length());
            if (url.empty() || url[0] != '/') url = "/" + url;

            // 提前获取 stat
            struct stat st;
            if (stat(physical_path.c_str(), &st) == 0) {
                // 1. 权限检查：必须具有“其他用户可读”权限 (S_IROTH)
                // 同时也建议检查 S_ISREG(st.st_mode) 确保它不是一个目录
                if ((st.st_mode & S_IROTH) && S_ISREG(st.st_mode)) {
                    ResourceInfo info;
                    info.path = physical_path;
                    info.file_size = st.st_size;
                    strcpy(info.mime_type,GetMimeType(physical_path));
                    route_map[url] = info;
                    
                    // 特殊处理首页
                    if (url == "/html/index.html") route_map["/"] = info;
                }else fprintf(stderr,"[WARNING] Resource %s exists but is not readable/regular file. Skipping.\n", physical_path.c_str());
            }
        }
    }
}