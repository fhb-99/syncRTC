import QtQuick
import QtQuick.Controls

Item {
    id: root

    property alias text: input.text
    property string placeholderText: ""
    property bool passwordVisible: true
    property bool hasError: false

    property url eyeSource: "qrc:/asserts/eye.svg"
    property url crossedEyeSource: "qrc:/asserts/crossed-eye.svg"

    signal accepted()

    implicitHeight: 52

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: "#f8fafc"
        border.color: root.hasError ? "#ef4444" : input.activeFocus ? "#2563eb" : "#dbe3ef"
        border.width: 1
    }

    TextField {
        id: input

        anchors.fill: parent
        placeholderText: root.placeholderText
        echoMode: root.passwordVisible ? TextInput.Normal : TextInput.Password
        font.pixelSize: 15
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: 16
        rightPadding: 52
        topPadding: 0
        bottomPadding: 0
        selectByMouse: true
        background: Item {}
        onAccepted: root.accepted()
    }

    Rectangle {
        id: eyeButton

        width: 34
        height: 34
        radius: 17
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        color: eyeMouseArea.containsMouse ? "#e8f0ff" : "transparent"

        Image {
            anchors.centerIn: parent
            width: 19
            height: 19
            source: root.passwordVisible ? eyeSource : crossedEyeSource
            opacity: eyeMouseArea.pressed ? 0.55 : 0.78
            fillMode: Image.PreserveAspectFit
        }

        MouseArea {
            id: eyeMouseArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.passwordVisible = !root.passwordVisible
                input.forceActiveFocus()
            }
        }
    }
}
