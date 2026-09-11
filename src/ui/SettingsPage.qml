import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Page {
    id: root
    property bool darkMode: false
    property bool reducedMotion: false
    property var storage: ({})
    property var watched: []
    signal darkModeRequested(bool value)
    signal reducedMotionRequested(bool value)
    signal addSources()
    signal notify(string message)
    EllaTokens { id: theme; darkMode: root.darkMode; reducedMotion: root.reducedMotion }
    background: Rectangle { color: theme.bg }

    function refresh() {
        storage = workspaceService.storageStatus()
        watched = libraryService.watchedFolders()
        componentManager.refresh()
    }
    function bytes(value) {
        const amount = Number(value || 0)
        if (amount >= 1073741824) return (amount / 1073741824).toFixed(1) + " GB"
        if (amount >= 1048576) return (amount / 1048576).toFixed(1) + " MB"
        return (amount / 1024).toFixed(0) + " KB"
    }
    Component.onCompleted: refresh()
    Connections {
        target: workspaceService
        function onOperationFinished(result) {
            root.notify(result.ok ? (result.restartRequired ? "Restore checked. Restart ELLA to apply it." : "Library operation completed.") : (result.error || "Library operation failed."))
            root.refresh()
        }
    }
    Connections { target: libraryService; function onChanged() { root.refresh() } }
    Connections { target: componentManager; function onStateChanged() { if (componentManager.lastError) root.notify(componentManager.lastError) } }
    Connections { target: semanticSearchService; function onIndexFinished(ok, message) { root.notify(ok ? message : (message || "Could not build the semantic index.")); root.refresh() } }

    FileDialog {
        id: backupDialog
        title: "Save ELLA library backup"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ELLA library backup (*.ella-backup)"]
        defaultSuffix: "ella-backup"
        onAccepted: workspaceService.backupLibrary(selectedFile)
    }
    FileDialog {
        id: restoreDialog
        title: "Choose an ELLA library backup"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ELLA library backup (*.ella-backup)"]
        onAccepted: workspaceService.restoreLibrary(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: "Export notes and annotations"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ELLA knowledge export (*.ella-export)"]
        defaultSuffix: "ella-export"
        onAccepted: workspaceService.exportKnowledge(selectedFile)
    }
    FileDialog {
        id: manifestDialog
        title: "Load an audited component catalogue"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON catalogue (*.json)"]
        onAccepted: if (!componentManager.loadManifest(selectedFile)) root.notify(componentManager.lastError)
    }
    FileDialog {
        id: localComponentDialog
        property string componentId: ""
        title: "Choose an existing component"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Component files (*.exe *.com *.bin)", "All files (*)"]
        onAccepted: if (!componentManager.useLocalPath(componentId, selectedFile)) root.notify(componentManager.lastError)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 20
            anchors.margins: 28
            Item { Layout.preferredHeight: 8 }
            Label { text: "Settings"; font.pixelSize: 30; font.bold: true; color: theme.textPrimary }
            Label { text: "Your library stays local. Choose what ELLA stores and which optional tools it may use."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary }

            SettingsCard {
                title: "Appearance"
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Dark theme"; Layout.fillWidth: true; color: theme.textPrimary }
                    Switch { checked: root.darkMode; Accessible.name: "Dark theme"; onToggled: root.darkModeRequested(checked) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Reduce animation"; Layout.fillWidth: true; color: theme.textPrimary }
                    Switch { checked: root.reducedMotion; Accessible.name: "Reduce animation"; onToggled: root.reducedMotionRequested(checked) }
                }
            }

            SettingsCard {
                title: "Watched folders"
                Label { visible: root.watched.length === 0; text: "No watched folders yet. Add a folder and ELLA will keep it current every 15 minutes."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary }
                Repeater {
                    model: root.watched
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle; color: theme.textPrimary }
                            Label { text: modelData.lastError || (modelData.lastScanAt ? "Last checked " + String(modelData.lastScanAt).slice(0, 16) : "Waiting for first scan"); color: modelData.lastError ? theme.dangerText : theme.textMuted; font.pixelSize: 11 }
                        }
                        EllaButton { tokens: theme; text: "Remove watch"; onClicked: libraryService.removeWatchedFolder(Number(modelData.id)) }
                    }
                }
                RowLayout {
                    EllaButton { tokens: theme; text: "+ Add sources"; tone: "primary"; onClicked: root.addSources() }
                    EllaButton { tokens: theme; text: "Scan now"; enabled: !libraryService.activity.running; onClicked: libraryService.scanNow() }
                }
            }

            SettingsCard {
                title: "Storage and ownership"
                Label { text: "Database  " + root.bytes(root.storage.databaseBytes) + "    Preview cache  " + root.bytes(root.storage.cacheBytes) + "    Notes  " + root.bytes(root.storage.notesBytes) + "    Components  " + root.bytes(root.storage.componentBytes); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary }
                Label { text: "Backups include catalog metadata, notes, annotations, tags, saved searches, and watched-folder settings. Original source files are never copied."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary; font.pixelSize: 12 }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    EllaButton { tokens: theme; text: "Back up library…"; tone: "primary"; enabled: !workspaceService.busy; onClicked: backupDialog.open() }
                    EllaButton { tokens: theme; text: "Restore…"; enabled: !workspaceService.busy; onClicked: restoreDialog.open() }
                    EllaButton { tokens: theme; text: "Export knowledge…"; enabled: !workspaceService.busy; onClicked: exportDialog.open() }
                    EllaButton { tokens: theme; text: "Clear preview cache"; enabled: !workspaceService.busy; onClicked: workspaceService.clearDerivedCaches() }
                    EllaButton { tokens: theme; text: "Clear local history"; onClicked: root.notify(workspaceService.clearHistory() ? "Search and reading history cleared." : "Could not clear history.") }
                }
                ProgressBar { visible: workspaceService.busy; indeterminate: true; Layout.fillWidth: true }
                Label { visible: workspaceService.busy; text: workspaceService.status; color: theme.textSecondary }
            }

            SettingsCard {
                title: "Optional capabilities"
                Label { text: "The core app remains useful without downloads. Install components only when you need OCR, transcription, faithful Office rendering, media playback, or meaning-based search."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary }
                Repeater {
                    model: componentManager.components
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: componentRow.implicitHeight + 22; radius: 10; color: theme.panelSoft; border.color: theme.border
                        RowLayout {
                            id: componentRow; anchors.fill: parent; anchors.margins: 11; spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.name; font.bold: true; color: theme.textPrimary }
                                Label { text: modelData.description; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary; font.pixelSize: 11 }
                                Label { text: modelData.installed ? "Installed · " + root.bytes(modelData.installedBytes) : (modelData.available ? "Download · " + root.bytes(modelData.downloadBytes) : "No audited download published — choose a compatible local tool"); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: modelData.installed || modelData.configured ? theme.accent : theme.textMuted; font.pixelSize: 10 }
                            }
                            ColumnLayout {
                                EllaButton { tokens: theme; text: modelData.installed ? "Remove" : "Install"; enabled: !componentManager.busy && (modelData.installed || modelData.available); onClicked: modelData.installed ? componentManager.remove(modelData.id) : componentManager.install(modelData.id) }
                                EllaButton { tokens: theme; text: "Choose local…"; enabled: !componentManager.busy && !modelData.installed; onClicked: { localComponentDialog.componentId = modelData.id; localComponentDialog.open() } }
                            }
                        }
                    }
                }
                ProgressBar { visible: componentManager.busy; from: 0; to: 1; value: componentManager.progress; Layout.fillWidth: true }
                RowLayout {
                    EllaButton { tokens: theme; text: "Load audited catalogue…"; enabled: !componentManager.busy; onClicked: manifestDialog.open() }
                    EllaButton { tokens: theme; text: "Rebuild meaning index"; visible: semanticSearchService.available; enabled: !semanticSearchService.busy; onClicked: semanticSearchService.rebuildIndex() }
                    EllaButton { tokens: theme; text: "Cancel installation"; visible: componentManager.busy; onClicked: componentManager.cancel() }
                }
                Label { visible: semanticSearchService.available; text: semanticSearchService.status; color: theme.textSecondary; font.pixelSize: 11 }
            }

            SettingsCard {
                title: "About and updates"
                Label { text: "ELLA " + String(fileListModel.releaseMetadata().version || "1.0") + " · " + String(fileListModel.releaseMetadata().channel || "preview"); color: theme.textPrimary }
                Label { text: "Open source under Apache-2.0. Update checks are manual and open the verified GitHub Releases page."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textSecondary }
                EllaButton { tokens: theme; text: "Check releases…"; onClicked: workspaceService.openReleasePage() }
            }
            Item { Layout.preferredHeight: 20 }
        }
    }

    component SettingsCard: Rectangle {
        id: card
        default property alias contents: body.data
        property string title: ""
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + 34
        radius: 14; color: theme.panel; border.color: theme.border
        ColumnLayout {
            id: body; anchors.fill: parent; anchors.margins: 17; spacing: 12
            Label { text: card.title; font.pixelSize: 18; font.bold: true; color: theme.textPrimary; Layout.fillWidth: true }
        }
    }
}
