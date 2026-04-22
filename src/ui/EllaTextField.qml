import QtQuick
import QtQuick.Controls

TextField {
    id: field

    property EllaTokens tokens: EllaTokens {}

    implicitHeight: 42
    color: tokens.textPrimary
    font.family: tokens.fontFamily
    font.pixelSize: 15
    placeholderTextColor: "#758398"
    selectionColor: tokens.accentSoft
    selectedTextColor: tokens.textPrimary

    background: Rectangle {
        radius: tokens.radiusMd
        color: tokens.panel
        border.color: {
            if (!field.enabled)
                return Qt.darker(tokens.border, 1.03)
            if (field.activeFocus)
                return tokens.accent
            if (field.hovered)
                return tokens.borderSoft
            return tokens.border
        }
        border.width: 1
        opacity: field.enabled ? 1.0 : 0.75
    }
}
