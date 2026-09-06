import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string username: ""
    property string email: ""
    property bool microphoneEnabled: true
    property bool cameraEnabled: false
    property bool desktopNotificationEnabled: true
    property bool darkAppearanceEnabled: false

    signal demonstrationAction(string message)

    property string displayUsername: username.trim().length > 0 ? username : "当前用户"
    property string displayEmail: email.trim().length > 0 ? email : "未提供邮箱"

    // 以下四个布尔值只保存当前页面的本地展示状态，不写入配置文件，也不会控制真实硬件或系统通知。
    // microphoneEnabled、cameraEnabled、desktopNotificationEnabled、darkAppearanceEnabled 仅用于开关的 UI 演示。

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    ScrollView {
        id: settingsScroll

        anchors.fill: parent
        clip: true

        ColumnLayout {
            x: 40
            y: 40
            width: Math.max(620, settingsScroll.availableWidth - 80)
            spacing: 24

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "设置"
                    color: "#0f172a"
                    font.pixelSize: 31
                    font.bold: true
                }

                Text {
                    text: "管理账号信息、音视频设备、通知和外观偏好。当前均为界面展示，不会保存设置。"
                    color: "#64748b"
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 198
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 14

                    Text {
                        text: "账号"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 24
                            color: "#dbeafe"

                            Text {
                                anchors.centerIn: parent
                                text: root.displayUsername.charAt(0).toUpperCase()
                                color: "#2563eb"
                                font.pixelSize: 19
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                Layout.fillWidth: true
                                text: root.displayUsername
                                color: "#0f172a"
                                font.pixelSize: 17
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.displayEmail
                                color: "#64748b"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }

                        Button {
                            id: manageAccountButton

                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 38
                            text: "管理账号"
                            font.pixelSize: 14
                            font.bold: true
                            onClicked: root.demonstrationAction("账号设置仅为界面演示，功能尚未接入")

                            background: Rectangle {
                                radius: 8
                                color: manageAccountButton.down ? "#dbeafe" : manageAccountButton.hovered ? "#eff6ff" : "#f4f8ff"
                                border.color: "#bfdbfe"
                            }

                            contentItem: Text {
                                text: manageAccountButton.text
                                color: "#2563eb"
                                font: manageAccountButton.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 242
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 10

                    Text {
                        text: "设备"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    // 麦克风和摄像头名称是固定展示数据，后续接入设备枚举后再替换为真实结果。
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 68
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "麦克风"
                                color: "#0f172a"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                text: "系统默认麦克风"
                                color: "#64748b"
                                font.pixelSize: 13
                            }
                        }

                        Switch {
                            id: microphoneSwitch

                            checked: root.microphoneEnabled
                            onToggled: root.microphoneEnabled = checked
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#e2e8f0"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 68
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "摄像头"
                                color: "#0f172a"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                text: "系统默认摄像头"
                                color: "#64748b"
                                font.pixelSize: 13
                            }
                        }

                        Switch {
                            id: cameraSwitch

                            checked: root.cameraEnabled
                            onToggled: root.cameraEnabled = checked
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 152
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 10

                    Text {
                        text: "通知"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "桌面通知"
                                color: "#0f172a"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                text: "会议开始或被邀请时显示提醒"
                                color: "#64748b"
                                font.pixelSize: 13
                            }
                        }

                        Switch {
                            id: desktopNotificationSwitch

                            checked: root.desktopNotificationEnabled
                            onToggled: root.desktopNotificationEnabled = checked
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 152
                Layout.bottomMargin: 40
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 10

                    Text {
                        text: "外观"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "深色外观"
                                color: "#0f172a"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                text: root.darkAppearanceEnabled ? "已开启（仅记录当前界面状态）" : "未开启（仅记录当前界面状态）"
                                color: "#64748b"
                                font.pixelSize: 13
                            }
                        }

                        Switch {
                            id: darkAppearanceSwitch

                            checked: root.darkAppearanceEnabled
                            onToggled: root.darkAppearanceEnabled = checked
                        }
                    }
                }
            }
        }
    }
}
