import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var meetings

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 40
        spacing: 22

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                text: "历史会议"
                color: "#0f172a"
                font.pixelSize: 31
                font.bold: true
            }

            Text {
                text: "回顾已结束和已预约的会议安排。"
                color: "#64748b"
                font.pixelSize: 15
            }
        }

        ListView {
            id: historyList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.meetings
            spacing: 12

            delegate: Rectangle {
                required property var modelData

                width: historyList.width
                height: 128
                radius: 8
                color: "#ffffff"
                border.color: "#e2e8f0"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Rectangle {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        Layout.alignment: Qt.AlignVCenter
                        radius: width / 2
                        color: "#dbeafe"
                        clip: true

                        Image {
                            id: creatorAvatar

                            anchors.fill: parent
                            source: modelData.creatorAvatarUrl
                            fillMode: Image.PreserveAspectCrop
                            // URL 为空或加载失败时保留默认首字头像。
                            visible: status === Image.Ready
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: !creatorAvatar.visible
                            text: modelData.creatorName.charAt(0)
                            color: "#2563eb"
                            font.pixelSize: 18
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: "#0f172a"
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "创建者：" + modelData.creatorName
                            color: "#64748b"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.startedAt + " ～ " + modelData.endedAt
                            color: "#94a3b8"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: "会议号\n" + modelData.meetingCode
                        color: "#2563eb"
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }
    }
}
