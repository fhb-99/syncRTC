import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal backRequested()
    signal registerFinished()
    signal loginRequested()

    property string usernameError: ""
    property string emailError: ""
    property string passwordError: ""
    property string confirmPasswordError: ""

    function isValidEmail(value) {
        return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
    }

    function isValidPassword(value) {
        return value.length >= 6 && /[A-Za-z]/.test(value) && /\d/.test(value)
    }

    function validateForm() {
        var username = usernameInput.text.trim()
        var email = emailInput.text.trim()

        usernameError = username.length === 0 ? "请输入用户名" : username.length < 2 ? "用户名至少 2 位" : ""
        emailError = email.length === 0 ? "请输入邮箱" : !isValidEmail(email) ? "请输入正确的邮箱地址" : ""
        passwordError = passwordInput.text.length === 0 ? "请输入密码" : !isValidPassword(passwordInput.text) ? "密码至少 6 位，且包含英文和数字" : ""
        confirmPasswordError = confirmPasswordInput.text.length === 0 ? "请再次输入密码" : confirmPasswordInput.text !== passwordInput.text ? "两次输入的密码不一致" : ""

        return usernameError.length === 0
            && emailError.length === 0
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

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.topMargin: 10
                    text: "注册"
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
                            root.registerFinished()
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
