cmake_minimum_required(VERSION 3.16)

project(rtc_client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Svg Network)

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
        SOURCES core/src/controllers/meeting/meetingcontroller.h core/src/controllers/meeting/meetingcontroller.cpp
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
)

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
endif()

include(GNUInstallDirs)
install(TARGETS apprtc_client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
