import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal backRequested()
    signal resetFinished()
    signal loginRequested()

    property string emailError: ""
    property string codeError: ""
    property string passwordError: ""
    property string confirmPasswordError: ""

    function isValidEmail(value) {
        return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
    }

    function isValidPassword(value) {
        return value.length >= 6 && /[A-Za-z]/.test(value) && /\d/.test(value)
    }

    function isValidCode(value) {
        return /^[A-Za-z0-9]{4,6}$/.test(value)
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    TextField {
                        id: codeInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        placeholderText: "验证码"
                        font.pixelSize: 15
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: 16
                        topPadding: 0
                        bottomPadding: 0
                        onTextChanged: codeError = ""
                        background: Rectangle {
                            radius: 16
                            color: "#f8fafc"
                            border.color: codeError.length > 0 ? "#ef4444" : codeInput.activeFocus ? "#2563eb" : "#dbe3ef"
                        }
                    }

                    Button {
                        Layout.preferredWidth: 128
                        Layout.preferredHeight: 52
                        text: "获取验证码"
                        font.pixelSize: 14
                        font.bold: true
                        contentItem: Text {
                            text: parent.text
                            color: "#2563eb"
                            font: parent.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 16
                            color: parent.hovered ? "#dbeafe" : "#eff6ff"
                            border.color: "#bfdbfe"
                        }
                    }
                }

                FieldErrorText {
                    message: codeError
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

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.topMargin: 10
                    text: "重置密码"
                    font.pixelSize: 17
                    font.bold: true
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 18
                        color: parent.down ? "#1d4ed8" : parent.hovered ? "#1e40af" : "#2563eb"
                    }
                    onClicked: {
                        if (validateForm()) {
                            root.resetFinished()
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
