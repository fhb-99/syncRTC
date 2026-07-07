import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root

    signal backRequested()
    signal registerFinished()
    signal loginRequested()

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"

        Rectangle {
            anchors.centerIn: parent
            width: 500
            height: 620
            radius: 28
            color: "#ffffff"
            border.color: "#e2e8f0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 42
                spacing: 16

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
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: usernameInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
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
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: emailInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                PasswordField {
                    id: passwordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "设置密码"
                }

                PasswordField {
                    id: confirmPasswordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "确认密码"
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
                    onClicked: root.registerFinished()
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
