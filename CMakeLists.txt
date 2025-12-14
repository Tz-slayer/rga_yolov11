# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(test)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找OpenCV
find_package(OpenCV REQUIRED)

# 查找RKNN库（假设安装在系统路径或指定路径）
# 如果RKNN不在标准路径，需要设置CMAKE_PREFIX_PATH或类似
find_library(RKNN_API rknn_api REQUIRED)
find_library(RGA rga REQUIRED)

# 包含目录
include_directories(include)

# 源文件列表（从src目录收集所有.cpp文件）
file(GLOB SOURCES src/*.cpp)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 编译定义（根据test.cpp中的条件编译）
add_definitions(
    -DUSE_RGA
    -DZERO_COPY
)

# 链接库
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    ${RKNN_API}
    ${RGA}
    pthread
    dl
)

# 如果需要特定路径，可以添加：
# link_directories(/path/to/rknn/libs /path/to/rga/libs)
# include_directories(/path/to/rknn/includes /path/to/rga/includes)