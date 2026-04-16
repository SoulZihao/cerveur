#include <Routes.hh>
#include <iostream>
	// 添加路由：直接插入哈希表
    void Router::add(const std::string& path, const std::string& file) {
        if (route_map.find(path) != route_map.end()) {
            std::cerr << "Warning: Route [" << path << "] already exists, overwriting...\n";
        }
        route_map[path] = file;
    }

    // 搜索路由：返回文件名，如果没找到则返回空对象 (C++17 std::optional)
    std::optional<std::string> Router::find(const std::string& path) const {
        auto it = route_map.find(path);
        if (it != route_map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 打印所有路由
    void Router::printAll() const {
        std::cout << "========== ALL AVAILABLE ROUTES ==========" << std::endl;
        for (const auto& [path, file] : route_map) {
            std::cout << path << " -> " << file << std::endl;
        }
        std::cout << "==========================================" << std::endl;
    }