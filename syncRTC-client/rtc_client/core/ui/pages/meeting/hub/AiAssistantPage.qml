pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal demonstrationAction(string message)

    // 以下能力卡片为固定展示数据，当前不调用任何 AI 服务。
    ListModel {
        id: capabilityModel

        ListElement { title: "会议问答"; description: "围绕会议上下文快速获得答案"; accentColor: "#2563eb" }
        ListElement { title: "行动项提取"; description: "识别讨论中的待跟进事项"; accentColor: "#0ea5e9" }
        ListElement { title: "知识检索"; description: "从团队资料中查找相关信息"; accentColor: "#10b981" }
    }

    // 以下最近任务为静态展示数据，后续接入 AI 服务后替换为真实任务记录。
    ListModel {
        id: recentTasksModel

        ListElement { title: "产品需求澄清"; detail: "今天 09:30 · 准备就绪"; status: "已完成"; statusColor: "#16a34a" }
        ListElement { title: "接口设计咨询"; detail: "昨天 16:20 · 等待继续提问"; status: "进行中"; statusColor: "#2563eb" }
        ListElement { title: "会前资料整理"; detail: "周二 14:00 · 资料已收集"; status: "已完成"; statusColor: "#16a34a" }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    // 固定工作台占满可用区域，仅“最近任务”列表在自身区域内滚动。
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
                    text: "AI 助手"
                    color: "#0f172a"
                    font.pixelSize: 31
                    font.bold: true
                }

                Text {
                    text: "用智能问答、行动项和知识检索辅助会议协作。"
                    color: "#64748b"
                    font.pixelSize: 15
                }
            }

            Button {
                id: askQuestionButton

                Layout.preferredWidth: 108
                Layout.preferredHeight: 42
                text: "新建提问"
                font.pixelSize: 14
                font.bold: true
                onClicked: root.demonstrationAction("AI 助手功能仅为界面演示，尚未接入服务")

                background: Rectangle {
                    radius: 9
                    color: askQuestionButton.down ? "#1d4ed8" : askQuestionButton.hovered ? "#1e5ddd" : "#2563eb"
                }

                contentItem: Text {
                    text: askQuestionButton.text
                    color: "#ffffff"
                    font: askQuestionButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 154
            spacing: 18

            Repeater {
                model: capabilityModel

                delegate: Rectangle {
                    id: capabilityCard

                    required property string title
                    required property string description
                    required property color accentColor

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: capabilityMouseArea.containsMouse ? "#f4f8ff" : "#ffffff"
                    border.color: "#e2e8f0"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 6
                            radius: 3
                            color: capabilityCard.accentColor
                        }

                        Text {
                            text: capabilityCard.title
                            color: "#0f172a"
                            font.pixelSize: 19
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: capabilityCard.description
                            color: "#64748b"
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }

                    MouseArea {
                        id: capabilityMouseArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.demonstrationAction("AI 助手功能仅为界面演示，尚未接入服务")
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 292
            radius: 10
            color: "#ffffff"
            border.color: "#e2e8f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16

                Text {
                    text: "最近任务"
                    color: "#0f172a"
                    font.pixelSize: 21
                    font.bold: true
                }

                ListView {
                    id: recentTasksList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: recentTasksModel
                    spacing: 10

                    delegate: Rectangle {
                        id: taskCard

                        required property string title
                        required property string detail
                        required property string status
                        required property color statusColor

                        width: ListView.view.width
                        height: 78
                        radius: 9
                        color: taskMouseArea.containsMouse ? "#f4f8ff" : "#f8fafc"
                        border.color: "#dbe3ef"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            anchors.topMargin: 14
                            anchors.bottomMargin: 14
                            spacing: 14

                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 44
                                radius: 5
                                color: taskCard.statusColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 220
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: taskCard.title
                                    color: "#0f172a"
                                    font.pixelSize: 16
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: taskCard.detail
                                    color: "#64748b"
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.preferredWidth: 88
                                Layout.alignment: Qt.AlignVCenter
                                text: taskCard.status
                                color: taskCard.statusColor
                                font.pixelSize: 13
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        MouseArea {
                            id: taskMouseArea

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.demonstrationAction("AI 助手功能仅为界面演示，尚未接入服务")
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        id: tasksScrollBar

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
                            color: tasksScrollBar.pressed ? "#60a5fa" : tasksScrollBar.hovered ? "#93c5fd" : "#bfdbfe"
                        }
                    }
                }
            }
        }
    }
}
