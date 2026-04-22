import QtQuick
import QtQuick.Controls

Button {
    id: control

    property EllaTokens tokens: EllaTokens {}

    property color fillColor: {
        if (tone === "primary")
            return tokens.accent
        if (tone === "success")
            return tokens.successBg
        if (tone === "danger")
            return tokens.dangerBg
        if (tone === "ghost")
            return tokens.panel
        return tokens.panelSoft
    }
    property color borderLine: {
        if (tone === "primary")
            return tokens.accent
        if (tone === "success")
            return tokens.successBorder
        if (tone === "danger")
            return tokens.dangerBorder
        if (tone === "ghost")
            return tokens.border
        return tokens.border
    }
    property color labelColor: {
        if (tone === "primary")
            return "#ffffff"
        if (tone === "success")
            return tokens.successText
        if (tone === "danger")
            return tokens.dangerText
        return tokens.textPrimary
    }
    property int radiusValue: tokens.radiusMd
    property string tone: "secondary" // primary, secondary, ghost, success, danger

    hoverEnabled: true
    implicitHeight: 40
    leftPadding: 14
    rightPadding: 14
    font.family: tokens.fontFamily
    font.pixelSize: 14

    background: Rectangle {
        radius: control.radiusValue
        color: {
            if (!control.enabled)
                return Qt.darker(control.fillColor, 1.02)
            if (control.down)
                return Qt.darker(control.fillColor, 1.08)
            if (control.hovered)
                return Qt.lighter(control.fillColor, 1.02)
            return control.fillColor
        }
        border.color: control.enabled ? control.borderLine : Qt.darker(control.borderLine, 1.03)
        border.width: 1
        opacity: control.enabled ? 1.0 : 0.6
    }

    contentItem: Text {
        text: control.text
        color: control.labelColor
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
