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
    add_executable(media_room_test
        tests/MediaRoomTest.cpp
        src/media/MediaRoom.cpp
        src/media/MediaPeer.cpp
        src/media/MediaSession.cpp
    )
    target_include_directories(media_room_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_options(media_room_test
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
    target_link_libraries(media_room_test PRIVATE LibDataChannel::LibDataChannel)
    add_test(NAME media_room_test COMMAND media_room_test)
endif()
