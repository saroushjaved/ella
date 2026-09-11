import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    property bool darkMode: false
    property var paths: []
    property var preview: ({})
    property bool previewBusy: false
    property bool previewValid: false
    signal importStarted()
    EllaTokens { id: theme; darkMode: root.darkMode }
    title: "Bring your knowledge together"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, parent ? parent.width - 60 : 640)
    background: Rectangle { radius: 18; color: theme.panel; border.color: theme.border }
    function openChooser() { open(); if (paths.length === 0) folderChooser.open() }
    function prepare(values) { paths = values.map(function(value) { return String(value) }); open(); updatePreview() }
    function exclusions() { return exclusionsField.text.split(",").map(function(value) { return value.trim() }).filter(function(value) { return value !== "" }) }
    function updatePreview() {
        if (!paths.length) return
        previewBusy = true; previewValid = false
        libraryService.previewImport(paths, exclusions())
    }
    function bytes(value) { return Number(value) >= 1073741824 ? (Number(value) / 1073741824).toFixed(1) + " GB" : (Number(value || 0) / 1048576).toFixed(1) + " MB" }
    Connections {
        target: libraryService
        function onPreviewReady(result) { root.preview = result; root.previewBusy = false; root.previewValid = true }
    }
    FileDialog { id: fileChooser; title: "Choose sources"; fileMode: FileDialog.OpenFiles; onAccepted: root.prepare(selectedFiles) }
    FolderDialog { id: folderChooser; title: "Choose a folder to watch"; onAccepted: root.prepare([selectedFolder]) }
    contentItem: ColumnLayout {
        spacing: 17
        Label { Layout.fillWidth: true; text: "Choose sources. Review the work. Then make them searchable."; wrapMode: Text.WordWrap; color: theme.textSecondary }
        RowLayout {
            EllaButton { tokens: theme; text: "Choose folder…"; tone: "primary"; onClicked: folderChooser.open() }
            EllaButton { tokens: theme; text: "Choose files…"; onClicked: fileChooser.open() }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: selectedPaths.implicitHeight + 24; radius: 10; color: theme.panelSoft; border.color: theme.border
            Label { id: selectedPaths; anchors.fill: parent; anchors.margins: 12; text: root.paths.length ? root.paths.slice(0, 4).join("\n") + (root.paths.length > 4 ? "\n+ " + (root.paths.length - 4) + " more" : "") : "No sources selected yet"; color: theme.textPrimary; wrapMode: Text.WrapAnywhere; font.pixelSize: 12 }
        }
        Label { text: "SKIP FOLDERS AND PATTERNS"; font.pixelSize: 10; font.letterSpacing: 1; color: theme.textMuted }
        EllaTextField { id: exclusionsField; tokens: theme; Layout.fillWidth: true; text: ".git, node_modules, build, *.tmp"; placeholderText: "Folder names or patterns, separated by commas"; onTextEdited: { root.previewValid = false; previewTimer.restart() } }
        Timer { id: previewTimer; interval: 500; onTriggered: root.updatePreview() }
        CheckBox { id: watch; text: "Keep folders up to date automatically"; checked: true; Accessible.description: "Watch source folders and reconcile every 15 minutes." }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: estimateColumn.implicitHeight + 28; radius: 10; color: theme.accentSoft
            ColumnLayout {
                id: estimateColumn; anchors.fill: parent; anchors.margins: 14; spacing: 8
                Label { text: root.previewBusy ? "Reviewing your sources…" : (root.previewValid ? Number(root.preview.supportedFiles || 0) + " supported sources · " + root.bytes(root.preview.totalBytes) : "Your import preview will appear here."); font.bold: true; color: theme.textPrimary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { visible: root.previewValid; text: Number(root.preview.unsupportedFiles || 0) + " unsupported files skipped. Estimated processing: " + Math.max(1, Math.ceil(Number(root.preview.estimatedSeconds || 0) / 60)) + " min. Scanned pages and media may take longer."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                Label { visible: !!root.preview.errors && Number(root.preview.errors.length || 0) > 0; text: (root.preview.errors || []).join("\n"); color: theme.dangerText; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                ProgressBar { visible: root.previewBusy; indeterminate: true; Layout.fillWidth: true }
            }
        }
        Label { text: "Original files stay in place. OCR, transcription and faithful Office rendering are optional components; text search works without them."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            EllaButton { tokens: theme; text: "Cancel"; onClicked: root.close() }
            EllaButton { tokens: theme; text: "Start indexing"; tone: "primary"; enabled: root.previewValid && !root.previewBusy && Number(root.preview.supportedFiles || 0) > 0; onClicked: { libraryService.startImport(root.paths, root.exclusions(), watch.checked); root.close(); root.importStarted() } }
        }
    }
}
