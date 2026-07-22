import QtQuick
import QtQuick.Controls
import "pages/auth"
import "pages/meeting"

Window {
    id: root

    width: 1180
    height: 760
    visible: true
    minimumWidth: 980
    minimumHeight: 640
    title: qsTr("SyncRTC")
    color: "#f5f8ff"

    property string loggedInUsername: ""
    property string loggedInEmail: ""

    // 仅用于未接入后端时预览会议主界面；默认仍从登录页进入。
    property bool isUiPreviewMode: Qt.application.arguments.indexOf("--preview-main-ui") !== -1
    // 以下账号信息是主界面预览的写死展示数据，不代表真实登录用户。
    property string previewUsername: "沈晟轩"
    property string previewEmail: "shenyuxuan@example.com"

    signal loginSucceeded(string username, string email)

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: root.isUiPreviewMode ? meetingShell : loginPage
    }

    Component {
        id: loginPage

        LoginPage {
            onLoginRequested: function(account, password) {
                authController.loginController.LoginRequest(account, password)
            }

            onLoginSucceeded: function(username, email) {
                root.loggedInUsername = username
                root.loggedInEmail = email
                root.loginSucceeded(username, email)
                stackView.replace(meetingShell)
            }

            onRegisterRequested: stackView.push(registerPage)
            onForgotPasswordRequested: stackView.push(forgotPasswordPage)
        }
    }

    Component {
        id: registerPage

        RegisterPage {
            onBackRequested: stackView.pop()
            onVerificationCodeRequested: function(email) {
                authController.registercontroller.GetVarifyCodeAsync(email)
            }
            onRegisterRequested: function(username, email, code, password, comfirm) {
                authController.registercontroller.RegisterRequest(username, email, code, password, comfirm)
            }
            onLoginRequested: stackView.pop()
        }
    }

    Component {
        id: forgotPasswordPage

        ForgotPasswordPage {
            onBackRequested: stackView.pop()
            onVerificationCodeRequested: function(email) {
                authController.passwordResetController.GetVarifyCodeAsync(email)
            }
            onResetPasswordRequested: function(email, code, password) {
                authController.passwordResetController.ReSetPassword("", email, code, password)
            }
            onResetFinished: stackView.pop()
            onLoginRequested: stackView.pop()
        }
    }

    Component {
        id: meetingShell

        MeetingShell {
            previewUsername: root.isUiPreviewMode ? root.previewUsername : ""
            previewEmail: root.isUiPreviewMode ? root.previewEmail : ""
        }
    }
}
