import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property string username: ""
    property string email: ""

    modal: true
    focus: true
    title: "个人资料"
    width: 380
    height: 310
    anchors.centerIn: parent
    padding: 0

    function avatarText() {
        var name = username.trim()
        return name.length > 0 ? name.charAt(0).toUpperCase() : "U"
    }

    background: Rectangle {
        radius: 12
        color: "#ffffff"
        border.color: "#dbe3ef"
    }

    header: Text {
        leftPadding: 28
        rightPadding: 28
        topPadding: 24
        bottomPadding: 4
        text: root.title
        color: "#0f172a"
        font.pixelSize: 22
        font.bold: true
    }

    contentItem: ColumnLayout {
        spacing: 14

        Item { Layout.preferredHeight: 2 }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 72
            Layout.preferredHeight: 72
            radius: 36
            color: "#2563eb"

            Text {
                anchors.centerIn: parent
                text: root.avatarText()
                color: "#ffffff"
                font.pixelSize: 28
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            spacing: 5

            Text {
                text: "用户名"
                color: "#64748b"
                font.pixelSize: 12
            }

            Text {
                Layout.fillWidth: true
                text: root.username.length > 0 ? root.username : "用户"
                color: "#0f172a"
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            spacing: 5

            Text {
                text: "邮箱"
                color: "#64748b"
                font.pixelSize: 12
            }

            Text {
                Layout.fillWidth: true
                text: root.email.length > 0 ? root.email : "暂未绑定邮箱"
                color: "#0f172a"
                font.pixelSize: 15
                elide: Text.ElideRight
            }
        }

        Item { Layout.fillHeight: true }
    }

    footer: Item {
        implicitHeight: 62

        Rectangle {
            width: 82
            height: 34
            radius: 8
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            color: closeMouseArea.containsMouse ? "#dbeafe" : "#eff6ff"

            Text {
                anchors.centerIn: parent
                text: "关闭"
                color: "#2563eb"
                font.pixelSize: 14
                font.bold: true
            }

            MouseArea {
                id: closeMouseArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.close()
            }
        }
    }
}
