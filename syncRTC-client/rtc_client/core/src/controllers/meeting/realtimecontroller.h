#ifndef REALTIMECONTROLLER_H
#define REALTIMECONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <functional>
#include <memory>

#include "../../models/currentuserstate.h"
#include "../../models/global.h"
#include "../../network/tcpmgr.h"
#include "profilecontroller.h"
#include "meetingcontroller.h"
#include "chatcontroller.h"

class RealtimeController : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeController(CurrentUserState *currentUser, QObject *parent = nullptr);

    // 供 main.cpp 注入 QML；所有权仍属于 RealtimeController。
    MeetingController *meetingController() const { return m_meeting.get(); }
    ChatController *chatController() const { return m_chat.get(); }

signals:
    // 个人资料处理完成后通知 QML 切换到会议主界面
    void profileReady();
    void loginFailed(int error);

public slots:
    // 只接收 TcpMgr 回包，并根据 RequestID 分发给对应业务控制器
    void slot_message_recv(RequestID reqID, QJsonObject json);

private:
    using MessageHandler = std::function<void(const QJsonObject &json)>;

    void initHandlers();

    CurrentUserState *m_currentUser = nullptr;
    std::unique_ptr<ProfileController> m_profile;
    std::unique_ptr<MeetingController> m_meeting;
    std::unique_ptr<ChatController> m_chat;
    // 后续联系人、会议、AI 等控制器只需在此注册各自 RequestID 的处理器。
    QMap<RequestID, MessageHandler> m_handlers;
};

#endif // REALTIMECONTROLLER_H
