cmake_minimum_required(VERSION 3.13)
project(ProdigyLLVMPass)

# 找到LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# 设置编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加LLVM定义和包含目录
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

# 构建Prodigy Pass共享库
add_library(ProdigyPass MODULE src/ProdigyPass.cpp)

# 设置RTTI（如果LLVM没有使用RTTI）
if(NOT LLVM_ENABLE_RTTI)
    set_target_properties(ProdigyPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
endif()

# 使用LLVM的CXX flags
set_target_properties(ProdigyPass PROPERTIES
    COMPILE_FLAGS "${LLVM_CXXFLAGS}"
)

# 构建运行时库（用于测试）
add_library(ProdigyRuntime SHARED src/ProdigyRuntime.cpp)

# 安装目标
install(TARGETS ProdigyPass LIBRARY DESTINATION lib)
install(TARGETS ProdigyRuntime LIBRARY DESTINATION lib)
install(FILES include/ProdigyRuntime.h DESTINATION include) 