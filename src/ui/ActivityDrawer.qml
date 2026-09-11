import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: root
    property bool darkMode: false
    property var indexing: ({})
    EllaTokens { id: theme; darkMode: root.darkMode }
    background: Rectangle { color: theme.panel; border.color: theme.border }
    onOpened: indexing = fileListModel.indexStatus()
    Connections { target: fileListModel; function onIndexStatusChanged() { root.indexing = fileListModel.indexStatus() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 26; spacing: 20
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Library activity"; color: theme.textPrimary; font.pixelSize: 24; font.bold: true }
            Item { Layout.fillWidth: true }
            EllaButton { tokens: theme; text: "×"; Accessible.name: "Close activity"; onClicked: root.close() }
        }
        Label { text: "ELLA keeps working while you read."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: jobColumn.implicitHeight + 32; radius: 12; color: theme.panelSoft; border.color: theme.border
            ColumnLayout {
                id: jobColumn; anchors.fill: parent; anchors.margins: 16; spacing: 12
                Label { text: "SOURCE LIBRARY"; color: theme.textMuted; font.pixelSize: 10; font.letterSpacing: 1 }
                Label { text: libraryService.activity.status || "Ready"; color: theme.textPrimary; font.pixelSize: 18; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                ProgressBar { Layout.fillWidth: true; visible: !!libraryService.activity.running; indeterminate: !Number(libraryService.activity.total); from: 0; to: Math.max(1, Number(libraryService.activity.total || 1)); value: Number(libraryService.activity.completed || 0) }
                Label { text: Number(libraryService.activity.completed || 0) + " of " + Number(libraryService.activity.total || 0) + " sources processed"; color: theme.textSecondary; font.pixelSize: 12 }
                Label { visible: !!libraryService.activity.lastError; text: libraryService.activity.lastError || ""; color: theme.dangerText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                RowLayout {
                    EllaButton { tokens: theme; text: "Pause"; enabled: !!libraryService.activity.running; onClicked: libraryService.pause() }
                    EllaButton { tokens: theme; text: "Resume"; enabled: libraryService.activity.status === "paused"; onClicked: libraryService.resume() }
                    EllaButton { tokens: theme; text: "Cancel"; enabled: !!libraryService.activity.running || libraryService.activity.status === "paused"; onClicked: libraryService.cancel() }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: indexColumn.implicitHeight + 32; radius: 12; color: theme.panelSoft; border.color: theme.border
            ColumnLayout {
                id: indexColumn; anchors.fill: parent; anchors.margins: 16; spacing: 12
                Label { text: "SEARCH INDEX"; color: theme.textMuted; font.pixelSize: 10; font.letterSpacing: 1 }
                Label { text: root.indexing.running ? "Extracting searchable passages" : "Ready to search"; color: theme.textPrimary; font.pixelSize: 17; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                ProgressBar { Layout.fillWidth: true; visible: !!root.indexing.running; from: 0; to: Math.max(1, Number(root.indexing.total || 1)); value: Number(root.indexing.processed || 0) }
                Label { text: Number(root.indexing.processed || 0) + " processed · " + Number(root.indexing.failed || 0) + " need attention"; color: theme.textSecondary; font.pixelSize: 12 }
                Label { visible: !!root.indexing.lastError; text: root.indexing.lastError || ""; color: theme.dangerText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        EllaButton { tokens: theme; text: "Scan watched folders now"; Layout.fillWidth: true; tone: "primary"; enabled: !libraryService.activity.running; onClicked: libraryService.scanNow() }
        Label { text: "Folder changes are checked automatically and reconciled every 15 minutes. Missing sources are kept for relinking."; color: theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Item { Layout.fillHeight: true }
    }
}
