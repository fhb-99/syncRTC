import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string meetingId: ""
    property string meetingCode: ""
    property string status: "scheduled"
    property string role: "participant"
    property string username: ""
    property bool microphoneMuted: false
    property bool cameraEnabled: true
    property bool sharingEnabled: false
    // 右侧抽屉只在拿到对应的真实会中数据后展示，避免进入会议时显示演示内容。
    property string sidePanelMode: ""
    property bool hasRealtimeMeetingData: false
    // 聊天范围和私聊对象目前仅用于 QML 展示，后续由服务端消息字段驱动。
    property string chatScope: "group"
    property string privateRecipient: ""
    property bool emojiPickerVisible: false
    readonly property bool isHost: role === "host" || role === "co_host" || role === "creator"
    readonly property bool isScheduled: status === "scheduled"
    readonly property bool isInProgress: status === "in_progress"
    readonly property bool isEnded: status === "ended"

    signal startMeetingRequested(string meetingId)
    signal endMeetingRequested(string meetingId)
    signal leaveRequested()

    function avatarText(name) {
        var value = name.trim()
        return value.length > 0 ? value.charAt(0).toUpperCase() : "U"
    }

    function statusText() {
        if (isInProgress)
            return "进行中"
        if (isEnded)
            return "已结束"
        return "未开始"
    }

    function roleText() {
        return isHost ? "主持人" : "参会者"
    }

    function roomTitle() {
        if (isEnded)
            return "会议已结束"
        if (isInProgress)
            return "SyncRTC 会议中"
        return "会议房间"
    }

    function statusHintText() {
        if (isEnded)
            return "本次会议已经结束"
        if (isInProgress)
            return "会议正在进行"
        if (isHost)
            return "会议尚未开始，你可以开始会议"
        return "等待主持人开始会议"
    }

    function requestStartMeeting() {
        if (!isHost || !isScheduled)
            return

        root.startMeetingRequested(root.meetingId)
    }

    function requestEndMeeting() {
        if (!isHost || !isInProgress)
            return

        root.endMeetingRequested(root.meetingId)
    }

    function openGroupChat() {
        chatScope = "group"
        privateRecipient = ""
        emojiPickerVisible = false
        sidePanelMode = "chat"
    }

    function openPrivateChat(memberName) {
        if (!hasRealtimeMeetingData)
            return

        chatScope = "private"
        privateRecipient = memberName
        emojiPickerVisible = false
        sidePanelMode = "chat"
    }

    function openMembersPanel() {
        sidePanelMode = "members"
    }

    // “发送给”入口统一跳转至成员列表，用户可从中发起私聊或返回群聊。
    function chooseChatRecipient() {
        if (!hasRealtimeMeetingData)
            return

        emojiPickerVisible = false
        openMembersPanel()
    }

    function openChatPanel() {
        sidePanelMode = "chat"
    }

    function closeSidePanel() {
        emojiPickerVisible = false
        sidePanelMode = ""
    }

    // 群聊本地模式也支持表情输入；私聊未接入时不允许打开表情面板。
    function toggleEmojiPicker() {
        if (chatScope !== "group")
            return

        emojiPickerVisible = !emojiPickerVisible
    }

    function currentTimeText() {
        var now = new Date()
        var hour = now.getHours().toString().padStart(2, "0")
        var minute = now.getMinutes().toString().padStart(2, "0")
        return hour + ":" + minute
    }

    // 群聊在信令接入前仅追加到本地模型，消息只对当前用户可见。
    function sendChatMessage() {
        if (chatScope !== "group")
            return

        var content = chatInput.text.trim()
        if (content.length === 0)
            return

        groupChatModel.append({ "name": "我", "message": content, "time": currentTimeText() })
        chatInput.text = ""
        emojiPickerVisible = false
        chatHistory.positionViewAtEnd()
    }

    // 成员、聊天等模型等待会议实时信令填充，入会成功前后都不再写入演示数据。
    ListModel {
        id: memberModel
    }

    // 服务端聊天消息后续追加到该模型；当前允许用户保留仅本机可见的群聊消息。
    ListModel {
        id: groupChatModel
    }

    ListModel {
        id: privateChatModel
    }

    component ControlButton: Rectangle {
        id: controlButtonRoot

        required property string label
        required property string iconText
        property bool selected: false
        signal triggered()

        implicitWidth: 76
        implicitHeight: 58
        radius: 14
        color: selected ? "#2563eb" : controlMouse.containsMouse ? "#eff6ff" : "#f8fafc"
        border.color: selected ? "#2563eb" : "#dbe3ef"

        Column {
            anchors.centerIn: parent
            spacing: 3

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: controlButtonRoot.iconText
                color: controlButtonRoot.selected ? "#ffffff" : "#2563eb"
                font.pixelSize: 17
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: controlButtonRoot.label
                color: controlButtonRoot.selected ? "#ffffff" : "#2563eb"
                font.pixelSize: 12
                font.bold: true
            }
        }

        MouseArea {
            id: controlMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.triggered()
        }
    }

    component LifecycleButton: Rectangle {
        id: lifecycleButtonRoot

        required property string label
        required property string iconText
        property string normalColor: "#2563eb"
        property string hoverColor: "#1d4ed8"
        signal triggered()

        width: 104
        height: 58
        radius: 14
        color: lifecycleMouse.containsMouse ? hoverColor : normalColor

        Column {
            anchors.centerIn: parent
            spacing: 3

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: lifecycleButtonRoot.iconText
                color: "#ffffff"
                font.pixelSize: 18
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: lifecycleButtonRoot.label
                color: "#ffffff"
                font.pixelSize: 12
                font.bold: true
            }
        }

        MouseArea {
            id: lifecycleMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.triggered()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#07111f"
    }

    Rectangle {
        id: topBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 74
        color: "#111c2f"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 32
            anchors.rightMargin: 38
            spacing: 22

            Text {
                text: root.roomTitle()
                color: "#f8fafc"
                font.pixelSize: 23
                font.bold: true
            }

            Rectangle {
                Layout.preferredWidth: 170
                Layout.preferredHeight: 34
                radius: 17
                color: "#1e3a8a"

                Text {
                    anchors.centerIn: parent
                    text: "会议号  " + root.meetingCode
                    color: "#dbeafe"
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            Rectangle {
                Layout.preferredWidth: roleBadgeText.implicitWidth + 28
                Layout.preferredHeight: 34
                radius: 17
                color: root.isHost ? "#7c3aed" : "#334155"

                Text {
                    id: roleBadgeText

                    anchors.centerIn: parent
                    text: root.roleText()
                    color: "#ffffff"
                    font.pixelSize: 13
                    font.bold: true
                }
            }

            Rectangle {
                Layout.preferredWidth: statusBadgeText.implicitWidth + 28
                Layout.preferredHeight: 34
                radius: 17
                color: root.isInProgress ? "#15803d" : root.isEnded ? "#475569" : "#ca8a04"

                Text {
                    id: statusBadgeText

                    anchors.centerIn: parent
                    text: root.statusText()
                    color: "#ffffff"
                    font.pixelSize: 13
                    font.bold: true
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.isInProgress
                      ? (root.hasRealtimeMeetingData ? "网络状态已同步" : "网络状态待接入")
                      : "聊天可用"
                color: "#93c5fd"
                font.pixelSize: 13
            }
        }
    }

    RowLayout {
        id: meetingContent
        objectName: "meetingContent"

        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: meetingControls.top
        anchors.margins: 28
        anchors.topMargin: 30
        anchors.bottomMargin: 16
        spacing: 18

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            Rectangle {
                id: stage

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 420
                radius: 18
                color: "#1d2a3d"
                clip: true
                objectName: "meetingStage"

                Rectangle {
                    width: 84
                    height: 84
                    radius: 42
                    anchors.centerIn: parent
                    color: "#2563eb"

                    Text {
                        anchors.centerIn: parent
                        text: root.avatarText(root.username)
                        color: "#ffffff"
                        font.pixelSize: 28
                        font.bold: true
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 16
                    anchors.bottomMargin: 16
                    width: hostLabel.implicitWidth + 28
                    height: 30
                    radius: 15
                    color: "#101a2d"

                    Text {
                        id: hostLabel

                        anchors.centerIn: parent
                        text: root.username.length > 0 ? root.username : "我"
                        color: "#f8fafc"
                        font.pixelSize: 13
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 32
                    width: Math.min(parent.width - 80, liveCaption.implicitWidth + 48)
                    height: 52
                    radius: 14
                    color: "#020617"
                    opacity: 0.94

                    Text {
                        id: liveCaption

                        objectName: "meetingLiveCaption"
                        anchors.centerIn: parent
                        width: parent.width - 32
                        text: root.isInProgress && root.hasRealtimeMeetingData ? "实时字幕已同步" : "实时字幕暂未接入"
                        color: "#ffffff"
                        font.pixelSize: 17
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    id: lifecycleBanner

                    objectName: "meetingLifecycleBanner"
                    visible: !root.isInProgress
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width - 96, lifecycleContent.implicitWidth + 64)
                    height: lifecycleContent.implicitHeight + 38
                    radius: 16
                    color: root.isEnded ? "#0f172a" : "#111827"
                    opacity: 0.96

                    Column {
                        id: lifecycleContent

                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            objectName: "meetingWaitingHostText"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.isScheduled && !root.isHost
                            text: "等待主持人开始会议"
                            color: "#f8fafc"
                            font.pixelSize: 22
                            font.bold: true
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.isScheduled && root.isHost
                            text: "会议尚未开始"
                            color: "#f8fafc"
                            font.pixelSize: 22
                            font.bold: true
                        }

                        Text {
                            objectName: "meetingEndedText"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.isEnded
                            text: "会议已结束"
                            color: "#f8fafc"
                            font.pixelSize: 22
                            font.bold: true
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.isEnded ? "可返回主界面查看其他会议" : "会议开始前仍可使用聊天和成员面板"
                            color: "#cbd5e1"
                            font.pixelSize: 13
                        }
                    }
                }

            }

            Row {
                // 底部成员缩略条持续保留，成员按钮只负责打开右侧成员面板。
                visible: true
                Layout.fillWidth: true
                Layout.preferredHeight: 112
                spacing: 14

                // 成员缩略区域只绑定 memberModel；模型为空时显示同步等待状态。
                Rectangle {
                    visible: memberModel.count === 0
                    width: parent.width
                    height: parent.height
                    radius: 14
                    color: "#172437"
                    border.color: "#304158"

                    Text {
                        anchors.centerIn: parent
                        text: "等待成员实时数据同步"
                        color: "#94a3b8"
                        font.pixelSize: 14
                    }
                }

                Repeater {
                    model: memberModel

                    delegate: Rectangle {
                        required property string name
                        required property color color
                        required property bool muted

                        width: Math.max(130, (parent.width - 42) / 4)
                        height: parent.height
                        radius: 14
                        color: color

                        Rectangle {
                            width: 54
                            height: 54
                            radius: 27
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 16
                            color: "#94a3b8"

                            Text {
                                anchors.centerIn: parent
                                text: root.avatarText(name)
                                color: "#ffffff"
                                font.pixelSize: 19
                                font.bold: true
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 12
                            height: 24
                            radius: 12
                            color: "#101a2d"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: name
                                color: "#ffffff"
                                font.pixelSize: 12
                            }

                            Text {
                                visible: muted
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                text: "静音"
                                color: "#fca5a5"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        // 固定宽度的右侧抽屉：AI、聊天、成员仅展示其中一个，保持主会场完整。
        Item {
            id: rightWorkbench

            Layout.preferredWidth: visible ? 360 : 0
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            visible: root.sidePanelMode.length > 0

        Rectangle {
            id: assistantPanel

            objectName: "meetingAssistantPanel"
            visible: root.sidePanelMode === "assistant"
            anchors.fill: parent
            radius: 18
            color: "#ffffff"
            border.color: "#e7ebf3"
            clip: true

            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: assistantContent.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ColumnLayout {
                    id: assistantContent

                    width: parent.width
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            spacing: 2

                            Text {
                                text: "AI 小助手"
                                color: "#172033"
                                font.pixelSize: 21
                                font.bold: true
                            }

                            Text {
                                text: "基于本次会议内容生成"
                                color: "#8a94a6"
                                font.pixelSize: 12
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 14
                            color: closeAiMouse.containsMouse ? "#f1f4f9" : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "×"
                                color: "#687386"
                                font.pixelSize: 20
                            }

                            MouseArea {
                                id: closeAiMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.closeSidePanel()
                            }
                        }
                    }

                    Item {
                        visible: !root.hasRealtimeMeetingData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 180

                        Column {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "暂无 AI 会议数据"
                                color: "#475569"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "实时字幕或纪要信令接入后将在这里展示"
                                color: "#94a3b8"
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        visible: root.hasRealtimeMeetingData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 10
                        color: "#f5f7fb"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12

                            Text {
                                text: "会议纪要"
                                color: "#2563eb"
                                font.pixelSize: 13
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "暂无会议纪要"
                                color: "#94a3b8"
                                font.pixelSize: 12
                            }
                        }
                    }

                    // AI 洞察仅在后续真实会议数据填充后显示。
                    Rectangle {
                        visible: root.hasRealtimeMeetingData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        radius: 14
                        color: "#f5f8ff"
                        border.color: "#dce8ff"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 6

                            Text {
                                text: "关键结论"
                                color: "#2d6be8"
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: ""
                                color: "#475569"
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                    visible: root.hasRealtimeMeetingData
                    text: "自动待办"
                    color: "#0f172a"
                    font.pixelSize: 17
                    font.bold: true
                    Layout.topMargin: 8
                }

                    Repeater {
                    visible: root.hasRealtimeMeetingData
                    // 自动待办模型等待 AI 服务返回，禁止在此填充写死任务。
                    model: []

                    delegate: Rectangle {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        radius: 12
                        color: "#eff6ff"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Text {
                                text: modelData.owner
                                color: "#2563eb"
                                font.pixelSize: 13
                                font.bold: true
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelData.task
                                    color: "#475569"
                                    font.pixelSize: 13
                                }

                                Text {
                                    text: modelData.deadline
                                    color: "#94a3b8"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                    Item {
                        visible: root.hasRealtimeMeetingData
                        Layout.fillHeight: true
                    }

                    Button {
                    visible: root.hasRealtimeMeetingData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    text: "生成完整纪要"
                    font.bold: true

                    background: Rectangle {
                        radius: 12
                        color: "#2563eb"
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 6
                }
            }
        }

        Rectangle {
            id: membersPanel

            objectName: "meetingMembersPanel"
            visible: root.sidePanelMode === "members"
            anchors.fill: parent
            radius: 18
            color: "#ffffff"
            border.color: "#e7ebf3"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "会议成员"
                        color: "#172033"
                        font.pixelSize: 21
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "×"
                        color: "#687386"
                        font.pixelSize: 20

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeSidePanel()
                        }
                    }
                }

                Text {
                    text: "成员同步后可发起私聊"
                    color: "#64748b"
                    font.pixelSize: 13
                }

                Item {
                    visible: !root.hasRealtimeMeetingData
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        text: "暂无成员实时数据"
                        color: "#94a3b8"
                        font.pixelSize: 14
                    }
                }

                Repeater {
                    model: memberModel

                    delegate: Rectangle {
                        required property string name
                        required property color color
                        required property bool muted

                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        radius: 12
                        color: "#f8fafc"
                        border.color: "#dbe3ef"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Rectangle {
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 38
                                radius: 19
                                color: parent.parent.color

                                Text {
                                    anchors.centerIn: parent
                                    text: root.avatarText(name)
                                    color: "#ffffff"
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: name
                                    color: "#1e293b"
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: muted ? "已静音" : "会议中"
                                    color: muted ? "#ef4444" : "#16a34a"
                                    font.pixelSize: 12
                                }
                            }

                            Button {
                                visible: index < 3
                                Layout.preferredWidth: 58
                                Layout.preferredHeight: 32
                                text: "私聊"
                                font.bold: true

                                background: Rectangle {
                                    radius: 10
                                    color: parent.down ? "#1d4ed8" : "#e8f0ff"
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: "#2563eb"
                                    font.pixelSize: 12
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                onClicked: root.openPrivateChat(name)
                            }
                        }
                    }
                }

                Item {
                    visible: root.hasRealtimeMeetingData
                    Layout.fillHeight: true
                }

                Button {
                    visible: root.hasRealtimeMeetingData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    text: "打开会议群聊"
                    font.bold: true
                    onClicked: root.openGroupChat()

                    background: Rectangle {
                        radius: 12
                        color: "#2563eb"
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        Rectangle {
            id: chatPanel

            objectName: "meetingChatPanel"
            visible: root.sidePanelMode === "chat"
            anchors.fill: parent
            radius: 18
            color: "#ffffff"
            border.color: "#e7ebf3"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 2

                        Text {
                            objectName: "chatPanelTitle"
                            text: root.chatScope === "group"
                                  ? "聊天"
                                  : "与 " + root.privateRecipient + " 私聊"
                            color: "#172033"
                            font.pixelSize: 21
                            font.bold: true
                        }

                        Text {
                            text: root.chatScope === "group" ? "会议内消息" : "仅你和对方可见"
                            color: "#8a94a6"
                            font.pixelSize: 12
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 14
                        color: closeChatMouse.containsMouse ? "#f1f4f9" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: "#687386"
                            font.pixelSize: 20
                        }

                        MouseArea {
                            id: closeChatMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeSidePanel()
                        }
                    }
                }

                // 输入前的轻量工具栏：表情与收件人选择，不额外占用消息展示区域。
                RowLayout {
                    id: chatTools

                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        id: emojiButton

                        objectName: "meetingEmojiButton"
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        radius: 10
                        color: emojiMouse.containsMouse ? "#edf3ff" : "#f5f7fb"

                        Text {
                            anchors.centerIn: parent
                            text: "☺"
                            color: "#4b5b73"
                            font.pixelSize: 22
                        }

                        MouseArea {
                            id: emojiMouse

                            objectName: "meetingEmojiMouse"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: root.chatScope === "group"
                            onClicked: root.toggleEmojiPicker()
                        }
                    }

                    Rectangle {
                        id: recipientButton

                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 10
                        color: root.hasRealtimeMeetingData ? "#f5f7fb" : "#f8fafc"
                        border.color: "#e7ebf3"
                        opacity: root.hasRealtimeMeetingData ? 1 : 0.65

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 6

                            Text {
                                text: "发送给"
                                color: "#8a94a6"
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.chatScope === "group"
                                      ? (root.hasRealtimeMeetingData ? "所有人" : "所有人（仅自己可见）")
                                      : root.privateRecipient
                                color: "#2563eb"
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "⌄"
                                color: "#7b8798"
                                font.pixelSize: 16
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: root.hasRealtimeMeetingData
                            onClicked: {
                                root.chooseChatRecipient()
                            }
                        }
                    }
                }

                ListView {
                    id: chatHistory

                    objectName: "meetingChatHistory"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    spacing: 10
                    clip: true
                    visible: root.hasRealtimeMeetingData || groupChatModel.count > 0
                    model: root.chatScope === "group" ? groupChatModel : privateChatModel

                    delegate: Rectangle {
                        required property string name
                        required property string message
                        required property string time

                        width: ListView.view.width
                        height: messageColumn.implicitHeight + 20
                        radius: 10
                        color: name === "我" ? "#e8f0ff" : "#f7f8fa"
                        border.color: name === "我" ? "#d7e5ff" : "transparent"

                        ColumnLayout {
                            id: messageColumn

                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: name === "对方" ? root.privateRecipient : name
                                    color: name === "我" ? "#2563eb" : "#4f5b6e"
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                // 每条消息均显示发送时间，避免群聊与私聊信息缺少时序。
                                Text {
                                    text: time
                                    color: "#98a2b3"
                                    font.pixelSize: 11
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: message
                                color: "#263247"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Item {
                    visible: !root.hasRealtimeMeetingData && groupChatModel.count === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        text: "暂无聊天消息"
                        color: "#94a3b8"
                        font.pixelSize: 14
                    }
                }

                RowLayout {
                    id: composeRow

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 8

                    TextField {
                        id: chatInput

                        objectName: "meetingChatInput"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        placeholderText: root.chatScope === "group"
                                         ? "向会议群聊发送消息（仅自己可见）"
                                         : "向 " + root.privateRecipient + " 发送私聊"
                        selectByMouse: true
                        enabled: root.chatScope === "group"
                        Keys.onReturnPressed: root.sendChatMessage()

                        background: Rectangle {
                            radius: 10
                            color: "#f5f7fb"
                            border.color: "#e5eaf2"
                        }
                    }

                    Button {
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 42
                        text: "发送"
                        enabled: root.chatScope === "group" && chatInput.text.trim().length > 0
                        onClicked: root.sendChatMessage()

                        background: Rectangle {
                            radius: 10
                            color: parent.enabled ? "#2d6be8" : "#dbe7ff"
                        }

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? "#ffffff" : "#7ba3ef"
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.hasRealtimeMeetingData
                          ? "聊天信令已接入后，可在此发送会议群聊或私聊消息。"
                          : "当前群聊消息仅在本机显示；成员同步后可开启私聊。"
                    color: "#94a3b8"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            // 表情面板为本地 QML 展示交互，选择后直接插入当前输入框。
            Rectangle {
                id: emojiPicker

                visible: root.emojiPickerVisible
                z: 2
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 92
                width: 232
                height: 138
                radius: 14
                color: "#ffffff"
                border.color: "#dfe6f1"

                Grid {
                    anchors.fill: parent
                    anchors.margins: 10
                    columns: 6
                    rowSpacing: 6
                    columnSpacing: 6

                    Repeater {
                        model: ["😀", "😁", "😂", "😄", "🙂", "😉", "👍", "👏", "🎉", "💡", "❤️", "🚀"]

                        delegate: Rectangle {
                            required property string modelData

                            width: 29
                            height: 29
                            radius: 8
                            color: emojiCellMouse.containsMouse ? "#edf3ff" : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 18
                            }

                            MouseArea {
                                id: emojiCellMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: root.chatScope === "group"
                                onClicked: {
                                    chatInput.text += modelData
                                    chatInput.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
        }
    }

    Row {
        id: meetingControls
        objectName: "meetingControls"

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        height: 58
        spacing: 12

        ControlButton {
            visible: root.isInProgress
            label: root.microphoneMuted ? "取消静音" : "静音"
            iconText: root.microphoneMuted ? "◌" : "●"
            selected: root.microphoneMuted
            onTriggered: root.microphoneMuted = !root.microphoneMuted
        }

        ControlButton {
            visible: root.isInProgress
            label: root.cameraEnabled ? "视频" : "开视频"
            iconText: root.cameraEnabled ? "▣" : "□"
            selected: root.cameraEnabled
            onTriggered: root.cameraEnabled = !root.cameraEnabled
        }

        ControlButton {
            visible: root.isInProgress
            label: "共享"
            iconText: "↗"
            selected: root.sharingEnabled
            onTriggered: root.sharingEnabled = !root.sharingEnabled
        }

        ControlButton {
            visible: !root.isEnded
            label: "聊天"
            iconText: "☰"
            selected: root.sidePanelMode === "chat"
            onTriggered: root.sidePanelMode = root.sidePanelMode === "chat" ? "" : "chat"
        }

        ControlButton {
            visible: root.isInProgress
            label: "AI"
            iconText: "✦"
            selected: root.sidePanelMode === "assistant"
            onTriggered: root.sidePanelMode = root.sidePanelMode === "assistant" ? "" : "assistant"
        }

        ControlButton {
            visible: !root.isEnded
            label: "成员"
            iconText: "♙"
            selected: root.sidePanelMode === "members"
            onTriggered: root.sidePanelMode = root.sidePanelMode === "members" ? "" : "members"
        }

        LifecycleButton {
            objectName: "startMeetingButton"
            visible: root.isHost && root.isScheduled
            label: "开始会议"
            iconText: "▶"
            normalColor: "#16a34a"
            hoverColor: "#15803d"
            onTriggered: root.requestStartMeeting()
        }

        LifecycleButton {
            objectName: "endMeetingButton"
            visible: root.isHost && root.isInProgress
            label: "结束会议"
            iconText: "■"
            normalColor: "#dc2626"
            hoverColor: "#b91c1c"
            onTriggered: root.requestEndMeeting()
        }

        Rectangle {
            width: 82
            height: 58
            radius: 14
            color: leaveMouse.containsMouse ? "#dc2626" : "#ef4444"

            Column {
                anchors.centerIn: parent
                spacing: 3

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "×"
                    color: "#ffffff"
                    font.pixelSize: 21
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.isEnded ? "返回" : "退出"
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            MouseArea {
                id: leaveMouse

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.leaveRequested()
            }
        }
    }
}
