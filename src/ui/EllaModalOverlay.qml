import QtQuick
import QtQuick.Layouts

Item {
    id: root
    visible: shown
    enabled: shown
    z: 2000

    property bool shown: false
    property int dialogWidth: 560
    property int dialogMaxHeight: Math.max(260, height - 120)
    property bool closeOnBackdropClick: true

    default property alias contentData: modalBody.data

    signal dismissed()

    Rectangle {
        anchors.fill: parent
        color: "#660f172a"
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.closeOnBackdropClick
        onClicked: root.dismissed()
    }

    Rectangle {
        id: modalFrame
        width: Math.min(root.width - 40, root.dialogWidth)
        height: Math.min(root.dialogMaxHeight, modalBody.implicitHeight + 28)
        radius: 14
        color: "#ffffff"
        border.color: "#cfd9e5"
        border.width: 1
        anchors.centerIn: parent
        clip: true

        Flickable {
            anchors.fill: parent
            anchors.margins: 14
            contentWidth: width
            contentHeight: modalBody.implicitHeight
            clip: true

            ColumnLayout {
                id: modalBody
                width: parent.width
                spacing: 10
            }
        }
    }
}
