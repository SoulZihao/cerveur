#pragma once
#include <string>
#include <unordered_map>
#include <optional>

class Router {
    // 路径：文件名
    std::unordered_map<std::string, std::string> route_map;
public:
    // 添加路由：直接插入哈希表
    void add(const std::string& path, const std::string& file);

    // 搜索路由：返回文件名，如果没找到则返回空对象 (C++17 std::optional)
    std::optional<std::string> find(const std::string& path) const ;
    // 打印所有路由
    void printAll() const ;
};