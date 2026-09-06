cmake_minimum_required(VERSION 3.20)

project(GateServer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

# Keep the Linux build reproducible and compatible with the project design.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Do not place generated binaries under build/. bin/ is the local runtime directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(CONFIGURATION Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIGURATION}" CONFIGURATION_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIGURATION_UPPER}
        "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

option(GATE_SERVER_ENABLE_GRPC "Enable the C++ to Node/Python gRPC boundary" ON)
option(GATE_SERVER_ENABLE_MYSQL "Enable MySQL Connector/C++ support in GateServer" OFF)

# RedisMgr.cpp is a required GateServer source now; discard the old optional
# cache switch so a stale OFF value cannot mislead future configuration.
unset(GATE_SERVER_ENABLE_REDIS CACHE)

# Keep this list explicit. New .cpp files must be added here; do not use GLOB,
# otherwise adding a source file does not reliably trigger a CMake reconfigure.
set(GATE_SERVER_SOURCES
    GateServer.cpp
    src/config/ConfigMgr.cpp
    src/net/HttpConnection.cpp
    src/net/CServer.cpp
    src/net/AsioIOServicePool.cpp
    src/service/LogicSystem.cpp
    src/rpc/VarifyGrpcClient.cpp
    src/storage/RedisMgr.cpp
)

add_executable(GateServer ${GATE_SERVER_SOURCES})

# 定义项目根目录宏，路径用双引号包裹防止空格
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}")
target_compile_definitions(GateServer PRIVATE
    PROJECT_ROOT_DIR="${PROJECT_ROOT}"
)

