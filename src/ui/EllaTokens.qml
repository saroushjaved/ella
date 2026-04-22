import QtQuick

QtObject {
    id: tokens

    readonly property string fontFamily: "Segoe UI"

    readonly property color bg: "#f3f5f8"
    readonly property color panel: "#ffffff"
    readonly property color panelSoft: "#f8fafc"
    readonly property color panelMuted: "#eef3f8"

    readonly property color border: "#d9e2ec"
    readonly property color borderSoft: "#c9d5e2"

    readonly property color textPrimary: "#0f172a"
    readonly property color textSecondary: "#475569"
    readonly property color textMuted: "#64748b"

    readonly property color accent: "#0f766e"
    readonly property color accentHover: "#115e59"
    readonly property color accentSoft: "#e6f4f1"
    readonly property color accentBorder: "#a7d9d2"

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

    readonly property color sidebarA: "#0f172a"
    readonly property color sidebarB: "#111827"
    readonly property color sidebarC: "#1f2937"
    readonly property color sidebarText: "#e5e7eb"
    readonly property color sidebarMutedText: "#94a3b8"
    readonly property color sidebarActiveBg: "#14343a"
    readonly property color sidebarActiveBorder: "#14b8a6"
    readonly property color sidebarActiveText: "#ccfbf1"

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
