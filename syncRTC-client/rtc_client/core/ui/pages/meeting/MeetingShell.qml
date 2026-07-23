import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // 登录后直接绑定 C++ 注入的 currentUser，资料更新时各子页会自动刷新。
    property string previewUsername: ""
    property string previewEmail: ""
    readonly property string username: currentUser.username.length > 0 ? currentUser.username : previewUsername
    readonly property string email: currentUser.email.length > 0 ? currentUser.email : previewEmail
    property string currentSection: "home"
    property string toastMessage: ""

    function avatarText() {
        var name = username.trim()
        return name.length > 0 ? name.charAt(0).toUpperCase() : "U"
    }

    function showToast(message) {
        toastMessage = message
        toastTimer.restart()
    }

    function sectionIndex() {
        if (currentSection === "history")
            return 1
        if (currentSection === "contacts")
            return 2
        if (currentSection === "assistant")
            return 3
        if (currentSection === "settings")
            return 4
        return 0
    }

    // 以下条目为当前静态展示数据，后续应由真实会议 API 与信令服务替换。
    ListModel {
        id: meetingModel

        ListElement {
            title: "媒体转发服务评审"
            meetingId: "100001"
            schedule: "今天 14:00"
            participants: "4 人"
            status: "进行中"
            statusColor: "#16a34a"
            summary: "讨论媒体转发链路与性能指标"
        }

        ListElement {
            title: "AI 助手接口联调"
            meetingId: "100002"
            schedule: "明天 10:30"
            participants: "6 人"
            status: "已预约"
            statusColor: "#2563eb"
            summary: "确认实时摘要与待办同步接口"
        }

        ListElement {
            title: "客户端多线程模型讨论"
            meetingId: "100003"
            schedule: "周五 19:00"
            participants: "3 人"
            status: "已结束"
            statusColor: "#64748b"
            summary: "梳理 IO 线程与业务线程边界"
        }

        ListElement {
            title: "产品迭代同步会"
            meetingId: "100004"
            schedule: "周三 16:00"
            participants: "8 人"
            status: "已结束"
            statusColor: "#64748b"
            summary: "同步本周功能进展与下周计划"
        }
    }

    ListModel {
        id: navigationModel

        ListElement { key: "home"; title: "会议首页" }
        ListElement { key: "history"; title: "历史会议" }
        ListElement { key: "contacts"; title: "通讯录" }
        ListElement { key: "assistant"; title: "AI 助手" }
        ListElement { key: "settings"; title: "设置" }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar

            Layout.fillHeight: true
            Layout.preferredWidth: 248
            Layout.minimumWidth: 220
            color: "#ffffff"
            border.color: "#e2e8f0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 38
                    spacing: 2

                    Text {
                        text: "SyncRTC"
                        color: "#1d4ed8"
                        font.pixelSize: 30
                        font.bold: true
                    }

                    Text {
                        text: "轻量级视频会议系统"
                        color: "#64748b"
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: navigationList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: navigationModel
                    spacing: 8

                    delegate: Item {
                        required property string key
                        required property string title

                        width: navigationList.width
                        height: 48

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: root.currentSection === key ? "#eaf2ff" : navMouseArea.containsMouse ? "#f7faff" : "transparent"
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            text: title
                            color: root.currentSection === key ? "#2563eb" : "#475569"
                            font.pixelSize: 16
                            font.bold: root.currentSection === key
                        }

                        MouseArea {
                            id: navMouseArea

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentSection = key
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    Layout.topMargin: 18
                    radius: 8
                    color: profileMouseArea.containsMouse ? "#f4f8ff" : "#f8fafc"
                    border.color: "#dbe3ef"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 42
                            radius: 21
                            color: "#2563eb"

                            Text {
                                anchors.centerIn: parent
                                text: root.avatarText()
                                color: "#ffffff"
                                font.pixelSize: 17
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: root.username.length > 0 ? root.username : "用户"
                                color: "#0f172a"
                                font.pixelSize: 14
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "个人资料"
                                color: "#64748b"
                                font.pixelSize: 12
                            }
                        }
                    }

                    MouseArea {
                        id: profileMouseArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: profileDialog.open()
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                anchors.fill: parent
                currentIndex: root.sectionIndex()

                MeetingHomePage {
                    // 最近会议由 C++ MeetingController 提供，模型重置会自动刷新 ListView。
                    meetings: meetingController
                    onJoinMeetingRequested: function(meetingId) {
                        root.showToast("会议号 " + meetingId + " 已通过校验，正在准备加入会议")
                    }
                    onActionRequested: function(action) {
                        root.showToast(action + "功能开发中")
                    }
                }

                MeetingHistoryPage {
                    meetings: meetingController
                }

                ContactsPage {
                    onDemonstrationAction: function(message) {
                        root.showToast(message)
                    }
                }

                AiAssistantPage {
                    onDemonstrationAction: function(message) {
                        root.showToast(message)
                    }
                }

                SettingsPage {
                    username: root.username
                    email: root.email
                    onDemonstrationAction: function(message) {
                        root.showToast(message)
                    }
                }
            }
        }
    }

    Rectangle {
        id: toast

        visible: root.toastMessage.length > 0
        z: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: Math.min(520, toastLabel.implicitWidth + 40)
        height: 44
        radius: 8
        color: "#0f172a"

        Text {
            id: toastLabel

            anchors.centerIn: parent
            width: parent.width - 32
            text: root.toastMessage
            color: "#ffffff"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Timer {
        id: toastTimer

        interval: 2600
        repeat: false
        onTriggered: root.toastMessage = ""
    }

    UserProfileDialog {
        id: profileDialog

        username: root.username
        email: root.email
    }
}
