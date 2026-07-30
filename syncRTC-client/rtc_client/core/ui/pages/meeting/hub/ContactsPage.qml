pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal demonstrationAction(string message)

    // 以下联系人仅用于当前 UI 展示，后续由 UserService 的真实联系人数据替换。
    ListModel {
        id: contactsModel

        ListElement { name: "林秋"; role: "产品经理"; status: "在线"; statusColor: "#16a34a" }
        ListElement { name: "周言"; role: "客户端工程师"; status: "忙碌"; statusColor: "#f59e0b" }
        ListElement { name: "陈默"; role: "服务端工程师"; status: "在线"; statusColor: "#16a34a" }
        ListElement { name: "何清"; role: "交互设计师"; status: "离线"; statusColor: "#94a3b8" }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    // 使用固定工作台布局，联系人列表在自身区域内滚动，避免页面整体出现粗滚动条。
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 52
        anchors.rightMargin: 56
        anchors.topMargin: 38
        anchors.bottomMargin: 34
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            spacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "通讯录"
                    color: "#0f172a"
                    font.pixelSize: 31
                    font.bold: true
                }

                Text {
                    text: "查看已有联系人，快速发起会议邀请。"
                    color: "#64748b"
                    font.pixelSize: 15
                }
            }

            Button {
                id: addContactButton

                Layout.preferredWidth: 118
                Layout.preferredHeight: 42
                text: "添加联系人"
                font.pixelSize: 14
                font.bold: true
                onClicked: root.demonstrationAction("联系人功能仅为界面演示，尚未接入服务")

                background: Rectangle {
                    radius: 9
                    color: addContactButton.down ? "#1d4ed8" : addContactButton.hovered ? "#1e5ddd" : "#2563eb"
                }

                contentItem: Text {
                    text: addContactButton.text
                    color: "#ffffff"
                    font: addContactButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        TextField {
            id: contactSearchInput

            Layout.fillWidth: true
            Layout.preferredHeight: 50
            placeholderText: "搜索联系人（当前仅为界面展示，不执行筛选）"
            font.pixelSize: 15
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 16
            rightPadding: 16
            topPadding: 0
            bottomPadding: 0

            background: Rectangle {
                radius: 9
                color: "#ffffff"
                border.color: contactSearchInput.activeFocus ? "#2563eb" : "#dbe3ef"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 300
            radius: 10
            color: "#ffffff"
            border.color: "#e2e8f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "已有联系人"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "4 位联系人"
                        color: "#64748b"
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: contactsList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: contactsModel
                    spacing: 10

                    delegate: contactCard

                    ScrollBar.vertical: ScrollBar {
                        id: contactsScrollBar

                        policy: ScrollBar.AsNeeded
                        width: 8

                        background: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: "#eef3fb"
                        }

                        contentItem: Rectangle {
                            implicitWidth: 6
                            radius: 3
                            color: contactsScrollBar.pressed ? "#60a5fa" : contactsScrollBar.hovered ? "#93c5fd" : "#bfdbfe"
                        }
                    }
                }
            }
        }
    }

    Component {
        id: contactCard

        Rectangle {
            id: contactCardRoot

            required property string name
            required property string role
            required property string status
            required property color statusColor

            width: ListView.view.width
            height: 78
            radius: 9
            color: contactMouseArea.containsMouse ? "#f4f8ff" : "#f8fafc"
            border.color: "#dbe3ef"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.topMargin: 14
                anchors.bottomMargin: 14
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: "#dbeafe"

                    Text {
                        anchors.centerIn: parent
                        text: contactCardRoot.name.charAt(0)
                        color: "#2563eb"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.name
                        color: "#0f172a"
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.role
                        color: "#64748b"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.preferredWidth: 88
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 7

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: contactCardRoot.statusColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.status
                        color: "#64748b"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }

            MouseArea {
                id: contactMouseArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.demonstrationAction("联系人功能仅为界面演示，尚未接入服务")
            }
        }
    }
}
