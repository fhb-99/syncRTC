import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// 弹窗归类到 dialogs 后，需要回退两级才能复用登录页表单组件。
import "../../auth/components"

Popup {
    id: root

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 486
    height: 414
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    signal timeSelected(string scheduledAt)

    property var dayOptions: []
    property var hourOptions: []
    property var minuteOptions: []
    property var lastSelectedDateTime: null
    property string selectionError: ""

    ListModel { id: dayOptionModel }
    ListModel { id: hourOptionModel }
    ListModel { id: minuteOptionModel }

    function twoDigits(value) {
        return value < 10 ? "0" + value : String(value)
    }

    function formatDateTime(dateTime) {
        return dateTime.getFullYear() + "-" + twoDigits(dateTime.getMonth() + 1)
                + "-" + twoDigits(dateTime.getDate()) + " "
                + twoDigits(dateTime.getHours()) + ":" + twoDigits(dateTime.getMinutes())
    }

    function moveTumblerByWheel(tumbler, wheelDelta) {
        if (wheelDelta === 0 || tumbler.count === 0)
            return

        // 一次滚轮事件只移动一项，触控板的小幅滚动也能稳定切换。
        var direction = wheelDelta > 0 ? -1 : 1
        tumbler.currentIndex = Math.max(0, Math.min(tumbler.count - 1,
                                                    tumbler.currentIndex + direction))
    }

    function createOptions() {
        var now = new Date()
        now.setSeconds(0, 0)
        now.setMinutes(now.getMinutes() + 1)

        dayOptions = []
        hourOptions = []
        minuteOptions = []
        dayOptionModel.clear()
        hourOptionModel.clear()
        minuteOptionModel.clear()
        for (var dayOffset = 0; dayOffset < 366; ++dayOffset) {
            var candidate = new Date(now.getFullYear(), now.getMonth(), now.getDate() + dayOffset)
            dayOptions.push({
                "year": candidate.getFullYear(),
                "month": candidate.getMonth(),
                "day": candidate.getDate(),
                "label": twoDigits(candidate.getMonth() + 1) + "月" + twoDigits(candidate.getDate()) + "日"
            })
            dayOptionModel.append({ "label": dayOptions[dayOffset].label })
        }
        for (var hour = 0; hour < 24; ++hour) {
            hourOptions.push({ "value": hour, "label": twoDigits(hour) + "时" })
            hourOptionModel.append({ "label": hourOptions[hour].label })
        }
        for (var minute = 0; minute < 60; ++minute) {
            minuteOptions.push({ "value": minute, "label": twoDigits(minute) + "分" })
            minuteOptionModel.append({ "label": minuteOptions[minute].label })
        }
        return now
    }

    function defaultScheduledAt() {
        var now = new Date()
        // 由于选择器不含秒，向上取整到下一分钟可保证默认值不会落入过去。
        now.setSeconds(0, 0)
        now.setMinutes(now.getMinutes() + 1)
        return formatDateTime(now)
    }

    function openForDateTime(dateTime) {
        var minimumDateTime = createOptions()
        var selected = dateTime && dateTime.getTime && dateTime.getTime() > Date.now()
                ? dateTime : minimumDateTime

        dateTumbler.currentIndex = 0
        hourTumbler.currentIndex = selected.getHours()
        minuteTumbler.currentIndex = selected.getMinutes()
        selectionError = ""
        open()
    }

    function confirmSelection() {
        var selectedDay = dayOptions[dateTumbler.currentIndex]
        var selectedHour = hourOptions[hourTumbler.currentIndex]
        var selectedMinute = minuteOptions[minuteTumbler.currentIndex]
        var selectedDateTime = new Date(selectedDay.year, selectedDay.month, selectedDay.day,
                                        selectedHour.value, selectedMinute.value)
        if (selectedDateTime.getTime() <= Date.now()) {
            selectionError = "计划开始时间必须晚于当前时间"
            return
        }

        lastSelectedDateTime = selectedDateTime
        timeSelected(formatDateTime(selectedDateTime))
        close()
    }

    background: Rectangle {
        radius: 14
        color: "#ffffff"
        border.color: "#dbe3ef"
    }

    contentItem: Item {
        implicitWidth: 486
        implicitHeight: 414

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 16

            Text {
                text: "选择计划开始时间"
                color: "#0f172a"
                font.pixelSize: 22
                font.bold: true
            }

            Text {
                text: "最小可选时间为当前时间的下一分钟"
                color: "#64748b"
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 184
                radius: 10
                color: "#f8fafc"
                border.color: "#e2e8f0"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Tumbler {
                        id: dateTumbler

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: dayOptionModel
                        visibleItemCount: 3
                        delegate: Text {
                            required property string label

                            width: dateTumbler.availableWidth
                            text: label
                            color: Tumbler.displacement === 0 ? "#0f172a" : "#94a3b8"
                            font.pixelSize: Tumbler.displacement === 0 ? 17 : 14
                            font.bold: Tumbler.displacement === 0
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        // 仅接收鼠标和触控板，触屏拖拽仍由 Tumbler 原生处理。
                        WheelHandler {
                            target: null
                            orientation: Qt.Vertical
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => root.moveTumblerByWheel(dateTumbler, event.angleDelta.y)
                        }
                    }

                    Tumbler {
                        id: hourTumbler

                        Layout.preferredWidth: 100
                        Layout.fillHeight: true
                        model: hourOptionModel
                        visibleItemCount: 3
                        delegate: Text {
                            required property string label

                            width: hourTumbler.availableWidth
                            text: label
                            color: Tumbler.displacement === 0 ? "#0f172a" : "#94a3b8"
                            font.pixelSize: Tumbler.displacement === 0 ? 17 : 14
                            font.bold: Tumbler.displacement === 0
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        // 仅接收鼠标和触控板，触屏拖拽仍由 Tumbler 原生处理。
                        WheelHandler {
                            target: null
                            orientation: Qt.Vertical
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => root.moveTumblerByWheel(hourTumbler, event.angleDelta.y)
                        }
                    }

                    Tumbler {
                        id: minuteTumbler

                        Layout.preferredWidth: 100
                        Layout.fillHeight: true
                        model: minuteOptionModel
                        visibleItemCount: 3
                        delegate: Text {
                            required property string label

                            width: minuteTumbler.availableWidth
                            text: label
                            color: Tumbler.displacement === 0 ? "#0f172a" : "#94a3b8"
                            font.pixelSize: Tumbler.displacement === 0 ? 17 : 14
                            font.bold: Tumbler.displacement === 0
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        // 仅接收鼠标和触控板，触屏拖拽仍由 Tumbler 原生处理。
                        WheelHandler {
                            target: null
                            orientation: Qt.Vertical
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => root.moveTumblerByWheel(minuteTumbler, event.angleDelta.y)
                        }
                    }
                }
            }

            Text {
                visible: root.selectionError.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? implicitHeight : 0
                text: root.selectionError
                color: "#dc2626"
                font.pixelSize: 12
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 102
                    Layout.preferredHeight: 46
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
                    Layout.preferredHeight: 46
                    radius: 8
                    text: "确定时间"
                    onClicked: root.confirmSelection()
                }
            }
        }
    }
}
