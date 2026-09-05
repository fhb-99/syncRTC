import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal backRequested()
    signal resetFinished()
    signal verificationCodeRequested(string email)
    signal resetPasswordRequested(string email, string code, string password, string confirm)
    signal loginRequested()

    property string emailError: ""
    property string codeError: ""
    property string passwordError: ""
    property string confirmPasswordError: ""
    property string noticeMessage: ""
    property bool noticeIsError: false

    function showNotice(message, isError) {
        noticeMessage = message
        noticeIsError = isError
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
        var email = accountInput.text.trim()
        emailError = email.length === 0 ? "请输入邮箱" : !isValidEmail(email) ? "请输入正确的邮箱地址" : ""
        return emailError.length === 0
    }

    function validateForm() {
        var email = accountInput.text.trim()
        var code = codeInput.text.trim()

        emailError = email.length === 0 ? "请输入邮箱" : !isValidEmail(email) ? "请输入正确的邮箱地址" : ""
        codeError = code.length === 0 ? "请输入验证码" : !isValidCode(code) ? "验证码应为 4-6 位数字或字母" : ""
        passwordError = passwordInput.text.length === 0 ? "请输入新密码" : !isValidPassword(passwordInput.text) ? "密码至少 6 位，且包含英文和数字" : ""
        confirmPasswordError = confirmPasswordInput.text.length === 0 ? "请再次输入新密码" : confirmPasswordInput.text !== passwordInput.text ? "两次输入的密码不一致" : ""

        return emailError.length === 0
            && codeError.length === 0
            && passwordError.length === 0
            && confirmPasswordError.length === 0
    }

    Connections {
        target: authController.passwordResetController

        function onVerifyCodeSent(email) {
            root.showNotice("验证码已发送到邮箱：" + email, false)
        }

        function onVerifyCodeFailed(reason) {
            root.showNotice(reason, true)
        }

        function onResetPasswordSucceeded() {
            root.showNotice("密码重置成功", false)
        }

        function onResetPasswordFailed(reason) {
            root.showNotice(reason, true)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"

        Rectangle {
            anchors.centerIn: parent
            width: 500
            height: 690
            radius: 28
            color: "#ffffff"
            border.color: "#e2e8f0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 42
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "找回密码"
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
                    text: "输入注册邮箱，验证通过后即可重置密码。"
                    color: "#64748b"
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
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
                    id: accountInput
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
                        border.color: emailError.length > 0 ? "#ef4444" : accountInput.activeFocus ? "#2563eb" : "#dbe3ef"
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
                            root.verificationCodeRequested(accountInput.text.trim())
                        }
                    }
                }

                PasswordField {
                    id: passwordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "新密码"
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
                    placeholderText: "确认新密码"
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
                    text: "重置密码"
                    onClicked: {
                        if (validateForm()) {
                            root.resetPasswordRequested(accountInput.text.trim(), codeInput.text.trim(), passwordInput.text.trim(), confirmPasswordInput.text.trim())
                        }
                    }
                }

                Row {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    spacing: 4

                    Text {
                        text: "想起密码了？"
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
