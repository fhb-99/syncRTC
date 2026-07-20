cmake_minimum_required(VERSION 3.20)

project(RealtimeServer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

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

set(REAL_SERVER_SOURCES
    RealtimeServer.cpp
    src/config/ConfigMgr.cpp
    src/net/CServer.cpp
    src/net/Session.cpp
    src/service/LogicSystem.cpp
)

add_executable(RealtimeServer ${REAL_SERVER_SOURCES})

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Threads REQUIRED)

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
        Boost::filesystem
        Boost::system
        Threads::Threads
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
        src/net/Session.cpp
        src/service/LogicSystem.cpp
    )
    target_include_directories(session_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(session_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(session_test PRIVATE Threads::Threads)
    add_test(NAME session_test COMMAND session_test)

    add_executable(logic_system_test
        tests/LogicSystemTest.cpp
        src/net/Session.cpp
        src/service/LogicSystem.cpp
    )
    target_include_directories(logic_system_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(logic_system_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(logic_system_test PRIVATE Threads::Threads)
    add_test(NAME logic_system_test COMMAND logic_system_test)
endif()
