cmake_minimum_required(VERSION 3.16)

project(rtc_client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Svg)

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
        core/ui/pages/meeting/MeetingHomePage.qml
        core/ui/pages/meeting/MeetingHistoryPage.qml
        core/ui/pages/meeting/ContactsPage.qml
        core/ui/pages/meeting/AiAssistantPage.qml
        core/ui/pages/meeting/SettingsPage.qml
        core/ui/pages/meeting/ModulePlaceholderPage.qml
        core/ui/pages/meeting/UserProfileDialog.qml
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
        RESOURCES config/config.ini
        SOURCES core/src/models/Data.h
        SOURCES core/src/network/tcpmgr.h core/src/network/tcpmgr.cpp
        SOURCES core/src/controllers/meeting/realtimecontroller.h core/src/controllers/meeting/realtimecontroller.cpp
        SOURCES core/src/controllers/meeting/profilecontroller.h core/src/controllers/meeting/profilecontroller.cpp
        SOURCES core/src/models/currentuserstate.h core/src/models/currentuserstate.cpp
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
)

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
        core/src/controllers/meeting/realtimecontroller.h
        core/src/controllers/meeting/realtimecontroller.cpp
    )

    target_link_libraries(realtimecontroller_test
        PRIVATE
            Qt6::Test
            Qt6::Network
    )

    add_test(NAME realtimecontroller_test COMMAND realtimecontroller_test)
endif()

include(GNUInstallDirs)
install(TARGETS apprtc_client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
