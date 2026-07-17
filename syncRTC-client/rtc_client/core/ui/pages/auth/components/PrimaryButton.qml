import QtQuick

Item {
    id: root

    property string text: ""
    property color normalColor: "#2563eb"
    property color hoverColor: "#1e40af"
    property color pressedColor: "#1d4ed8"
    property color disabledColor: "#93c5fd"
    property color textColor: "#ffffff"
    property int radius: 18
    property alias font: label.font

    signal clicked()

    implicitHeight: 52

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: !root.enabled ? root.disabledColor
              : mouseArea.pressed ? root.pressedColor
              : mouseArea.containsMouse ? root.hoverColor
              : root.normalColor
        clip: true
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.pixelSize: 17
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