target_include_directories(GateServer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/common"
)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system thread)
find_package(OpenSSL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_library(CRYPT_LIBRARY NAMES crypt REQUIRED)

target_link_libraries(GateServer
    PRIVATE
        Threads::Threads
        Boost::filesystem
        Boost::system
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        JsonCpp::JsonCpp
        ${CRYPT_LIBRARY}
)

if(GATE_SERVER_ENABLE_GRPC)
    find_package(Protobuf REQUIRED)
    find_package(gRPC CONFIG REQUIRED)

    target_link_libraries(GateServer
        PRIVATE
            gRPC::grpc++
            protobuf::libprotobuf
    )

    function(gate_server_add_proto PROTO_FILE)
        get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)
        set(PROTO_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/common")
        set(PROTO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
        set(PB_CC "${PROTO_SOURCE_DIR}/${PROTO_NAME}.pb.cc")
        set(PB_H  "${PROTO_HEADER_DIR}/${PROTO_NAME}.pb.h")
        set(GRPC_CC "${PROTO_SOURCE_DIR}/${PROTO_NAME}.grpc.pb.cc")
        set(GRPC_H  "${PROTO_HEADER_DIR}/${PROTO_NAME}.grpc.pb.h")

        add_custom_command(
            OUTPUT "${PB_CC}" "${PB_H}" "${GRPC_CC}" "${GRPC_H}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROTO_HEADER_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROTO_SOURCE_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${PB_H}" "${GRPC_H}"
            COMMAND "${Protobuf_PROTOC_EXECUTABLE}"
                --cpp_out=${PROTO_SOURCE_DIR}
                --grpc_out=${PROTO_SOURCE_DIR}
                --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                -I "${CMAKE_CURRENT_SOURCE_DIR}/proto"
                "${PROTO_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E rename
                "${PROTO_SOURCE_DIR}/${PROTO_NAME}.pb.h"
                "${PB_H}"
            COMMAND "${CMAKE_COMMAND}" -E rename
                "${PROTO_SOURCE_DIR}/${PROTO_NAME}.grpc.pb.h"
                "${GRPC_H}"
            DEPENDS "${PROTO_FILE}"
            COMMENT "Generating C++ and gRPC sources for ${PROTO_NAME}.proto"
            VERBATIM
        )

        target_sources(GateServer PRIVATE "${PB_CC}" "${GRPC_CC}")
    endfunction()

    # Define the function before calling it. Add every new proto explicitly.
    gate_server_add_proto("${CMAKE_CURRENT_SOURCE_DIR}/proto/message.proto")
endif()

# RedisMgr.cpp is now part of GateServer, so Redis++ must be linked for every
# GateServer build. Keeping it behind an OFF option compiles RedisMgr.cpp but
# omits libredis++ at link time, which causes undefined references.
# Ubuntu's libhiredis-dev exposes hiredis through pkg-config rather than a
# hiredisConfig.cmake package, so create an imported target explicitly.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
find_path(REDIS_PLUS_PLUS_INCLUDE_DIR
    NAMES sw/redis++/redis++.h
    HINTS /usr/local/include
    REQUIRED)
find_library(REDIS_PLUS_PLUS_LIBRARY
    NAMES redis++
    HINTS /usr/local/lib
    REQUIRED)

add_library(SyncRTC::RedisPlusPlus UNKNOWN IMPORTED)
set_target_properties(SyncRTC::RedisPlusPlus PROPERTIES
    IMPORTED_LOCATION "${REDIS_PLUS_PLUS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${REDIS_PLUS_PLUS_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "PkgConfig::HIREDIS"
)
target_link_libraries(GateServer PRIVATE SyncRTC::RedisPlusPlus)

if(GATE_SERVER_ENABLE_MYSQL)
    find_path(MYSQLCPPCONN_INCLUDE_DIR NAMES mysql_driver.h REQUIRED)
    find_library(MYSQLCPPCONN_LIBRARY NAMES mysqlcppconn REQUIRED)
    find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient REQUIRED)

    add_library(MySQL::ConnectorCpp UNKNOWN IMPORTED)
    set_target_properties(MySQL::ConnectorCpp PROPERTIES
        IMPORTED_LOCATION "${MYSQLCPPCONN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MYSQLCPPCONN_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${MYSQLCLIENT_LIBRARY}"
    )
    target_link_libraries(GateServer PRIVATE MySQL::ConnectorCpp)
    target_sources(GateServer PRIVATE src/storage/MySqlMgr.cpp)

    if(BUILD_TESTING)
        add_executable(mysql_mgr_registration_test
            tests/MysqlMgrRegistrationTest.cpp
            src/config/ConfigMgr.cpp
            src/storage/MySqlMgr.cpp
        )
        target_compile_definitions(mysql_mgr_registration_test PRIVATE
            PROJECT_ROOT_DIR="${PROJECT_ROOT}"
        )
        target_include_directories(mysql_mgr_registration_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/common"
        )
        target_link_libraries(mysql_mgr_registration_test PRIVATE
            Boost::filesystem
            MySQL::ConnectorCpp
        )
        add_test(NAME mysql_mgr_registration_test
                 COMMAND mysql_mgr_registration_test)

        add_executable(logic_system_token_test
            tests/LogicSystemTokenTest.cpp
            src/config/ConfigMgr.cpp
            src/net/HttpConnection.cpp
            src/rpc/VarifyGrpcClient.cpp
            src/service/LogicSystem.cpp
            src/storage/MySqlMgr.cpp
            src/storage/RedisMgr.cpp
            src/common/message.pb.cc
            src/common/message.grpc.pb.cc
        )
        target_compile_definitions(logic_system_token_test PRIVATE
            PROJECT_ROOT_DIR="${PROJECT_ROOT}"
        )
        target_include_directories(logic_system_token_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/common"
        )
        target_link_libraries(logic_system_token_test PRIVATE
            Threads::Threads
            Boost::filesystem
            Boost::system
            Boost::thread
            OpenSSL::SSL
            OpenSSL::Crypto
            JsonCpp::JsonCpp
            ${CRYPT_LIBRARY}
            SyncRTC::RedisPlusPlus
            MySQL::ConnectorCpp
            gRPC::grpc++
            protobuf::libprotobuf
        )
        add_test(NAME logic_system_token_test
                 COMMAND logic_system_token_test)
    endif()
endif()

target_compile_options(GateServer
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

# conf/ is copied beside the executable for local execution. Runtime secrets
# should be supplied through an ignored local config or environment variables.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/conf")
    add_custom_command(TARGET GateServer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:GateServer>/conf"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/conf"
            "$<TARGET_FILE_DIR:GateServer>/conf"
        COMMENT "Copying GateServer runtime configuration"
    )
endif()

include(GNUInstallDirs)
install(TARGETS GateServer
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/conf")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/conf/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/conf")
endif()
