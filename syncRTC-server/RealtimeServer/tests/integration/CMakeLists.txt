cmake_minimum_required(VERSION 3.21)
project(RealtimeIntegrationFixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
if(NOT MEDIA_BUILD OR NOT LIBDATACHANNEL_SOURCE)
    message(FATAL_ERROR "Provide MEDIA_BUILD and LIBDATACHANNEL_SOURCE from the completed MediaServer build")
endif()
set(RTC_BUILD "${MEDIA_BUILD}/_deps/libdatachannel-build")
add_executable(offer_fixture OfferFixture.cpp)
target_include_directories(offer_fixture PRIVATE "${LIBDATACHANNEL_SOURCE}/include")
target_compile_definitions(offer_fixture PRIVATE RTC_ENABLE_MEDIA=1 RTC_ENABLE_WEBSOCKET=1)
target_link_libraries(offer_fixture PRIVATE
    "${RTC_BUILD}/libdatachannel.a"
    "${RTC_BUILD}/deps/usrsctp/usrsctplib/libusrsctp.a"
    "${RTC_BUILD}/deps/libsrtp/libsrtp2.a"
    OpenSSL::SSL OpenSSL::Crypto
    "${RTC_BUILD}/deps/libjuice/libjuice.a"
    Threads::Threads)
