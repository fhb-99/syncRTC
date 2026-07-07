import QtQuick
import QtQuick.Controls
import "pages/auth"

Window {
    id: root

    width: 1180
    height: 760
    visible: true
    minimumWidth: 980
    minimumHeight: 640
    title: qsTr("SyncRTC")
    color: "#f5f8ff"

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: loginPage
    }

    Component {
        id: loginPage

        LoginPage {
            onLoginRequested: function(account, password) {
                console.log("login requested:", account)
            }

            onRegisterRequested: stackView.push(registerPage)
            onForgotPasswordRequested: stackView.push(forgotPasswordPage)
        }
    }

    Component {
        id: registerPage

        RegisterPage {
            onBackRequested: stackView.pop()
            onRegisterFinished: stackView.pop()
            onLoginRequested: stackView.pop()
        }
    }

    Component {
        id: forgotPasswordPage

        ForgotPasswordPage {
            onBackRequested: stackView.pop()
            onResetFinished: stackView.pop()
            onLoginRequested: stackView.pop()
        }
    }
}
