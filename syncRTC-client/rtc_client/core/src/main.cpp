#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QPermissions>
#endif

#include "controllers/auth/AuthController.h"
#include "controllers/contacts/contactscontroller.h"
#include "controllers/meeting/realtimecontroller.h"
#include "media/mediacontroller.h"
#include "models/currentuserstate.h"
#include "models/clientsession.h"
#include "models/deviceidstore.h"
#include "models/global.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

#ifdef Q_OS_ANDROID
    // Android 6.0 及以上需要在运行时申请摄像头和麦克风权限。
    // 提前发起申请，避免进入会议后首次启动采集时因权限未授予而失败。
    const QCameraPermission cameraPermission;
    if (app.checkPermission(cameraPermission) == Qt::PermissionStatus::Undetermined) {
        app.requestPermission(cameraPermission, [](const QPermission &) {});
    }

    const QMicrophonePermission microphonePermission;
    if (app.checkPermission(microphonePermission) == Qt::PermissionStatus::Undetermined) {
        app.requestPermission(microphonePermission, [](const QPermission &) {});
    }
#endif

    QQmlApplicationEngine engine;

    ClientSession clientSession;
    AuthController authController(&clientSession);
    engine.rootContext()->setContextProperty("authController", &authController);

    QSettings deviceSettings(QSettings::IniFormat,
                             QSettings::UserScope,
                             QStringLiteral("SyncRTC"),
                             QStringLiteral("rtc_client"));
    QString device_id = DeviceIdStore::loadOrCreate(deviceSettings);

    clientSession.setDeviceId(device_id);

    const QString dir = QDir::currentPath();
    QDir configDir(dir);
    QString configFile = configDir.filePath("config/config.ini");
    if (!QFile::exists(configFile)) {
        // Android APK 没有可直接读取的程序当前目录，改用随应用打包的只读资源。
        configFile = QStringLiteral(":/qt/qml/rtc_client/config/config.ini");
    }

    QString GateServer_Host;
    QString GateServer_Port;

    if (QFile::exists(configFile)) {
        QSettings settings(configFile, QSettings::IniFormat);
        // 正确兜底：找不到键给默认空字符串即可
        GateServer_Host = settings.value("GateServer/Host", "").toString().trimmed();
        GateServer_Port = settings.value("GateServer/Port", "").toString().trimmed();
        // ICE 服务地址和 TURN 凭据由运行配置提供。MediaSession 位于独立 RTC 线程，
        // 因此在主线程读取一次后保存，避免工作线程重复访问配置文件。
        WebRtcStunHost = settings.value("WebRTC/StunHost", "").toString().trimmed();
        WebRtcStunPort = settings.value("WebRTC/StunPort", "").toString().trimmed();
        WebRtcTurnHost = settings.value("WebRTC/TurnHost", "").toString().trimmed();
        WebRtcTurnPort = settings.value("WebRTC/TurnPort", "").toString().trimmed();
        WebRtcTurnUsername = settings.value("WebRTC/TurnUsername", "").toString().trimmed();
        WebRtcTurnPassword = settings.value("WebRTC/TurnPassword", "").toString();

        if(GateServer_Host.isEmpty() || GateServer_Port.isEmpty()) {
            qDebug() << "GateServer config missing. Keys:" << settings.allKeys();
        }
    } else {
        qDebug() << "配置文件不存在：" << configFile;
    }

    GateServer_URL = "http://" + GateServer_Host + ":" + GateServer_Port;
    qDebug() << "Gate Server Url: " << GateServer_URL;



    CurrentUserState currentUser;
    RealtimeController realtimeController(&currentUser);
    ContactsController contactsController(&currentUser, &clientSession);
    MediaController mediaController;
    realtimeController.setMediaController(&mediaController);

    engine.rootContext()->setContextProperty("currentUser", &currentUser);
    engine.rootContext()->setContextProperty("realtimeController", &realtimeController);
    engine.rootContext()->setContextProperty("meetingController", realtimeController.meetingController());
    engine.rootContext()->setContextProperty("chatController", realtimeController.chatController());
    engine.rootContext()->setContextProperty("contactsController", &contactsController);
    engine.rootContext()->setContextProperty("mediaController", &mediaController);

    QObject::connect(authController.GetLoginControll(), &LoginController::signal_connect_tcp,
                     &currentUser, [&currentUser](const ServerInfo &server) {
                         currentUser.setUid(server.uid);
                     });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("rtc_client", "Main");

    return app.exec();
}
