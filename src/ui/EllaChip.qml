import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: chip

    property EllaTokens tokens: EllaTokens {}
    property string labelText: ""
    property color chipColor: tokens.panelSoft
    property color chipBorder: tokens.border
    property color chipText: tokens.textSecondary
    property color bulletColor: chipText
    property bool showBullet: true

    radius: 999
    color: chipColor
    border.color: chipBorder
    border.width: 1
    implicitHeight: chipRow.implicitHeight + 10
    implicitWidth: chipRow.implicitWidth + 16

    RowLayout {
        id: chipRow
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            visible: chip.showBullet
            width: 7
            height: 7
            radius: 4
            color: bulletColor
        }

        Label {
            text: labelText
            color: chipText
            font.family: tokens.fontFamily
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
