import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property alias text: codeInput.text
    property string errorText: ""
    property string placeholderText: "验证码"
    property string buttonText: "获取验证码"

    signal requestCode()

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        TextField {
            id: codeInput

            Layout.fillWidth: true
            Layout.preferredHeight: 52
            placeholderText: root.placeholderText
            font.pixelSize: 15
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 16
            topPadding: 0
            bottomPadding: 0
            selectByMouse: true
            onTextChanged: root.errorText = ""
            background: Rectangle {
                radius: 16
                color: "#f8fafc"
                border.color: root.errorText.length > 0 ? "#ef4444" : codeInput.activeFocus ? "#2563eb" : "#dbe3ef"
            }
        }

        Button {
            id: codeButton

            Layout.preferredWidth: 128
            Layout.preferredHeight: 52
            text: root.buttonText
            font.pixelSize: 14
            font.bold: true
            flat: true
            hoverEnabled: true
            contentItem: Text {
                text: codeButton.text
                color: "#2563eb"
                font: codeButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                anchors.fill: parent
                radius: 16
                color: codeButton.down ? "#dbeafe" : codeButton.hovered ? "#e0edff" : "#eff6ff"
                border.color: "#bfdbfe"
            }
            onClicked: root.requestCode()
        }
    }

    FieldErrorText {
        message: root.errorText
    }
}
