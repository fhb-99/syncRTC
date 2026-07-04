import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal loginRequested(string account, string password)
    signal registerRequested()
    signal forgotPasswordRequested()

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
            height: 520
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
                spacing: 18

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

                TextField {
                    id: accountInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "邮箱 / 用户名"
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 0
                    bottomPadding: 0
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: accountInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                TextField {
                    id: passwordInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    placeholderText: "密码"
                    echoMode: TextInput.Password
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 0
                    bottomPadding: 0
                    background: Rectangle {
                        radius: 16
                        color: "#f8fafc"
                        border.color: passwordInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: -2

                    CheckBox {
                        id: rememberCheck
                        text: "记住登录状态"
                        checked: true
                        font.pixelSize: 13
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "忘记密码？"
                        color: "#2563eb"
                        font.pixelSize: 13
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.forgotPasswordRequested()
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    text: "登录"
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
                    onClicked: root.loginRequested(accountInput.text, passwordInput.text)
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
                        text: "立即注册"
                        color: "#2563eb"
                        font.pixelSize: 14
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
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
