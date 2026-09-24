import QtQuick
import QtTest
import "../ui/pages/meeting/dialogs" as Dialogs

TestCase {
    name: "MeetingDialogs"
    when: true

    Component {
        id: createMeetingDialogComponent

        Dialogs.CreateMeetingDialog { }
    }

    Component {
        id: timePickerComponent

        Dialogs.MeetingTimePickerPopup { }
    }

    function test_dialogsCanBeCreatedAfterDirectoryGrouping() {
        var createDialog = createMeetingDialogComponent.createObject(null)
        var timePicker = timePickerComponent.createObject(null)

        verify(createDialog !== null)
        verify(timePicker !== null)

        createDialog.destroy()
        timePicker.destroy()
    }
}
