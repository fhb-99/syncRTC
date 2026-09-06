import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string title: ""
    property string description: ""

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 460)
        spacing: 10

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            color: "#0f172a"
            font.pixelSize: 30
            font.bold: true
        }

        Text {
            Layout.fillWidth: true
            text: root.description
            color: "#64748b"
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "功能开发中"
            color: "#2563eb"
            font.pixelSize: 14
            font.bold: true
        }
    }
}
