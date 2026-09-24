import QtQuick
import QtTest
import "../ui/pages/meeting/room" as Meeting

TestCase {
    id: testCase

    name: "MeetingRoomPage"
    when: true

    Component {
        id: roomComponent

        Meeting.MeetingRoomPage { }
    }

    Component {
        id: mediaControllerComponent

        QtObject {
            property bool cameraEnabled: false
            property bool microphoneEnabled: false
            property bool localVideoAvailable: false

            function bindLocalVideoSink(sink) { void(sink) }
            function unbindLocalVideoSink(sink) { void(sink) }
            function bindRemoteVideoSink(userId, sink) {
                void(userId)
                void(sink)
            }
            function unbindRemoteVideoSink(userId, sink) {
                void(userId)
                void(sink)
            }
        }
    }

    SignalSpy {
        id: leaveSpy
    }

    SignalSpy {
        id: returnSpy
    }

    function test_pageExposesMeetingContextAndLeaveSignal() {
        var room = roomComponent.createObject(null, {
            "meetingCode": "100001",
            "username": "测试用户"
        })

        verify(room !== null)
        compare(room.meetingCode, "100001")
        compare(room.username, "测试用户")

        leaveSpy.target = room
        leaveSpy.signalName = "leaveRequested"
        room.leaveRequested()
        compare(leaveSpy.count, 1)

        leaveSpy.clear()
        leaveSpy.target = null
        room.destroy()
    }

    function test_leaveButtonReturnsLocallyAfterMeetingEnds() {
        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760,
            "status": "in_progress"
        })

        var leaveButton = findChild(room, "meetingLeaveButton")
        verify(leaveButton !== null)

        leaveSpy.target = room
        leaveSpy.signalName = "leaveRequested"
        returnSpy.target = room
        returnSpy.signalName = "returnRequested"

        mouseClick(leaveButton, leaveButton.width / 2, leaveButton.height / 2)
        compare(leaveSpy.count, 1)
        compare(returnSpy.count, 0)

        room.status = "ended"
        mouseClick(leaveButton, leaveButton.width / 2, leaveButton.height / 2)
        compare(leaveSpy.count, 1)
        compare(returnSpy.count, 1)

        leaveSpy.clear()
        leaveSpy.target = null
        returnSpy.clear()
        returnSpy.target = null
        room.destroy()
    }

    function test_controlsDoNotOverlapMeetingContent() {
        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760
        })

        var content = findChild(room, "meetingContent")
        var controls = findChild(room, "meetingControls")
        verify(content !== null)
        verify(controls !== null)
        verify(controls.y >= content.y + content.height)

        room.destroy()
    }

    function test_roomStartsWithoutMockMeetingData() {
        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760
        })

        var stage = findChild(room, "meetingStage")
        var background = findChild(room, "meetingBackground")
        var topBar = findChild(room, "meetingTopBar")
        var chatFeed = findChild(room, "meetingChatFeed")

        verify(stage !== null)
        verify(background !== null)
        verify(topBar !== null)
        compare(String(background.color), "#f5f7fb")
        compare(String(stage.color), "#f5f7fb")
        compare(String(topBar.color), "#ffffff")
        // 未接入真实字幕时，会议主画面不展示字幕占位区域。
        verify(findChild(room, "meetingLiveCaption") === null)
        verify(chatFeed === null)

        room.destroy()
    }

    function test_mediaControlsStartDisabled() {
        var controller = mediaControllerComponent.createObject(testCase)
        var room = roomComponent.createObject(testCase, {
            "width": 1160,
            "height": 760,
            "status": "in_progress",
            "mediaController": controller
        })

        var microphoneControl = findChild(room, "meetingMicrophoneControl")
        var cameraControl = findChild(room, "meetingCameraControl")
        verify(microphoneControl !== null)
        verify(cameraControl !== null)
        compare(room.microphoneEnabled, false)
        compare(room.cameraEnabled, false)
        compare(microphoneControl.label, "开麦克风")
        compare(cameraControl.label, "开视频")
        compare(microphoneControl.selected, false)
        compare(cameraControl.selected, false)
        compare(String(microphoneControl.color), "#ffffff")
        compare(String(cameraControl.color), "#ffffff")

        controller.microphoneEnabled = true
        controller.cameraEnabled = true
        tryCompare(microphoneControl, "label", "静音")
        tryCompare(cameraControl, "label", "关视频")
        compare(microphoneControl.selected, true)
        compare(cameraControl.selected, true)
        compare(String(microphoneControl.color), "#e8f0ff")
        compare(String(cameraControl.color), "#e8f0ff")

        room.destroy()
        controller.destroy()
    }

    function test_roomDoesNotOpenDemoSidePanelsBeforeRealtimeDataArrives() {
        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760
        })

        var assistantPanel = findChild(room, "meetingAssistantPanel")
        var chatPanel = findChild(room, "meetingChatPanel")

        verify(assistantPanel !== null)
        verify(chatPanel !== null)
        compare(room.sidePanelMode, "")
        verify(!assistantPanel.visible)
        verify(!chatPanel.visible)

        room.destroy()
    }

    function test_memberAreaCreatesLocalAndRemoteVideoOutputs() {
        var room = roomComponent.createObject(testCase, {
            "width": 1160,
            "height": 760,
            "members": [{
                "user_id": 1,
                "name": "当前用户",
                "is_self": true
            }, {
                "user_id": 2,
                "name": "远端成员",
                "is_self": false
            }]
        })

        verify(room !== null)
        tryVerify(function() {
            return findChild(room, "localVideoOutput") !== null
                    && findChild(room, "remoteVideoOutput_2") !== null
        })

        room.destroy()
    }

    function test_localAvatarStaysSmallUntilVideoIsAvailable() {
        var controller = mediaControllerComponent.createObject(testCase)
        var room = roomComponent.createObject(testCase, {
            "width": 1160,
            "height": 760,
            "status": "in_progress",
            "mediaController": controller,
            "members": [{
                "user_id": 1,
                "name": "当前用户",
                "is_self": true
            }]
        })

        verify(room !== null)
        tryVerify(function() {
            return findChild(room, "meetingMemberTile_1") !== null
                    && findChild(room, "meetingMemberAvatar_1") !== null
                    && findChild(room, "meetingMemberNameBadge_1") !== null
                    && findChild(room, "localVideoOutput") !== null
        })

        var memberTile = findChild(room, "meetingMemberTile_1")
        var avatar = findChild(room, "meetingMemberAvatar_1")
        var nameBadge = findChild(room, "meetingMemberNameBadge_1")
        var videoOutput = findChild(room, "localVideoOutput")
        compare(String(memberTile.color), "#eef2f7")
        compare(memberTile.parent.isSelf, true)
        compare(memberTile.parent.videoAvailable, false)
        verify(avatar.width <= 72)
        verify(avatar.width < memberTile.width)
        verify(avatar.height < memberTile.height)
        verify(nameBadge.width < memberTile.width / 2)

        controller.cameraEnabled = true
        controller.localVideoAvailable = true
        tryCompare(memberTile.parent, "videoAvailable", true)

        controller.cameraEnabled = false
        controller.localVideoAvailable = false
        tryCompare(memberTile.parent, "videoAvailable", false)

        room.destroy()
        controller.destroy()
    }

    function test_memberGridAdjustsColumnsWithMemberCount() {
        var fourMembers = []
        for (var i = 1; i <= 4; ++i) {
            fourMembers.push({
                "user_id": i,
                "name": "成员" + i,
                "is_self": i === 1
            })
        }

        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760,
            "members": fourMembers
        })

        verify(room !== null)
        var grid = findChild(room, "meetingParticipantGrid")
        verify(grid !== null)
        compare(grid.count, 4)
        compare(grid.layoutColumnCount, 2)
        compare(grid.visibleRowCount, 2)

        var sixMembers = []
        for (i = 1; i <= 6; ++i) {
            sixMembers.push({
                "user_id": i,
                "name": "成员" + i,
                "is_self": i === 1
            })
        }
        room.members = sixMembers

        tryCompare(grid, "count", 6)
        tryCompare(grid, "layoutColumnCount", 3)
        tryCompare(grid, "visibleRowCount", 2)

        room.destroy()
    }

    function test_groupAndPrivateChatScopesRequireChatController() {
        var room = roomComponent.createObject(null, {
            "width": 1160,
            "height": 760
        })

        room.openChatPanel()
        compare(room.chatScope, "group")

        var input = findChild(room, "meetingChatInput")
        var history = findChild(room, "meetingChatHistory")
        var chatPanel = findChild(room, "meetingChatPanel")
        verify(input !== null)
        verify(history !== null)
        verify(chatPanel !== null)
        verify(chatPanel.visible)
        verify(!input.enabled)
        compare(history.count, 0)

        room.openPrivateChat("测试成员", "2")
        compare(room.chatScope, "private")
        compare(room.privateRecipientId, "2")

        room.openGroupChat()
        room.toggleEmojiPicker()
        verify(room.emojiPickerVisible)

        room.destroy()
    }
}
