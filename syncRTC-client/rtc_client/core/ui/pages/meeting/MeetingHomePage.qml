import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../auth/components"

Item {
    id: root

    property var meetings
    property string joinError: ""

    signal joinMeetingRequested(string meetingId)
    signal actionRequested(string action)

    function greeting() {
        var hour = new Date().getHours()
        if (hour < 12)
            return "上午好，准备开始会议了吗？"
        if (hour < 18)
            return "下午好，准备开始会议了吗？"
        return "晚上好，准备开始会议了吗？"
    }

    function submitMeetingId() {
        var meetingId = meetingIdInput.text.trim()
        joinError = meetingId.length === 0 ? "请输入会议号" : !/^\d{6,12}$/.test(meetingId) ? "会议号应为 6-12 位数字" : ""
        if (joinError.length === 0)
            root.joinMeetingRequested(meetingId)
    }

    // 首页使用固定工作台布局，只有“最近会议”列表允许在自身区域内滚动。
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 48
        anchors.rightMargin: 56
        anchors.topMargin: 36
        anchors.bottomMargin: 32
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            spacing: 3

            Text {
                text: root.greeting()
                color: "#0f172a"
                font.pixelSize: 31
                font.bold: true
            }

            Text {
                text: "创建会议、加入会议，或查看最近的协作安排。"
                color: "#64748b"
                font.pixelSize: 15
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 142
            spacing: 18

            Repeater {
                model: [
                    { "title": "发起会议", "subtitle": "立即创建一个新的房间", "color": "#2563eb" },
                    { "title": "加入会议", "subtitle": "输入会议号快速入会", "color": "#0ea5e9" },
                    { "title": "预约会议", "subtitle": "稍后开始并通知成员", "color": "#10b981" }
                ]

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: modelData.color

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 22
                        spacing: 4

                        Text {
                            text: modelData.title
                            color: "#ffffff"
                            font.pixelSize: 23
                            font.bold: true
                        }

                        Text {
                            text: modelData.subtitle
                            color: "#e0f2fe"
                            font.pixelSize: 14
                        }

                        Item { Layout.fillHeight: true }

                        Text {
                            text: modelData.title === "加入会议" ? "在下方输入会议号" : "点击开始"
                            color: "#ffffff"
                            font.pixelSize: 13
                            opacity: 0.86
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.title === "加入会议")
                                meetingIdInput.forceActiveFocus()
                            else
                                root.actionRequested(modelData.title)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 292
            spacing: 20

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 16

                    Text {
                        text: "最近会议"
                        color: "#0f172a"
                        font.pixelSize: 23
                        font.bold: true
                    }

                    ListView {
                        id: recentMeetingList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.meetings
                        spacing: 10
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Rectangle {
                            required property string title
                            required property string meetingId
                            required property string schedule
                            required property string participants
                            required property string status
                            required property color statusColor

                            width: recentMeetingList.width
                            height: 88
                            radius: 8
                            color: "#f8fafc"
                            border.color: "#dbe3ef"

                            ColumnLayout {
                                anchors.left: parent.left
                                anchors.right: enterButton.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 16
                                anchors.rightMargin: 12
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    color: "#0f172a"
                                    font.pixelSize: 16
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Row {
                                    spacing: 8

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: statusColor
                                    }

                                    Text {
                                        text: schedule + "  " + participants + " · " + status
                                        color: "#64748b"
                                        font.pixelSize: 13
                                    }
                                }
                            }

                            Rectangle {
                                id: enterButton

                                width: 76
                                height: 34
                                radius: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                anchors.verticalCenter: parent.verticalCenter
                                color: enterMouseArea.containsMouse ? "#dbeafe" : "#eff6ff"

                                Text {
                                    anchors.centerIn: parent
                                    text: "进入"
                                    color: "#2563eb"
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                MouseArea {
                                    id: enterMouseArea

                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.joinMeetingRequested(meetingId)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: Math.max(300, Math.min(390, root.width * 0.34))
                Layout.fillHeight: true
                radius: 10
                color: "#ffffff"
                border.color: "#e2e8f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 16

                    Item { Layout.fillHeight: true }

                    Text {
                        text: "加入会议"
                        color: "#0f172a"
                        font.pixelSize: 23
                        font.bold: true
                    }

                    Text {
                        text: "会议号"
                        color: "#475569"
                        font.pixelSize: 14
                    }

                    TextField {
                        id: meetingIdInput

                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        placeholderText: "输入 6-12 位会议号"
                        font.pixelSize: 15
                        inputMethodHints: Qt.ImhDigitsOnly
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: 16
                        rightPadding: 16
                        topPadding: 0
                        bottomPadding: 0
                        onTextChanged: root.joinError = ""
                        onAccepted: root.submitMeetingId()

                        background: Rectangle {
                            radius: 8
                            color: "#f8fafc"
                            border.color: root.joinError.length > 0 ? "#ef4444" : meetingIdInput.activeFocus ? "#2563eb" : "#dbe3ef"
                        }
                    }

                    Text {
                        visible: root.joinError.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? implicitHeight : 0
                        text: root.joinError
                        color: "#dc2626"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    PrimaryButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        radius: 8
                        text: "加入会议"
                        onClicked: root.submitMeetingId()
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
