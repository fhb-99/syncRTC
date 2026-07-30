import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// 弹窗归类到 dialogs 后，需要回退两级才能复用登录页表单组件。
import "../../auth/components"

Dialog {
    id: root

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(540, parent.width - 32)
    // 小尺寸窗口中保持固定头尾，表单主体滚动，避免校验提示挤压选项卡。
    height: Math.min(620, parent.height - 32)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // scheduledAt 为空代表立即开始；password 为空代表会议默认不加密。
    signal meetingCreationRequested(string title, string scheduledAt, string password)

    property bool scheduleMeeting: false
    property bool passwordProtected: false
    property string titleError: ""
    property string scheduleError: ""
    property string passwordError: ""

    function selectStartMode(scheduled) {
        scheduleMeeting = scheduled
        scheduleError = ""
        if (scheduled && scheduledAtInput.text.length === 0)
            scheduledAtInput.text = timePicker.defaultScheduledAt()
    }

    function resetForm() {
        scheduleMeeting = false
        passwordProtected = false
        titleInput.text = ""
        scheduledAtInput.text = ""
        passwordInput.text = ""
        timePicker.lastSelectedDateTime = null
        titleError = ""
        scheduleError = ""
        passwordError = ""
        formScroll.contentY = 0
    }

    function openForCreation() {
        resetForm()
        open()
    }

    function submit() {
        var title = titleInput.text.trim()
        titleError = title.length === 0 ? "请输入会议主题" : ""
        passwordError = ""
        scheduleError = ""

        var password = ""
        if (passwordProtected) {
            password = passwordInput.text.trim()
            if (password.length < 6 || password.length > 16)
                passwordError = "会议密码应为 6-16 位"
        }

        var scheduledAt = ""
        if (scheduleMeeting) {
            scheduledAt = scheduledAtInput.text.trim()
            if (scheduledAt.length === 0)
                scheduleError = "请选择计划开始时间"
        }

        if (titleError.length > 0 || passwordError.length > 0 || scheduleError.length > 0) {
            formScroll.contentY = 0
            return
        }

        meetingCreationRequested(title, scheduledAt, password)
        close()
    }

    background: Rectangle {
        radius: 14
        color: "#ffffff"
        border.color: "#dbe3ef"
    }

    contentItem: ColumnLayout {
        implicitWidth: root.width
        implicitHeight: root.height
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 76

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 18
                anchors.topMargin: 18
                anchors.bottomMargin: 12
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        text: "创建会议"
                        color: "#0f172a"
                        font.pixelSize: 24
                        font.bold: true
                    }

                    Text {
                        text: "选择立即开始，或设定未来的会议时间。"
                        color: "#64748b"
                        font.pixelSize: 14
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    Layout.alignment: Qt.AlignTop
                    radius: 16
                    color: closeArea.containsMouse ? "#eff6ff" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: "#64748b"
                        font.pixelSize: 22
                    }

                    MouseArea {
                        id: closeArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#e2e8f0"
        }

        Flickable {
            id: formScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            contentWidth: width
            contentHeight: meetingForm.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: meetingForm

                width: formScroll.width - 48
                x: 24
                y: 20
                spacing: 12

                Text {
                    text: "会议主题"
                    color: "#334155"
                    font.pixelSize: 14
                    font.bold: true
                }

                TextField {
                    id: titleInput

                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    placeholderText: "例如：产品需求评审会"
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 14
                    rightPadding: 14
                    onTextChanged: root.titleError = ""

                    background: Rectangle {
                        radius: 8
                        color: "#f8fafc"
                        border.color: root.titleError.length > 0 ? "#ef4444"
                                      : titleInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                Text {
                    visible: root.titleError.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    text: root.titleError
                    color: "#dc2626"
                    font.pixelSize: 12
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe3ef"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10

                        Text {
                            text: "会议号"
                            color: "#334155"
                            font.pixelSize: 14
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "创建成功后自动生成"
                            color: "#64748b"
                            font.pixelSize: 13
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    radius: 8
                    color: "#f8fafc"
                    border.color: "#dbe3ef"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 12

                        Column {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "会议密码"
                                color: "#334155"
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Text {
                                text: root.passwordProtected ? "已开启密码保护" : "默认不加密"
                                color: "#64748b"
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            width: 46
                            height: 26
                            radius: 13
                            color: root.passwordProtected ? "#2563eb" : "#cbd5e1"

                            Rectangle {
                                width: 20
                                height: 20
                                radius: 10
                                anchors.verticalCenter: parent.verticalCenter
                                x: root.passwordProtected ? parent.width - width - 3 : 3
                                color: "#ffffff"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.passwordProtected = !root.passwordProtected
                                    if (!root.passwordProtected)
                                        passwordInput.text = ""
                                    root.passwordError = ""
                                }
                            }
                        }
                    }
                }

                TextField {
                    id: passwordInput

                    visible: root.passwordProtected
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 46 : 0
                    placeholderText: "设置 6-16 位会议密码"
                    echoMode: TextInput.Password
                    font.pixelSize: 15
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 14
                    rightPadding: 14
                    onTextChanged: root.passwordError = ""

                    background: Rectangle {
                        radius: 8
                        color: "#f8fafc"
                        border.color: root.passwordError.length > 0 ? "#ef4444"
                                      : passwordInput.activeFocus ? "#2563eb" : "#dbe3ef"
                    }
                }

                Text {
                    visible: root.passwordProtected && root.passwordError.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    text: root.passwordError
                    color: "#dc2626"
                    font.pixelSize: 12
                }

                Text {
                    text: "开始方式"
                    color: "#334155"
                    font.pixelSize: 14
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    spacing: 12

                    Repeater {
                        model: [
                            { "title": "立即开始", "detail": "创建后直接进入会议", "scheduled": false },
                            { "title": "定时开始", "detail": "在指定时间提醒成员", "scheduled": true }
                        ]

                        delegate: Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            Layout.fillHeight: true
                            radius: 9
                            color: root.scheduleMeeting === modelData.scheduled ? "#eff6ff" : "#f8fafc"
                            border.color: root.scheduleMeeting === modelData.scheduled ? "#2563eb" : "#dbe3ef"
                            // 固定边框宽度，选中态仅改变颜色，避免两张选项卡发生视觉跳动。
                            border.width: 1

                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 14
                                spacing: 9

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "transparent"
                                    border.color: root.scheduleMeeting === modelData.scheduled ? "#2563eb" : "#94a3b8"

                                    Rectangle {
                                        visible: root.scheduleMeeting === modelData.scheduled
                                        anchors.centerIn: parent
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: "#2563eb"
                                    }
                                }

                                Column {
                                    spacing: 4

                                    Text {
                                        text: modelData.title
                                        color: "#0f172a"
                                        font.pixelSize: 15
                                        font.bold: true
                                    }

                                    Text {
                                        text: modelData.detail
                                        color: "#64748b"
                                        font.pixelSize: 12
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectStartMode(modelData.scheduled)
                            }
                        }
                    }
                }

                Text {
                    visible: root.scheduleMeeting
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    text: "计划开始时间"
                    color: "#334155"
                    font.pixelSize: 14
                    font.bold: true
                }

                Rectangle {
                    id: scheduledAtInput

                    visible: root.scheduleMeeting
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 46 : 0
                    radius: 8
                    color: "#f8fafc"
                    border.color: root.scheduleError.length > 0 ? "#ef4444" : "#dbe3ef"

                    property string text: ""

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        text: scheduledAtInput.text
                        color: "#334155"
                        font.pixelSize: 15
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 14
                        text: "选择"
                        color: "#2563eb"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: timePicker.openForDateTime(timePicker.lastSelectedDateTime)
                    }
                }

                Text {
                    visible: root.scheduleMeeting && root.scheduleError.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    text: root.scheduleError
                    color: "#dc2626"
                    font.pixelSize: 12
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#e2e8f0"
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 72

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 48
                    radius: 8
                    color: cancelArea.containsMouse ? "#f1f5f9" : "#f8fafc"
                    border.color: "#dbe3ef"

                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: "#475569"
                        font.pixelSize: 15
                    }

                    MouseArea {
                        id: cancelArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 8
                    text: root.scheduleMeeting ? "创建定时会议" : "立即创建会议"
                    onClicked: root.submit()
                }
            }
        }
    }

    MeetingTimePickerPopup {
        id: timePicker

        onTimeSelected: function(scheduledAt) {
            scheduledAtInput.text = scheduledAt
            root.scheduleError = ""
        }
    }

    onOpened: titleInput.forceActiveFocus()
}
