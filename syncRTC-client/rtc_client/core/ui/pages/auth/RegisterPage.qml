import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal backRequested()
    signal registerRequested(string username, string email, string code, string password, string comfirm)
    signal verificationCodeRequested(string email)
    signal loginRequested()

    property string usernameError: ""
    property string emailError: ""
    property string codeError: ""
    property string passwordError: ""
    property string confirmPasswordError: ""
    property string noticeMessage: ""
    property bool noticeIsError: false
    property int registerReturnSeconds: 0

    function showNotice(message, isError) {
        noticeMessage = message
        noticeIsError = isError
    }

    function showRegisterReturnCountdown() {
        showNotice("注册成功，" + registerReturnSeconds + " 秒后返回登录", false)
    }

    function isValidEmail(value) {
        return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
    }

    function isValidPassword(value) {
        return value.length >= 6 && /[A-Za-z]/.test(value) && /\d/.test(value)
    }

    function isValidCode(value) {
        return /^[A-Za-z0-9]{4,6}$/.test(value)
    }

    function validateEmailForCode() {
        var email = emailInput.text.trim()
        emailError = email.length === 0 ? "请输入邮箱" : !isValidEmail(email) ? "请输入正确的邮箱地址" : ""
        return emailError.length === 0
    }

    function validateForm() {
        var username = usernameInput.text.trim()
        var email = emailInput.text.trim()
        var code = codeInput.text.trim()

        usernameError = username.length === 0 ? "请输入用户名" : username.length < 2 ? "用户名至少 2 位" : ""
        emailError = email.length === 0 ? "请输入邮箱" : !isValidEmail(email) ? "请输入正确的邮箱地址" : ""
        codeError = code.length === 0 ? "请输入验证码" : !isValidCode(code) ? "验证码应为 4-6 位数字或字母" : ""
        passwordError = passwordInput.text.length === 0 ? "请输入密码" : !isValidPassword(passwordInput.text) ? "密码至少 6 位，且包含英文和数字" : ""
        confirmPasswordError = confirmPasswordInput.text.length === 0 ? "请再次输入密码" : confirmPasswordInput.text !== passwordInput.text ? "两次输入的密码不一致" : ""

        return usernameError.length === 0
            && emailError.length === 0
            && codeError.length === 0
            && passwordError.length === 0
            && confirmPasswordError.length === 0
    }

    Timer {
        id: registerSuccessTimer
        interval: 1000
        repeat: true
        onTriggered: {
            registerReturnSeconds -= 1
            if (registerReturnSeconds <= 0) {
                stop()
                root.loginRequested()
                return
            }
            root.showRegisterReturnCountdown()
        }
    }

    Connections {
        target: authController.registercontroller

        function onVerifyCodeSent(email) {
            root.showNotice("验证码已发送到邮箱：" + email, false)
        }

        function onVerifyCodeFailed(reason) {
            root.showNotice(reason, true)
        }

        function onRegisterSucceeded(username) {
            registerReturnSeconds = 5
            root.showRegisterReturnCountdown()
            registerSuccessTimer.restart()
        }

        function onRegisterFailed(reason) {
            registerSuccessTimer.stop()
            root.showNotice(reason, true)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"

        Rectangle {
            anchors.centerIn: parent
            width: 500
            height: 760
            radius: 28
            color: "#ffffff"
            border.color: "#e2e8f0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 42
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "创建账号"
                        color: "#0f172a"
                        font.pixelSize: 30
                        font.bold: true
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "返回"
                        color: "#2563eb"
                        font.pixelSize: 14
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.backRequested()
                        }
                    }
                }

                Text {
                    text: "注册 SyncRTC 后即可创建会议、保存聊天记录和查看 AI 纪要。"
                    color: "#64748b"
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                }

                Rectangle {
                    visible: noticeMessage.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: noticeText.implicitHeight + 18
                    radius: 12
                    color: noticeIsError ? "#fef2f2" : "#eff6ff"
                    border.color: noticeIsError ? "#fecaca" : "#bfdbfe"

                    Text {
                        id: noticeText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        color: noticeIsError ? "#b91c1c" : "#1d4ed8"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        text: noticeMessage
                    }
                }

                TextField {
                    id: usernameInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "用户名"
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 16
                    topPadding: 0
                    bottomPadding: 0
                    onTextChanged: usernameError = ""
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: usernameError.length > 0 ? "#ef4444" : usernameInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                FieldErrorText {
                    message: usernameError
                }

                TextField {
                    id: emailInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "邮箱"
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 16
                    topPadding: 0
                    bottomPadding: 0
                    onTextChanged: emailError = ""
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: emailError.length > 0 ? "#ef4444" : emailInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                FieldErrorText {
                    message: emailError
                }

                EmailCodeInput {
                    id: codeInput
                    Layout.fillWidth: true
                    errorText: codeError
                    onTextChanged: codeError = ""
                    onRequestCode: {
                        if (validateEmailForCode()) {
                            root.verificationCodeRequested(emailInput.text.trim())
                        }
                    }
                }

                PasswordField {
                    id: passwordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "设置密码"
                    hasError: passwordError.length > 0
                    onTextChanged: {
                        passwordError = ""
                        if (confirmPasswordError.length > 0) {
                            confirmPasswordError = ""
                        }
                    }
                }

                FieldErrorText {
                    message: passwordError
                }

                PasswordField {
                    id: confirmPasswordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "确认密码"
                    hasError: confirmPasswordError.length > 0
                    onTextChanged: confirmPasswordError = ""
                }

                FieldErrorText {
                    message: confirmPasswordError
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.topMargin: 10
                    text: "注册"
                    onClicked: {
                        if (validateForm()) {
                            root.registerRequested(usernameInput.text.trim(), emailInput.text.trim(), codeInput.text.trim(), passwordInput.text.trim(), confirmPasswordInput.text.trim())
                        }
                    }
                }

                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    spacing: 4

                    Text {
                        text: "已有账号？"
                        color: "#64748b"
                        font.pixelSize: 14
                    }

                    Text {
                        text: "返回登录"
                        color: "#2563eb"
                        font.pixelSize: 14
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.loginRequested()
                        }
                    }
                }
            }
        }
    }
}
