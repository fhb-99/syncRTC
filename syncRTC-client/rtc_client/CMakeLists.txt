cmake_minimum_required(VERSION 3.16)

project(rtc_client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option(SYNCRTC_ENABLE_FFMPEG "Enable FFmpeg codec dependencies" ON)

if(WIN32)
    set(SYNCRTC_LIBDATACHANNEL_ROOT "E:/deps/libdatachannel-0.24.5-mingw1120-static"
        CACHE PATH "libdatachannel install root")
    set(SYNCRTC_OPENSSL_ROOT "D:/Qt6/Tools/mingw1120_64/opt"
        CACHE PATH "OpenSSL root used by the Qt MinGW toolchain")

    if(EXISTS "${SYNCRTC_LIBDATACHANNEL_ROOT}/lib/cmake/LibDataChannel/LibDataChannelConfig.cmake")
        list(APPEND CMAKE_PREFIX_PATH "${SYNCRTC_LIBDATACHANNEL_ROOT}")
    endif()

    if(EXISTS "${SYNCRTC_OPENSSL_ROOT}/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "${SYNCRTC_OPENSSL_ROOT}" CACHE PATH "OpenSSL root" FORCE)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Svg Network Multimedia)

if(SYNCRTC_ENABLE_FFMPEG)
    find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swresample swscale)
    include_directories(${FFMPEG_INCLUDE_DIR})
endif()

if(NOT SYNCRTC_ENABLE_FFMPEG)
    message(FATAL_ERROR "The media stack requires SYNCRTC_ENABLE_FFMPEG=ON.")
endif()

find_package(OpenSSL REQUIRED)
find_package(LibDataChannel CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    find_package(libdatachannel CONFIG QUIET)
endif()
if(NOT TARGET LibDataChannel::LibDataChannel AND NOT TARGET datachannel)
    message(FATAL_ERROR
        "libdatachannel was not found. Install it with a CMake config package "
        "or pass its install prefix with -DCMAKE_PREFIX_PATH=<path>.")
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(apprtc_client
    core/src/main.cpp
)

qt_add_resources(apprtc_client "client_assets"
    PREFIX "/"
    BASE core
    FILES
        core/asserts/eye.svg
        core/asserts/crossed-eye.svg
)

qt_add_qml_module(apprtc_client
    URI rtc_client
    NO_GENERATE_QMLTYPES
    QML_FILES
        core/ui/Main.qml
        core/ui/pages/auth/LoginPage.qml
        core/ui/pages/auth/RegisterPage.qml
        core/ui/pages/auth/ForgotPasswordPage.qml
        core/ui/pages/auth/components/PasswordField.qml
        core/ui/pages/auth/components/FieldErrorText.qml
        core/ui/pages/auth/components/EmailCodeInput.qml
        core/ui/pages/auth/components/PrimaryButton.qml
        core/ui/pages/meeting/MeetingShell.qml
        core/ui/pages/meeting/hub/MeetingHomePage.qml
        core/ui/pages/meeting/hub/MeetingHistoryPage.qml
        core/ui/pages/meeting/hub/ContactsPage.qml
        core/ui/pages/meeting/hub/AiAssistantPage.qml
        core/ui/pages/meeting/hub/SettingsPage.qml
        core/ui/pages/meeting/room/MeetingRoomPage.qml
        core/ui/pages/meeting/dialogs/CreateMeetingDialog.qml
        core/ui/pages/meeting/dialogs/MeetingTimePickerPopup.qml
        core/ui/pages/meeting/dialogs/UserProfileDialog.qml
        core/ui/pages/meeting/common/ModulePlaceholderPage.qml
    SOURCES
        core/src/network/httpmgr.h
        core/src/network/httpmgr.cpp
        core/src/models/Singleton.h
        core/src/controllers/auth/AuthController.h
        core/src/controllers/auth/AuthController.cpp
        core/src/controllers/auth/LoginController.h
        core/src/controllers/auth/LoginController.cpp
        core/src/controllers/auth/RegisterController.h
        core/src/controllers/auth/RegisterController.cpp
        core/src/controllers/auth/PasswordResetController.h
        core/src/controllers/auth/PasswordResetController.cpp
        SOURCES core/src/models/global.h
        SOURCES core/src/models/deviceidstore.h core/src/models/deviceidstore.cpp
        SOURCES core/src/models/sessioncredentialstore.h core/src/models/sessioncredentialstore.cpp
        RESOURCES config/config.ini
        SOURCES core/src/models/Data.h
        SOURCES core/src/network/tcpmgr.h core/src/network/tcpmgr.cpp
        SOURCES core/src/controllers/meeting/realtimecontroller.h core/src/controllers/meeting/realtimecontroller.cpp
        SOURCES core/src/controllers/meeting/profilecontroller.h core/src/controllers/meeting/profilecontroller.cpp
        SOURCES core/src/models/currentuserstate.h core/src/models/currentuserstate.cpp
        SOURCES core/src/models/clientsession.h core/src/models/clientsession.cpp
        SOURCES core/src/controllers/meeting/meetingcontroller.h core/src/controllers/meeting/meetingcontroller.cpp
        SOURCES core/src/controllers/meeting/chatcontroller.h core/src/controllers/meeting/chatcontroller.cpp
        SOURCES core/src/controllers/contacts/contactscontroller.h core/src/controllers/contacts/contactscontroller.cpp
        SOURCES core/src/media/media_stack_probe.cpp
        SOURCES core/src/media/mediacontroller.h core/src/media/mediacontroller.cpp
        SOURCES core/src/media/mediasession.h core/src/media/mediasession.cpp
        SOURCES core/src/media/mediadevicecapture.h core/src/media/mediadevicecapture.cpp
        SOURCES core/src/media/mediastreamprocessor.h core/src/media/mediastreamprocessor.cpp
        SOURCES core/src/media/receive/remotemediareceiver.h core/src/media/receive/remotemediareceiver.cpp
        SOURCES core/src/media/receive/videoreceivepipeline.h core/src/media/receive/videoreceivepipeline.cpp
        SOURCES core/src/media/receive/audioreceivepipeline.h core/src/media/receive/audioreceivepipeline.cpp
        SOURCES core/src/network/mediatransportmgr.h core/src/network/mediatransportmgr.cpp
        SOURCES core/src/media/receive/avsyncscheduler.h core/src/media/receive/avsyncscheduler.cpp
        SOURCES core/src/media/render/remotevideorenderer.h core/src/media/render/remotevideorenderer.cpp
)

add_custom_command(TARGET apprtc_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:apprtc_client>/config"
    COMMENT "Copying runtime config files to output directory"
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(apprtc_client PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.apprtc_client
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(apprtc_client
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Network
        Qt6::Multimedia
)

if(SYNCRTC_ENABLE_FFMPEG)
    target_link_libraries(apprtc_client PRIVATE FFmpeg::FFmpeg)
endif()

if(TARGET LibDataChannel::LibDataChannel)
    target_link_libraries(apprtc_client PRIVATE LibDataChannel::LibDataChannel)
else()
    target_link_libraries(apprtc_client PRIVATE datachannel)
endif()

if(WIN32)
    target_link_libraries(apprtc_client PRIVATE Advapi32)
endif()

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(currentuserstate_test
        core/tests/currentuserstate_test.cpp
        core/src/models/currentuserstate.h
        core/src/models/currentuserstate.cpp
    )

    target_link_libraries(currentuserstate_test
        PRIVATE
            Qt6::Test
    )

    add_test(NAME currentuserstate_test COMMAND currentuserstate_test)

    qt_add_executable(clientsession_test
        core/tests/clientsession_test.cpp
        core/src/models/clientsession.h
        core/src/models/clientsession.cpp
    )

    target_link_libraries(clientsession_test
        PRIVATE
            Qt6::Test
    )

    add_test(NAME clientsession_test COMMAND clientsession_test)

    qt_add_executable(deviceidstore_test
        core/tests/deviceidstore_test.cpp
        core/src/models/deviceidstore.h
        core/src/models/deviceidstore.cpp
    )

    target_link_libraries(deviceidstore_test
        PRIVATE
            Qt6::Test
    )

    add_test(NAME deviceidstore_test COMMAND deviceidstore_test)

    qt_add_executable(sessioncredentialstore_test
        core/tests/sessioncredentialstore_test.cpp
        core/src/models/sessioncredentialstore.h
        core/src/models/sessioncredentialstore.cpp
    )

    target_link_libraries(sessioncredentialstore_test
        PRIVATE
            Qt6::Test
    )

    if(WIN32)
        target_link_libraries(sessioncredentialstore_test PRIVATE Advapi32)
    endif()

    add_test(NAME sessioncredentialstore_test COMMAND sessioncredentialstore_test)

    qt_add_executable(logincontroller_test
        core/tests/logincontroller_test.cpp
        core/src/controllers/auth/LoginController.h
        core/src/controllers/auth/LoginController.cpp
        core/src/models/Data.h
        core/src/models/Singleton.h
        core/src/models/deviceidstore.h
        core/src/models/deviceidstore.cpp
        core/src/models/clientsession.h
        core/src/models/clientsession.cpp
        core/src/models/global.h
        core/src/models/sessioncredentialstore.h
        core/src/models/sessioncredentialstore.cpp
        core/src/network/httpmgr.h
        core/src/network/httpmgr.cpp
        core/src/network/tcpmgr.h
        core/src/network/tcpmgr.cpp
    )

    target_link_libraries(logincontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Network
    )

    if(WIN32)
        target_link_libraries(logincontroller_test PRIVATE Advapi32)
    endif()

    add_test(NAME logincontroller_test COMMAND logincontroller_test)

    qt_add_executable(realtimecontroller_test
        core/tests/realtimecontroller_test.cpp
        core/src/models/currentuserstate.h
        core/src/models/currentuserstate.cpp
        core/src/models/Data.h
        core/src/models/Singleton.h
        core/src/models/global.h
        core/src/network/tcpmgr.h
        core/src/network/tcpmgr.cpp
        core/src/controllers/meeting/profilecontroller.h
        core/src/controllers/meeting/profilecontroller.cpp
        core/src/controllers/meeting/meetingcontroller.h
        core/src/controllers/meeting/meetingcontroller.cpp
        core/src/controllers/meeting/chatcontroller.h
        core/src/controllers/meeting/chatcontroller.cpp
        core/src/controllers/meeting/realtimecontroller.h
        core/src/controllers/meeting/realtimecontroller.cpp
    )

    target_link_libraries(realtimecontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Gui
            Qt6::Network
    )

    add_test(NAME realtimecontroller_test COMMAND realtimecontroller_test)

    qt_add_executable(meetingcontroller_test
        core/tests/meetingcontroller_test.cpp
        core/src/controllers/meeting/meetingcontroller.h
        core/src/controllers/meeting/meetingcontroller.cpp
        core/src/models/Data.h
        core/src/models/Singleton.h
        core/src/models/global.h
        core/src/network/tcpmgr.h
        core/src/network/tcpmgr.cpp
    )

    target_link_libraries(meetingcontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Gui
            Qt6::Network
    )

    add_test(NAME meetingcontroller_test COMMAND meetingcontroller_test)

    qt_add_executable(chatcontroller_test
        core/tests/chatcontroller_test.cpp
        core/src/controllers/meeting/chatcontroller.h
        core/src/controllers/meeting/chatcontroller.cpp
        core/src/models/Singleton.h
        core/src/models/global.h
        core/src/network/tcpmgr.h
        core/src/network/tcpmgr.cpp
    )

    target_link_libraries(chatcontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Gui
            Qt6::Network
    )

    add_test(NAME chatcontroller_test COMMAND chatcontroller_test)

    qt_add_executable(contactscontroller_test
        core/tests/contactscontroller_test.cpp
        core/src/controllers/contacts/contactscontroller.h
        core/src/controllers/contacts/contactscontroller.cpp
        core/src/models/Singleton.h
        core/src/models/currentuserstate.h
        core/src/models/currentuserstate.cpp
        core/src/models/clientsession.h
        core/src/models/clientsession.cpp
        core/src/models/global.h
        core/src/network/httpmgr.h
        core/src/network/httpmgr.cpp
    )

    target_link_libraries(contactscontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Network
    )

    add_test(NAME contactscontroller_test COMMAND contactscontroller_test)

    qt_add_executable(remotevideorenderer_test
        core/tests/remotevideorenderer_test.cpp
        core/src/media/render/remotevideorenderer.h
        core/src/media/render/remotevideorenderer.cpp
    )

    target_link_libraries(remotevideorenderer_test
        PRIVATE
            Qt6::Test
            Qt6::Multimedia
    )

    add_test(NAME remotevideorenderer_test COMMAND remotevideorenderer_test)
endif()

include(GNUInstallDirs)
install(TARGETS apprtc_client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
