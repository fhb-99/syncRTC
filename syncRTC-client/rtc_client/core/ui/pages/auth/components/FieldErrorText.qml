import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Label {
    id: root

    property string message: ""

    Layout.fillWidth: true
    Layout.preferredHeight: visible ? implicitHeight : 0
    visible: message.length > 0
    text: message
    color: "#dc2626"
    font.pixelSize: 12
    wrapMode: Text.WordWrap
}
