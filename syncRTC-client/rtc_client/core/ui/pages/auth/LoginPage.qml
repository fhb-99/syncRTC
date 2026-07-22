import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal loginRequested(string account, string password)
    signal loginSucceeded(string username, string email)
    signal registerRequested()
    signal forgotPasswordRequested()

    property string accountError: ""
    property string passwordError: ""
    property string noticeMessage: ""
    property bool noticeIsError: false
    property bool loginPending: false

    function showNotice(message, isError) {
        noticeMessage = message
        noticeIsError = isError
    }

    function validateForm() {
        accountError = accountInput.text.trim().length === 0 ? "请输入邮箱或用户名" : ""
        passwordError = passwordInput.text.length === 0 ? "请输入密码" : ""
        return accountError.length === 0 && passwordError.length === 0
    }

    function submitLogin() {
        if (!validateForm()) {
            return
        }

        noticeMessage = ""
        loginPending = true
        root.loginRequested(accountInput.text.trim(), passwordInput.text)
    }

    Connections {
        target: authController.loginController

        function onLoginSucceeded(username, email) {
            root.loginPending = false
            root.showNotice("登录成功，正在进入会议...", false)
            root.loginSucceeded(username, email)
        }

        function onLoginFailed(reason) {
            root.loginPending = false
            root.showNotice(reason, true)
        }
    }

    Connections {
        target: realtimeController

        function onProfileReady() {
            root.loginPending = false
            root.showNotice("登录成功，正在进入会议...", false)
            root.loginSucceeded(currentUser.username, currentUser.email)
        }

        function onLoginFailed(error) {
            root.loginPending = false
            root.showNotice("TCP 登录失败，错误码：" + error, true)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"

        Rectangle {
            width: parent.width * 0.42
            height: parent.height
            color: "#eff6ff"

            Rectangle {
                width: 520
                height: 520
                radius: 260
                x: -170
                y: parent.height - 310
                color: "#dbeafe"
            }

            Rectangle {
                width: 420
                height: 420
                radius: 210
                x: parent.width - 150
                y: 76
                color: "#e0f2fe"
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 76
                anchors.verticalCenter: parent.verticalCenter
                spacing: 22

                Text {
                    text: "SyncRTC"
                    color: "#1d4ed8"
                    font.pixelSize: 48
                    font.bold: true
                }

                Text {
                    width: 390
                    text: "轻量级视频会议系统与 AI 会议助手"
                    color: "#0f172a"
                    font.pixelSize: 28
                    font.bold: true
                    wrapMode: Text.WordWrap
                    lineHeight: 1.15
                }

                Text {
                    width: 390
                    text: "支持会议房间、实时聊天、音视频通话、实时字幕、会议纪要和待办事项提取。"
                    color: "#64748b"
                    font.pixelSize: 16
                    wrapMode: Text.WordWrap
                    lineHeight: 1.35
                }
            }
        }

        Rectangle {
            id: card
            width: 430
            height: 560
            radius: 28
            color: "#ffffff"
            border.color: "#e2e8f0"
            border.width: 1
            anchors.right: parent.right
            anchors.rightMargin: Math.max(80, parent.width * 0.1)
            anchors.verticalCenter: parent.verticalCenter

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 42
                spacing: 12

                Text {
                    text: "欢迎回来"
                    color: "#0f172a"
                    font.pixelSize: 30
                    font.bold: true
                }

                Text {
                    text: "登录后开始或加入一场会议"
                    color: "#64748b"
                    font.pixelSize: 15
                    Layout.bottomMargin: 8
                }

                Rectangle {
                    visible: root.noticeMessage.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: noticeText.implicitHeight + 18
                    radius: 12
                    color: root.noticeIsError ? "#fef2f2" : "#eff6ff"
                    border.color: root.noticeIsError ? "#fecaca" : "#bfdbfe"

                    Text {
                        id: noticeText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        color: root.noticeIsError ? "#b91c1c" : "#1d4ed8"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        text: root.noticeMessage
                    }
                }

                TextField {
                    id: accountInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    enabled: !root.loginPending
                    placeholderText: "邮箱"
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 0
                    bottomPadding: 0
                    onTextChanged: {
                        root.accountError = ""
                        if (root.noticeIsError) {
                            root.noticeMessage = ""
                        }
                    }
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: root.accountError.length > 0 ? "#ef4444" : accountInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                FieldErrorText {
                    message: root.accountError
                }

                PasswordField {
                    id: passwordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    enabled: !root.loginPending
                    placeholderText: "密码"
                    hasError: root.passwordError.length > 0
                    onTextChanged: {
                        root.passwordError = ""
                        if (root.noticeIsError) {
                            root.noticeMessage = ""
                        }
                    }
                    onAccepted: root.submitLogin()
                }

                FieldErrorText {
                    message: root.passwordError
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: -2

                    CheckBox {
                        id: rememberCheck
                        enabled: !root.loginPending
                        text: "记住登录状态"
                        checked: true
                        font.pixelSize: 13
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        opacity: root.loginPending ? 0.45 : 1.0
                        text: "忘记密码？"
                        color: "#2563eb"
                        font.pixelSize: 13
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.loginPending
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.forgotPasswordRequested()
                        }
                    }
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    enabled: !root.loginPending
                    text: root.loginPending ? "登录中..." : "登录"
                    onClicked: root.submitLogin()
                }

                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    spacing: 4

                    Text {
                        text: "还没有账号？"
                        color: "#64748b"
                        font.pixelSize: 14
                    }

                    Text {
                        opacity: root.loginPending ? 0.45 : 1.0
                        text: "立即注册"
                        color: "#2563eb"
                        font.pixelSize: 14
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.loginPending
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.registerRequested()
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
