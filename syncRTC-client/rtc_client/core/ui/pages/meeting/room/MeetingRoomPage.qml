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
    property var members: []
    property var chatController: null
    property bool microphoneMuted: false
    property bool cameraEnabled: true
    property bool sharingEnabled: false
    // 右侧抽屉只在拿到对应的真实会中数据后展示，避免进入会议时显示演示内容。
    property string sidePanelMode: ""
    property bool hasRealtimeMeetingData: false
    // 私聊尚未接入，群聊范围固定为当前会议房间。
    property string chatScope: "group"
    property string privateRecipient: ""
    property string privateRecipientId: ""
    property bool emojiPickerVisible: false
    readonly property int chatHistoryPageSize: 50
    property bool loadingOlderMessages: false
    property bool allowTopHistoryLoad: false
    property bool groupHasMoreHistory: true
    property var privateHasMoreHistory: ({})
    property bool chatHadMessagesBeforeInitialLoad: false
    property real historyContentHeightBeforeLoad: 0
    property real historyContentYBeforeLoad: 0
    readonly property bool isHost: role === "host" || role === "co_host" || role === "creator"
    readonly property bool isScheduled: status === "scheduled"
    readonly property bool isInProgress: status === "in_progress"
    readonly property bool isEnded: status === "ended"

    signal startMeetingRequested(string meetingId)
    signal endMeetingRequested(string meetingId)
    signal leaveRequested()
    signal openMicrophoneRequested()
    signal closeMicrophoneRequested()
    signal openCameraRequested(string meetingId)
    signal closeCameraRequested(string meetingId)

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

    function setMicrophoneMuted(muted) {
        if (root.microphoneMuted === muted)
            return

        root.microphoneMuted = muted
        // QML 只发出用户意图，真实设备开关由 MediaController 接收信号后处理。
        if (muted)
            root.closeMicrophoneRequested()
        else
            root.openMicrophoneRequested()
    }

    function setCameraEnabled(enabled) {
        if (root.cameraEnabled === enabled)
            return

        root.cameraEnabled = enabled
        if (enabled)
            root.openCameraRequested(root.meetingId)
        else
            root.closeCameraRequested(root.meetingId)
    }

    function currentChatPeerId() {
        return chatScope === "private" ? privateRecipientId : ""
    }

    function hasMoreHistoryForCurrentChat() {
        if (chatScope === "group")
            return groupHasMoreHistory

        if (privateRecipientId.length === 0)
            return false

        return privateHasMoreHistory[privateRecipientId] !== false
    }

    function setHasMoreHistory(chatType, peerUserId, hasMore) {
        if (chatType === "group") {
            groupHasMoreHistory = hasMore
            return
        }

        // 私聊按对方 user_id 单独保存是否还有更多历史，避免 A 聊天到底影响 B 聊天继续加载。
        var updated = ({})
        for (var key in privateHasMoreHistory)
            updated[key] = privateHasMoreHistory[key]
        updated[String(peerUserId)] = hasMore
        privateHasMoreHistory = updated
    }

    function requestChatHistory(beforeMessageId, older) {
        if (!root.chatController || root.meetingId.length === 0)
            return
        if (older && (loadingOlderMessages || !hasMoreHistoryForCurrentChat()))
            return

        if (older) {
            loadingOlderMessages = true
            allowTopHistoryLoad = false
            historyContentHeightBeforeLoad = chatHistory.contentHeight
            historyContentYBeforeLoad = chatHistory.contentY
        } else {
            chatHadMessagesBeforeInitialLoad =
                    root.chatController.earliestMessageId(
                        chatScope, currentChatPeerId()).length > 0
        }

        root.chatController.requestHistory(
                    root.meetingId,
                    chatScope,
                    currentChatPeerId(),
                    beforeMessageId || "",
                    chatHistoryPageSize)
    }

    function requestOlderChatHistory() {
        if (!root.chatController)
            return

        var beforeMessageId = root.chatController.earliestMessageId(
                    chatScope, currentChatPeerId())
        if (String(beforeMessageId).length === 0)
            return

        requestChatHistory(String(beforeMessageId), true)
    }

    function maybeLoadOlderChatHistory() {
        if (!allowTopHistoryLoad || sidePanelMode !== "chat" || chatHistory.contentY > 24)
            return

        requestOlderChatHistory()
    }

    function openGroupChat() {
        chatScope = "group"
        privateRecipient = ""
        privateRecipientId = ""
        emojiPickerVisible = false
        sidePanelMode = "chat"
        allowTopHistoryLoad = false
        requestChatHistory("", false)
    }

    function openPrivateChat(memberName, memberId) {
        if (String(memberId).length === 0)
            return

        chatScope = "private"
        privateRecipient = memberName
        privateRecipientId = String(memberId)
        emojiPickerVisible = false
        sidePanelMode = "chat"
        allowTopHistoryLoad = false
        requestChatHistory("", false)
    }

    function openMembersPanel() {
        sidePanelMode = "members"
    }

    // 收件人入口回到成员列表，私聊目标只能从当前会议成员中选择。
    function chooseChatRecipient() {
        if (!hasRealtimeMeetingData)
            return

        emojiPickerVisible = false
        openMembersPanel()
    }

    function openChatPanel() {
        openGroupChat()
    }

    function closeSidePanel() {
        emojiPickerVisible = false
        sidePanelMode = ""
    }

    // 表情仅作为群聊输入内容的一部分，私聊输入保持纯文本。
    function toggleEmojiPicker() {
        if (chatScope !== "group")
            return

        emojiPickerVisible = !emojiPickerVisible
    }

    // 根据当前聊天范围发送群聊或私聊，消息状态由服务端 ack 和推送驱动。
    function sendChatMessage() {
        var content = chatInput.text.trim()
        if (content.length === 0 || !root.chatController)
            return

        if (chatScope === "private")
            root.chatController.sendPrivateMessage(root.meetingId, Number(privateRecipientId), content)
        else
            root.chatController.sendGroupMessage(root.meetingId, content)
        chatInput.text = ""
        emojiPickerVisible = false
    }

    // 入会回包提供当前房间成员，私聊只使用其中的 user_id 作为目标身份。
    function refreshMembers() {
        memberModel.clear()
        for (var i = 0; i < members.length; ++i) {
            var member = members[i]
            var userId = member && typeof member === "object"
                    ? String(member.user_id || "") : String(member)
            if (userId.length === 0)
                continue

            var memberName = member && typeof member === "object" && member.name
                    ? String(member.name) : "成员 " + userId
            memberModel.append({
                "userId": userId,
                "name": memberName,
                "color": "#2563eb",
                "muted": false,
                "roomState": member && typeof member === "object" && member.room_state === "reconnecting"
                             ? "reconnecting" : "active",
                "isSelf": member && typeof member === "object" && member.is_self === true
            })
        }
    }

    onMembersChanged: refreshMembers()
    onMeetingIdChanged: {
        loadingOlderMessages = false
        allowTopHistoryLoad = false
        groupHasMoreHistory = true
        privateHasMoreHistory = ({})
    }
    Component.onCompleted: refreshMembers()

    // 成员、聊天等模型等待会议实时信令填充，入会成功前后都不再写入演示数据。
    ListModel {
        id: memberModel
    }

    Connections {
        target: root.chatController

        function onMessagesChanged() {
            if (!root.loadingOlderMessages) {
                Qt.callLater(function() {
                    chatHistory.positionViewAtEnd()
                    root.allowTopHistoryLoad = true
                })
            }
        }

        function onHistoryMessagesLoaded(chatType, peerUserId, addedCount, hasMore) {
            var sameChat = chatType === root.chatScope &&
                    (chatType === "group" || String(peerUserId) === root.privateRecipientId)
            if (!sameChat)
                return

            if (!root.loadingOlderMessages) {
                if (addedCount > 0 || !root.chatHadMessagesBeforeInitialLoad)
                    root.setHasMoreHistory(chatType, String(peerUserId), hasMore)
                root.allowTopHistoryLoad = true
                return
            }

            root.setHasMoreHistory(chatType, String(peerUserId), hasMore && addedCount > 0)
            var oldHeight = root.historyContentHeightBeforeLoad
            var oldY = root.historyContentYBeforeLoad
            root.loadingOlderMessages = false

            // 历史消息是插到模型前面的。恢复 contentY 可以让用户继续停在原来的阅读位置，
            // 视觉上就是“往上滚动加载出更早消息”，而不是突然跳到底部或跳到列表开头。
            Qt.callLater(function() {
                var delta = Math.max(0, chatHistory.contentHeight - oldHeight)
                chatHistory.contentY = Math.max(0, oldY + delta)
                root.allowTopHistoryLoad = true
            })
        }

        function onGroupHistoryLoadFailed(error) {
            void(error)
            root.loadingOlderMessages = false
            root.allowTopHistoryLoad = true
        }
    }

    // 成员模型仍等待后续会议成员实时同步，聊天消息由 ChatController 单独维护。
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
                        required property string roomState

                        width: Math.max(130, (parent.width - 42) / 4)
                        height: parent.height
                        radius: 14
                        color: roomState === "reconnecting" ? "#64748b" : color

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
                                text: roomState === "reconnecting" ? name + " · 重连中" : name
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
                    text: "选择成员可发起私聊"
                    color: "#64748b"
                    font.pixelSize: 13
                }

                Item {
                    visible: memberModel.count === 0
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
                        required property string userId
                        required property color color
                        required property bool muted
                        required property bool isSelf
                        required property string roomState

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
                                    text: roomState === "reconnecting" ? "重连中" : (muted ? "已静音" : "会议中")
                                    color: roomState === "reconnecting" ? "#d97706" : (muted ? "#ef4444" : "#16a34a")
                                    font.pixelSize: 12
                                }
                            }

                            Button {
                                visible: !isSelf
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

                                onClicked: root.openPrivateChat(name, userId)
                            }
                        }
                    }
                }

                Item {
                    visible: memberModel.count > 0
                    Layout.fillHeight: true
                }

                Button {
                    visible: memberModel.count > 0
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
                        color: "#f5f7fb"
                        border.color: "#e7ebf3"

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
                                text: root.chatScope === "group" ? "所有人" : root.privateRecipient
                                color: "#2563eb"
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                            }

                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: root.chatScope === "private"
                            onClicked: root.chooseChatRecipient()
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
                    visible: root.chatController && root.chatController.count > 0
                    model: root.chatController
                    onContentYChanged: root.maybeLoadOlderChatHistory()

                    header: Item {
                        width: chatHistory.width
                        height: root.loadingOlderMessages ? 32 : 0

                        Text {
                            anchors.centerIn: parent
                            visible: root.loadingOlderMessages
                            text: "正在加载更早消息"
                            color: "#94a3b8"
                            font.pixelSize: 12
                        }
                    }

                    delegate: Rectangle {
                        required property string senderName
                        required property string chatType
                        required property var senderUserId
                        required property string receiverUserId
                        required property string content
                        required property string createdAt
                        required property string deliveryState
                        required property bool isMine

                        property bool belongsToCurrentChat: chatType === "group"
                                                            ? root.chatScope === "group"
                                                            : root.chatScope === "private" &&
                                                              (String(senderUserId) === root.privateRecipientId ||
                                                               receiverUserId === root.privateRecipientId)

                        visible: belongsToCurrentChat
                        width: ListView.view.width
                        height: belongsToCurrentChat ? messageColumn.implicitHeight + 20 : 0
                        radius: 10
                        color: isMine ? "#e8f0ff" : "#f7f8fa"
                        border.color: isMine ? "#d7e5ff" : "transparent"

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
                                    text: isMine ? "我" : senderName
                                    color: isMine ? "#2563eb" : "#4f5b6e"
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                // 每条消息均显示发送时间，避免群聊与私聊信息缺少时序。
                                Text {
                                    text: createdAt
                                    color: "#98a2b3"
                                    font.pixelSize: 11
                                }

                                // 本地消息先显示发送中，服务端拒绝后明确提示失败。
                                Text {
                                    visible: isMine && deliveryState !== "sent"
                                    text: deliveryState === "failed" ? "发送失败" : "发送中"
                                    color: deliveryState === "failed" ? "#dc2626" : "#98a2b3"
                                    font.pixelSize: 11
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: content
                                color: "#263247"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Item {
                    visible: !root.chatController || root.chatController.count === 0
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
                                         ? "向会议群聊发送消息"
                                         : "向 " + root.privateRecipient + " 发送私聊"
                        selectByMouse: true
                        enabled: root.chatController && !root.isEnded &&
                                 (root.chatScope === "group" || root.privateRecipientId.length > 0)
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
                        enabled: root.chatController && !root.isEnded &&
                                 (root.chatScope === "group" || root.privateRecipientId.length > 0) &&
                                 chatInput.text.trim().length > 0
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
                    text: root.chatScope === "group"
                          ? "群聊消息会发送给当前房间成员。"
                          : "私聊消息仅发送给当前选择的会议成员。"
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
            onTriggered: root.setMicrophoneMuted(!root.microphoneMuted)
        }

        ControlButton {
            visible: root.isInProgress
            label: root.cameraEnabled ? "视频" : "开视频"
            iconText: root.cameraEnabled ? "▣" : "□"
            selected: root.cameraEnabled
            onTriggered: root.setCameraEnabled(!root.cameraEnabled)
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
            onTriggered: root.sidePanelMode === "chat" ? root.closeSidePanel() : root.openGroupChat()
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
