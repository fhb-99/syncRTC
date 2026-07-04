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
        core/ui/components/PasswordField.qml
        core/ui/pages/LoginPage.qml
        core/ui/pages/RegisterPage.qml
        core/ui/pages/ForgotPasswordPage.qml
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

include(GNUInstallDirs)
install(TARGETS apprtc_client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
