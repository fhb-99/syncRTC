cmake_minimum_required(VERSION 3.21)

project(MediaServer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_BUILD_TYPE Debug CACHE STRING "")
add_compile_options(-g -O0 -Wall -Wextra)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MediaServer 直接使用 libdatachannel 建立 PeerConnection；固定提交避免构建时拉到不兼容版本。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG 61204eb447916d259eea6e90f4a50c73d2d062e8
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(libdatachannel)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(CONFIGURATION Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIGURATION}" CONFIGURATION_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIGURATION_UPPER}
        "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

set(MEDIA_LOGIC_SOURCES
    src/config/ConfigMgr.cpp
    src/net/Session.cpp
    src/net/CServer.cpp
    src/service/LogicSystem.cpp
    src/media/MediaRoom.cpp
    src/media/MediaPeer.cpp
    src/media/MediaSession.cpp
)

add_executable(MediaServer
    MediaServer.cpp
    ${MEDIA_LOGIC_SOURCES}
)

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Threads REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

set(MEDIA_SERVER_LIBRARIES
    Threads::Threads
    Boost::filesystem
    Boost::system
    JsonCpp::JsonCpp
    LibDataChannel::LibDataChannel
)

target_compile_definitions(MediaServer
    PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(MediaServer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(MediaServer
    PRIVATE
        ${MEDIA_SERVER_LIBRARIES}
)

target_compile_options(MediaServer
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_custom_command(TARGET MediaServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:MediaServer>/conf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/conf/config.ini"
        "$<TARGET_FILE_DIR:MediaServer>/conf/config.ini"
    COMMENT "Copying MediaServer runtime configuration"
)

if(BUILD_TESTING)
    add_executable(rtp_session_test
        tests/RtpSessionTest.cpp
        src/config/ConfigMgr.cpp
        src/media/MediaRoom.cpp
        src/media/MediaPeer.cpp
        src/media/MediaSession.cpp
    )
    target_compile_definitions(rtp_session_test PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(rtp_session_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(rtp_session_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
    target_link_libraries(rtp_session_test PRIVATE ${MEDIA_SERVER_LIBRARIES})
    add_dependencies(rtp_session_test MediaServer)
    add_test(NAME rtp_session_test COMMAND rtp_session_test)
    set_tests_properties(rtp_session_test PROPERTIES TIMEOUT 30)

    add_executable(local_signal_test
        tests/LocalSignalTest.cpp
        ${MEDIA_LOGIC_SOURCES}
    )
    target_compile_definitions(local_signal_test PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(local_signal_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(local_signal_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
    target_link_libraries(local_signal_test PRIVATE ${MEDIA_SERVER_LIBRARIES})
    # 真实 MediaSession 需要服务构建后复制的运行配置。
    add_dependencies(local_signal_test MediaServer)
    add_test(NAME local_signal_test COMMAND local_signal_test)
    set_tests_properties(local_signal_test PROPERTIES TIMEOUT 30)

    add_executable(session_send_test
        tests/SessionSendTest.cpp
        ${MEDIA_LOGIC_SOURCES}
    )
    target_compile_definitions(session_send_test PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(session_send_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(session_send_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
    target_link_libraries(session_send_test PRIVATE ${MEDIA_SERVER_LIBRARIES})
    add_test(NAME session_send_test COMMAND session_send_test)
    set_tests_properties(session_send_test PROPERTIES TIMEOUT 20)

    add_executable(track_ready_test
        tests/TrackReadyTest.cpp
        ${MEDIA_LOGIC_SOURCES}
    )
    target_compile_definitions(track_ready_test PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(track_ready_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(track_ready_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
    target_link_libraries(track_ready_test PRIVATE ${MEDIA_SERVER_LIBRARIES})
    # 测试创建真实 MediaSession，需要服务构建后复制到 bin/conf 的运行配置。
    add_dependencies(track_ready_test MediaServer)
    add_test(NAME track_ready_test COMMAND track_ready_test)
    set_tests_properties(track_ready_test PROPERTIES TIMEOUT 30)

    add_executable(room_thread_pool_test
        tests/RoomThreadPoolTest.cpp
    )
    target_include_directories(room_thread_pool_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(room_thread_pool_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(room_thread_pool_test PRIVATE Threads::Threads)
    add_test(NAME room_thread_pool_test COMMAND room_thread_pool_test)

    add_executable(media_room_test
        tests/MediaRoomTest.cpp
        src/config/ConfigMgr.cpp
        src/media/MediaRoom.cpp
        src/media/MediaPeer.cpp
        src/media/MediaSession.cpp
    )
    target_compile_definitions(media_room_test
        PRIVATE
            PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_include_directories(media_room_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(media_room_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(media_room_test
        PRIVATE
            Boost::filesystem
            Boost::system
            LibDataChannel::LibDataChannel
    )
    add_test(NAME media_room_test COMMAND media_room_test)
endif()
