pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // 保留该信号以兼容 MeetingShell 的现有连接；当前页面已接入真实问答，不再发送演示提示。
    signal demonstrationAction(string message)

    property bool waitingForAnswer: false
    property string answerText: ""
    property string errorText: ""

    function submitQuestion() {
        var question = questionInput.text.trim()
        if (question.length === 0) {
            answerText = ""
            errorText = "请输入问题"
            questionInput.forceActiveFocus()
            return
        }
        if (waitingForAnswer)
            return

        errorText = ""
        answerText = ""
        waitingForAnswer = true
        // RealtimeController 负责复用已登录的 TCP 控制连接。
        realtimeController.askAiQuestion(question)
    }

    Connections {
        target: realtimeController

        function onAiAnswerReceived(answer) {
            root.answerText = answer
        }

        function onAiRequestFailed(error, message) {
            void(error)
            root.errorText = message
        }

        function onAiRequestFinished() {
            root.waitingForAnswer = false
        }
    }

    // 能力卡片保留为产品入口展示，当前只接通“会议问答”。
    ListModel {
        id: capabilityModel

        ListElement { title: "会议问答"; description: "直接提问并获得普通中文回答"; accentColor: "#2563eb" }
        ListElement { title: "行动项提取"; description: "识别讨论中的待跟进事项"; accentColor: "#0ea5e9" }
        ListElement { title: "知识检索"; description: "从团队资料中查找相关信息"; accentColor: "#10b981" }
    }

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    // 固定工作台占满可用区域，问答面板使用剩余空间显示较长答案。
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
                    text: "在会议应用中直接提问，获得普通中文回答。"
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
                onClicked: questionInput.forceActiveFocus()

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
                        onClicked: {
                            if (capabilityCard.title === "会议问答")
                                questionInput.forceActiveFocus()
                        }
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
                spacing: 14

                Text {
                    text: "会议问答"
                    color: "#0f172a"
                    font.pixelSize: 21
                    font.bold: true
                }

                TextArea {
                    id: questionInput

                    Layout.fillWidth: true
                    Layout.preferredHeight: 84
                    enabled: !root.waitingForAnswer
                    placeholderText: "请输入你想了解的问题"
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 15
                    color: "#0f172a"
                    background: Rectangle {
                        radius: 8
                        color: "#f8fafc"
                        border.color: "#dbe3ef"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: root.waitingForAnswer ? "正在请求 AI……" : root.errorText
                        color: root.errorText.length > 0 && !root.waitingForAnswer ? "#dc2626" : "#64748b"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Button {
                        id: sendQuestionButton

                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 38
                        text: root.waitingForAnswer ? "请求中" : "发送问题"
                        enabled: !root.waitingForAnswer
                        onClicked: root.submitQuestion()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe3ef"

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 14
                        clip: true

                        Text {
                            width: parent.width
                            text: root.answerText.length > 0 ? root.answerText : "AI 的回答会显示在这里"
                            color: root.answerText.length > 0 ? "#0f172a" : "#94a3b8"
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
