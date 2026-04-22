import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    EllaTokens { id: tokens }
    property var appReleaseInfo: (fileListModel && fileListModel.releaseMetadata)
                                 ? fileListModel.releaseMetadata()
                                 : (releaseMetadata || {})
    width: 1540
    height: 920
    visible: true
    icon: "qrc:/qt/qml/SecondBrain/src/ui/assets/ella_icon_256.png"
    title: "ELLA (Memory Browser)"
    color: tokens.bg
    font.family: tokens.fontFamily

    Component.onCompleted: {
        if (fileListModel && fileListModel.shouldShowBetaScopeNotice && fileListModel.shouldShowBetaScopeNotice()) {
            betaScopeDialog.open()
        }
    }

    footer: Rectangle {
        height: 26
        color: "#ffffff"
        border.color: tokens.border

        Label {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            text: "ELLA " + String(appReleaseInfo.version || "0.9.0-beta")
                  + " \u2022 " + String(appReleaseInfo.channel || "beta").toUpperCase()
                  + " \u2022 Build " + String(appReleaseInfo.buildId || "local")
            font.pixelSize: 11
            color: tokens.textSecondary
        }
    }

    Component {
        id: homePageComponent

        HomePage {
            navigationStack: stackView

            onOpenKnowledgeLibrary: function(initialSearchText, launchAction, focusFileId) {
                stackView.push(browserPageComponent, {
                    "initialSearchText": initialSearchText || "",
                    "launchAction": launchAction || "",
                    "initialFocusFileId": focusFileId !== undefined ? Number(focusFileId) : -1
                })
            }
        }
    }

    Component {
        id: browserPageComponent

        BrowserPage {
            navigationStack: stackView

            onRequestOpenReader: function(fileIndex, fileDetails) {
                stackView.push(readerPageComponent, {
                    "fileIndex": fileIndex,
                    "fileDetails": fileDetails,
                    "navigationStack": stackView
                })
            }
        }
    }

    Component {
        id: readerPageComponent

        ReaderPage {
        }
    }

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: homePageComponent

        pushEnter: Transition {
            NumberAnimation {
                property: "x"
                from: stackView.width
                to: 0
                duration: 180
                easing.type: Easing.OutCubic
            }
        }

        popExit: Transition {
            NumberAnimation {
                property: "x"
                from: 0
                to: stackView.width
                duration: 160
                easing.type: Easing.InCubic
            }
        }
    }

    Dialog {
        id: betaScopeDialog
        modal: true
        width: 660
        title: "ELLA Closed Beta Scope"
        anchors.centerIn: Overlay.overlay

        onAccepted: {
            if (fileListModel && fileListModel.markBetaScopeNoticeSeen) {
                fileListModel.markBetaScopeNoticeSeen()
            }
        }

        onRejected: {
            if (fileListModel && fileListModel.markBetaScopeNoticeSeen) {
                fileListModel.markBetaScopeNoticeSeen()
            }
        }

        contentItem: Column {
            spacing: 10
            padding: 16

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "This beta prioritizes local-first workflows: import, indexing, search, browser/reader, notes/annotations, and remove-from-ELLA."
                color: tokens.textPrimary
                font.pixelSize: 14
            }

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Cloud sync remains available as Experimental during this beta and is not part of acceptance gating."
                color: tokens.textSecondary
                font.pixelSize: 13
            }

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "When reporting issues, include diagnostics export plus reproduction steps."
                color: tokens.textSecondary
                font.pixelSize: 13
            }
        }

        standardButtons: Dialog.Ok
    }
}
