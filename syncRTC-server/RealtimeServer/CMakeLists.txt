cmake_minimum_required(VERSION 3.20)

project(RealtimeServer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

# 放在CMake文件最顶部
set(CMAKE_BUILD_TYPE Debug CACHE STRING "")

# 强制生成调试符号、关闭编译器优化
add_compile_options(-g -O0 -Wall -Wextra)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Do not place generated binaries under build/. bin/ is the local runtime directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(CONFIGURATION Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIGURATION}" CONFIGURATION_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIGURATION_UPPER}
        "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

set(REALTIME_LOGIC_SOURCES
    src/config/ConfigMgr.cpp
    src/net/Session.cpp
    src/service/LogicSystem.cpp
    src/storage/RedisMgr.cpp
    src/storage/MySqlMgr.cpp
)

set(REAL_SERVER_SOURCES
    RealtimeServer.cpp
    src/net/CServer.cpp
    ${REALTIME_LOGIC_SOURCES}
)

add_executable(RealtimeServer ${REAL_SERVER_SOURCES})

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Threads REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

# Redis++ 在当前环境中没有 CMake 包，显式补齐它与 hiredis 的依赖关系。
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
find_path(REDIS_PLUS_PLUS_INCLUDE_DIR
    NAMES sw/redis++/redis++.h
    HINTS /usr/local/include
    REQUIRED
)
find_library(REDIS_PLUS_PLUS_LIBRARY
    NAMES redis++
    HINTS /usr/local/lib
    REQUIRED
)
add_library(SyncRTC::RedisPlusPlus UNKNOWN IMPORTED)
set_target_properties(SyncRTC::RedisPlusPlus PROPERTIES
    IMPORTED_LOCATION "${REDIS_PLUS_PLUS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${REDIS_PLUS_PLUS_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "PkgConfig::HIREDIS"
)

# LoginHandler 会通过 MysqlMgr 查询用户信息，链接 MySQL Connector/C++ 与 mysqlclient。
find_path(MYSQLCPPCONN_INCLUDE_DIR NAMES mysql_driver.h REQUIRED)
find_library(MYSQLCPPCONN_LIBRARY NAMES mysqlcppconn REQUIRED)
find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient REQUIRED)
add_library(MySQL::ConnectorCpp UNKNOWN IMPORTED)
set_target_properties(MySQL::ConnectorCpp PROPERTIES
    IMPORTED_LOCATION "${MYSQLCPPCONN_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MYSQLCPPCONN_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${MYSQLCLIENT_LIBRARY}"
)

set(REALTIME_LOGIC_LIBRARIES
    Threads::Threads
    Boost::filesystem
    Boost::system
    JsonCpp::JsonCpp
    SyncRTC::RedisPlusPlus
    MySQL::ConnectorCpp
)

# 让运行时配置读取到源码目录下构建后的 bin/conf/config.ini。
target_compile_definitions(RealtimeServer
    PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(RealtimeServer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(RealtimeServer
    PRIVATE
        ${REALTIME_LOGIC_LIBRARIES}
)

target_compile_options(RealtimeServer
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_custom_command(TARGET RealtimeServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:RealtimeServer>/conf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/conf/config.ini"
        "$<TARGET_FILE_DIR:RealtimeServer>/conf/config.ini"
    COMMENT "Copying RealtimeServer runtime configuration"
)

if(BUILD_TESTING)
    add_executable(session_test
        tests/SessionTest.cpp
        ${REALTIME_LOGIC_SOURCES}
    )
    target_compile_definitions(session_test
        PRIVATE
            PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(session_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(session_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(session_test PRIVATE ${REALTIME_LOGIC_LIBRARIES})
    add_test(NAME session_test COMMAND session_test)

    add_executable(logic_system_test
        tests/LogicSystemTest.cpp
        ${REALTIME_LOGIC_SOURCES}
    )
    target_compile_definitions(logic_system_test
        PRIVATE
            PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(logic_system_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(logic_system_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(logic_system_test PRIVATE ${REALTIME_LOGIC_LIBRARIES})
    add_test(NAME logic_system_test COMMAND logic_system_test)

    add_executable(meeting_info_test
        tests/MeetingInfoTest.cpp
    )
    target_include_directories(meeting_info_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(meeting_info_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(meeting_info_test PRIVATE MySQL::ConnectorCpp)
    add_test(NAME meeting_info_test COMMAND meeting_info_test)
endif()
