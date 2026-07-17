#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QCoreApplication>
#include <QSettings>

#include "controllers/auth/AuthController.h"
#include "models/global.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QString dir = QDir::currentPath();
    QDir configDir(dir);
    const QString configFile = configDir.filePath("config/config.ini");

    QString GateServer_Host;
    QString GateServer_Port;

    QFile configFileObj(configFile);
    if (configFileObj.exists()) {
        QSettings settings(configFile, QSettings::IniFormat);
        // 正确兜底：找不到键给默认空字符串即可
        GateServer_Host = settings.value("GateServer/Host", "").toString().trimmed();
        GateServer_Port = settings.value("GateServer/Port", "").toString().trimmed();

        if(GateServer_Host.isEmpty() || GateServer_Port.isEmpty()) {
            qDebug() << "GateServer config missing. Keys:" << settings.allKeys();
        }
    } else {
        qDebug() << "配置文件不存在：" << configFile;
    }

    GateServer_URL = "http://" + GateServer_Host + ":" + GateServer_Port;
    qDebug() << "Gate Server Url: " << GateServer_URL;

    QQmlApplicationEngine engine;

    AuthController authController;
    engine.rootContext()->setContextProperty("authController", &authController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("rtc_client", "Main");

    return app.exec();
}
