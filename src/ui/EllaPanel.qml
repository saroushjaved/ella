import QtQuick

Rectangle {
    id: panel

    property EllaTokens tokens: EllaTokens {}
    property color fillColor: tokens.panel
    property color borderLine: tokens.border
    property int radiusValue: tokens.radiusLg

    radius: radiusValue
    color: fillColor
    border.color: borderLine
    border.width: 1
}
