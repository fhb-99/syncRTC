# Ubuntu 22.04 的 libgrpc++-dev 提供 pkg-config 文件，但没有上游项目常见的
# gRPCConfig.cmake。这里仅把发行版已经安装的库映射成项目现有 CMake 使用的
# 两个目标，不下载另一套 gRPC，也不改变业务源码的链接方式。
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)

if(NOT TARGET gRPC::grpc++)
    add_library(gRPC::grpc++ INTERFACE IMPORTED)
    set_property(TARGET gRPC::grpc++ PROPERTY
        INTERFACE_LINK_LIBRARIES PkgConfig::GRPCPP)
endif()

if(NOT TARGET gRPC::grpc_cpp_plugin)
    add_executable(gRPC::grpc_cpp_plugin IMPORTED)
    set_property(TARGET gRPC::grpc_cpp_plugin PROPERTY
        IMPORTED_LOCATION /usr/bin/grpc_cpp_plugin)
endif()

set(gRPC_FOUND TRUE)
