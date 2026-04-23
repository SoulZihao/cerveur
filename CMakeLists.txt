# 1. 基础设置
cmake_minimum_required(VERSION 3.28)
project(Cerveur LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 设置 C++ 标准（建议使用 C++23 以支持 jthread 等特性）
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# 2. 引入 Catch2 (使用 FetchContent 自动下载)
# FetchContent_Declare(
#   Catch2
#   GIT_REPOSITORY https://github.com/catchorg/Catch2.git
#   GIT_TAG        v3.14.0
# )
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0   # 建议固定版本
)
# FetchContent_MakeAvailable(Catch2)
FetchContent_MakeAvailable(spdlog)

# 3. 配置头文件路径
# include_directories(include)

# 4. 构建核心逻辑库
# 将 src 下的源文件编译成一个静态库，方便主程序和测试程序共享
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/*.cc")
# 排除掉主程序的入口文件（假设是 main.cc），避免与单元测试的 main 冲突
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main.cc")
add_library(CerveurCore STATIC ${CORE_SOURCES})
target_include_directories(CerveurCore PUBLIC include)
target_link_libraries(CerveurCore PUBLIC spdlog::spdlog)


# 5. 构建主程序
add_executable(server src/main.cc)
# target_link_options(server PRIVATE -fsanitize=thread)#
target_link_libraries(server PRIVATE CerveurCore)
option(ENABLE_CERVEUR_DEBUG "Enable debug flags for CerveurCore" OFF)
# target_compile_options(CerveurCore PRIVATE -fsanitize=thread)
target_compile_options(CerveurCore PUBLIC
    $<$<BOOL:${ENABLE_CERVEUR_DEBUG}>:-g>
)
target_compile_definitions(CerveurCore PUBLIC
    $<$<BOOL:${ENABLE_CERVEUR_DEBUG}>:DEBUG>
)

# 6. 构建单元测试程序
# enable_testing()
# file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cc")
# 创建测试执行文件
# add_executable(unit_tests ${TEST_SOURCES}) 

# 链接核心逻辑库
#target_link_libraries(unit_tests PRIVATE CerveurCore Catch2::Catch2WithMain)

# target_compile_options(spdlog::spdlog INTERFACE -O3 -DNDEBUG)
# 自动发现测试用例（由 Catch2 提供的辅助功能）
# include(Catch)
# catch_discover_tests(unit_tests)