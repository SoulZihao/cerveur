#pragma once
#include <string>
#include <unordered_map>
#include <cstring>
#include <sys/stat.h>

struct ResourceInfo {
    std::string path;       // 物理路径
    off_t file_size;        // 文件大小（对应 stat.st_size）
    char mime_type[32];  // MIME 类型（如 text/css）
    
};


class Router {
    // 路径：文件名
    std::unordered_map<std::string, ResourceInfo> route_map;
    // 辅助函数获取 MIME 类型
    static const char* GetMimeType(const std::string& filename) {
            auto dot_pos = filename.find_last_of('.');
            if (dot_pos == std::string::npos) return "text/plain";
            std::string ext = filename.substr(dot_pos);
            if (ext == ".html") return "text/html";
            if (ext == ".css")  return "text/css";
            if (ext == ".js")   return "application/javascript";
            if (ext == ".jpg")  return "image/jpeg";
            return "text/plain";
        }

public:
    static Router& getInstance() {
        static Router instance;
        return instance;
    }
    // 打印所有路由
    void printAll() const ;

    void ScanAndCache(const std::string& root_path);

    const ResourceInfo* GetResource(const std::string& url) {
        auto it = route_map.find(url);
        return (it != route_map.end()) ? &(it->second) : nullptr;
    }
};