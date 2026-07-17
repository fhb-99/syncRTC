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
                required property string title
                required property string schedule
                required property string participants
                required property string status
                required property color statusColor
                required property string summary

                width: historyList.width
                height: 118
                radius: 8
                color: "#ffffff"
                border.color: "#e2e8f0"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.fillHeight: true
                        radius: 5
                        color: statusColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: title
                            color: "#0f172a"
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: summary
                            color: "#64748b"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }

                        Text {
                            text: schedule + " · " + participants
                            color: "#94a3b8"
                            font.pixelSize: 13
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: status
                        color: statusColor
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
            }
        }
    }
}
