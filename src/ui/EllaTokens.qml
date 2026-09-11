import QtQuick

QtObject {
    id: tokens

    property bool darkMode: false
    property bool reducedMotion: false

    readonly property string fontFamily: "Segoe UI"

    readonly property color bg: darkMode ? "#181d1c" : "#f5f4f0"
    readonly property color panel: darkMode ? "#212826" : "#ffffff"
    readonly property color panelSoft: darkMode ? "#252e2b" : "#faf9f6"
    readonly property color panelMuted: darkMode ? "#2d3833" : "#eeeee8"

    readonly property color border: darkMode ? "#39463f" : "#e1e3dc"
    readonly property color borderSoft: darkMode ? "#53635b" : "#c8d0c6"

    readonly property color textPrimary: darkMode ? "#eff3ed" : "#24352f"
    readonly property color textSecondary: darkMode ? "#bdc9c0" : "#617067"
    readonly property color textMuted: darkMode ? "#a2b1a7" : "#6f7b72"

    readonly property color accent: darkMode ? "#46b9a5" : "#137c69"
    readonly property color accentHover: darkMode ? "#66cbb8" : "#0b6253"
    readonly property color accentSoft: darkMode ? "#23463c" : "#e6f3ec"
    readonly property color accentBorder: darkMode ? "#3f7563" : "#b4d9ca"

    readonly property color successBg: "#e7f8ef"
    readonly property color successBorder: "#34c07a"
    readonly property color successText: "#166534"

    readonly property color warningBg: "#fff7e5"
    readonly property color warningBorder: "#f4c167"
    readonly property color warningText: "#854d0e"

    readonly property color dangerBg: "#fef1f1"
    readonly property color dangerBorder: "#ef7d7d"
    readonly property color dangerText: "#b42318"

    readonly property color infoBg: "#e8f3fb"
    readonly property color infoBorder: "#84c5ee"
    readonly property color infoText: "#0f5f99"

    readonly property color sidebarA: darkMode ? "#1c2420" : "#efefe8"
    readonly property color sidebarB: panelSoft
    readonly property color sidebarC: border
    readonly property color sidebarText: textPrimary
    readonly property color sidebarMutedText: textSecondary
    readonly property color sidebarActiveBg: accentSoft
    readonly property color sidebarActiveBorder: accentBorder
    readonly property color sidebarActiveText: accent

    readonly property int radiusXs: 4
    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 16
    readonly property int radiusXl: 20

    readonly property int gapXs: 4
    readonly property int gapSm: 8
    readonly property int gapMd: 12
    readonly property int gapLg: 16
    readonly property int gapXl: 24

    readonly property int shadowSm: 4
    readonly property int shadowMd: 8
    readonly property int shadowLg: 12
}
