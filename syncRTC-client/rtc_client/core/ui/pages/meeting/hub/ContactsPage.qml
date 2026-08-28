pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal demonstrationAction(string message)
    signal contactsPageEntered()

    // 页面切入后仅通知联系人控制器，网络加载逻辑不放在 QML 中。
    onContactsPageEntered: contactsController.onContactsPageEntered()

    Rectangle {
        anchors.fill: parent
        color: "#f5f8ff"
    }

    // 使用固定工作台布局，联系人列表在自身区域内滚动，避免页面整体出现粗滚动条。
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
                    text: "通讯录"
                    color: "#0f172a"
                    font.pixelSize: 31
                    font.bold: true
                }

                Text {
                    text: "查看已有联系人，快速发起会议邀请。"
                    color: "#64748b"
                    font.pixelSize: 15
                }
            }

            Button {
                id: addContactButton

                Layout.preferredWidth: 118
                Layout.preferredHeight: 42
                text: "搜索联系人"
                font.pixelSize: 14
                font.bold: true
                onClicked: contactsController.searchContacts(contactSearchInput.text)

                background: Rectangle {
                    radius: 9
                    color: addContactButton.down ? "#1d4ed8" : addContactButton.hovered ? "#1e5ddd" : "#2563eb"
                }

                contentItem: Text {
                    text: addContactButton.text
                    color: "#ffffff"
                    font: addContactButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        TextField {
            id: contactSearchInput

            Layout.fillWidth: true
            Layout.preferredHeight: 50
            placeholderText: "输入邮箱搜索联系人"
            font.pixelSize: 15
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 16
            rightPadding: 16
            topPadding: 0
            bottomPadding: 0
            onAccepted: contactsController.searchContacts(text)

            background: Rectangle {
                radius: 9
                color: "#ffffff"
                border.color: contactSearchInput.activeFocus ? "#2563eb" : "#dbe3ef"
            }
        }

        Text {
            Layout.fillWidth: true
            visible: contactsController.contactsError.length > 0
                     || contactsController.contactsMessage.length > 0
            text: contactsController.contactsError.length > 0
                  ? contactsController.contactsError
                  : contactsController.contactsMessage
            color: contactsController.contactsError.length > 0 ? "#b91c1c" : "#166534"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Rectangle {
            id: searchResultCard

            property var resultData: contactsController.searchResults.length > 0
                                     ? contactsController.searchResults[0]
                                     : ({})

            visible: contactsController.searchResults.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 72 : 0
            radius: 9
            color: "#eff6ff"
            border.color: "#bfdbfe"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: searchResultCard.resultData.name || ""
                        color: "#0f172a"
                        font.pixelSize: 15
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: searchResultCard.resultData.email || ""
                        color: "#64748b"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                Button {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 36
                    text: "添加"
                    onClicked: contactsController.addContact(Number(searchResultCard.resultData.uid || 0))
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 300
            radius: 10
            color: "#ffffff"
            border.color: "#e2e8f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "已有联系人"
                        color: "#0f172a"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: contactsList.count + " 位联系人"
                        color: "#64748b"
                        font.pixelSize: 13
                    }
                }

                ListView {
                    id: contactsList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: contactsController.contacts
                    spacing: 10

                    delegate: contactCard

                    ScrollBar.vertical: ScrollBar {
                        id: contactsScrollBar

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
                            color: contactsScrollBar.pressed ? "#60a5fa" : contactsScrollBar.hovered ? "#93c5fd" : "#bfdbfe"
                        }
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                visible: contactsController.contactsLoading
                running: visible
                z: 2
            }

            Text {
                anchors.centerIn: parent
                visible: !contactsController.contactsLoading && contactsList.count === 0
                text: "暂无联系人，可通过邮箱搜索并添加"
                color: "#94a3b8"
                font.pixelSize: 14
                z: 2
            }
        }
    }

    Component {
        id: contactCard

        Rectangle {
            id: contactCardRoot

            required property var modelData
            property var contactData: contactCardRoot.modelData || ({})
            property int uid: Number(contactCardRoot.contactData.uid || 0)
            property string name: contactCardRoot.contactData.name || ""
            property string email: contactCardRoot.contactData.email || ""
            property string status: contactCardRoot.contactData.status || "离线"
            property color statusColor: contactCardRoot.contactData.statusColor || "#94a3b8"

            width: ListView.view.width
            height: 78
            radius: 9
            color: "#f8fafc"
            border.color: "#dbe3ef"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.topMargin: 14
                anchors.bottomMargin: 14
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    radius: 23
                    color: "#dbeafe"

                    Text {
                        anchors.centerIn: parent
                        text: contactCardRoot.name.charAt(0)
                        color: "#2563eb"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.name
                        color: "#0f172a"
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.email
                        color: "#64748b"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.preferredWidth: 88
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 7

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: contactCardRoot.statusColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: contactCardRoot.status
                        color: "#64748b"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                Button {
                    id: deleteContactButton

                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 34
                    text: "删除"
                    onClicked: contactsController.deleteContact(contactCardRoot.uid)

                    background: Rectangle {
                        radius: 7
                        color: deleteContactButton.down ? "#fee2e2"
                                                        : deleteContactButton.hovered ? "#fef2f2" : "transparent"
                        border.color: "#fecaca"
                    }

                    contentItem: Text {
                        text: deleteContactButton.text
                        color: "#b91c1c"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
