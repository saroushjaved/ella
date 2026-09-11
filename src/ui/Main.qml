import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 720
    visible: true
    title: "ELLA — Your personal research library"
    color: theme.bg
    font.family: theme.fontFamily
    palette.window: theme.bg
    palette.base: theme.panel
    palette.button: theme.panelSoft
    palette.text: theme.textPrimary
    palette.windowText: theme.textPrimary
    palette.buttonText: theme.textPrimary
    palette.highlight: theme.accent
    palette.highlightedText: theme.darkMode ? "#102b23" : "#ffffff"

    Settings {
        id: preferences
        category: "Appearance"
        property bool darkMode: false
        property bool reducedMotion: false
        property bool onboardingComplete: false
    }
    EllaTokens { id: theme; darkMode: preferences.darkMode; reducedMotion: preferences.reducedMotion }
    property string section: "home"
    property string sectionBeforeReader: "home"
    property string toastMessage: ""
    property bool undoAvailable: false
    property var collections: []

    function notify(message, undo) { toastMessage = message; undoAvailable = !!undo; toastTimer.restart() }
    function leaveCurrentPage() { return !stack.currentItem || !stack.currentItem.flushSave || stack.currentItem.flushSave() }
    function showHome() { if (!leaveCurrentPage()) return; section = "home"; stack.replace(homeComponent) }
    function showLibrary(query, mode, collectionId) {
        if (!leaveCurrentPage()) return
        section = mode || "library"
        stack.replace(browserComponent, {initialSearchText: query || "", viewMode: mode || "library", selectedCollectionId: collectionId === undefined ? -1 : collectionId})
    }
    function showNotes(fileId) {
        if (!leaveCurrentPage()) return
        section = "notes"
        stack.replace(notesComponent, {initialNoteId: fileId === undefined ? -1 : fileId})
    }
    function showSettings() { if (!leaveCurrentPage()) return; section = "settings"; stack.replace(settingsComponent) }
    function openReader(fileId, anchor) {
        const details = fileListModel.getDetailsById(fileId)
        if (!details.id) { notify("This source is no longer in the library."); return }
        if (fileListModel.isEllaNoteFileId(fileId)) { showNotes(fileId); return }
        fileListModel.trackRetrievalEvent("result_opened", "", fileId, 0, "", "{}")
        if (section !== "reader") sectionBeforeReader = section
        stack.push(readerComponent, {fileDetails: details, initialAnchor: anchor || ({})})
        section = "reader"
    }

    Component.onCompleted: {
        collections = fileListModel.getCollectionPickerOptions()
        if (applicationScreenshotMode) {
            preferences.darkMode = applicationScreenshotDark
            if (applicationStartupPage === "onboarding") welcome.open()
            else if (applicationStartupPage === "library") showLibrary("", "library")
            else if (applicationStartupPage === "notes") showNotes()
            else if (applicationStartupPage === "settings") showSettings()
            else if (applicationStartupPage === "reader" && applicationStartupFileId >= 0) openReader(applicationStartupFileId, ({}))
        } else if (!preferences.onboardingComplete) welcome.open()
    }
    onClosing: function(close) { if (!leaveCurrentPage()) close.accepted = false }
    Connections { target: libraryService; function onChanged() { collections = fileListModel.getCollectionPickerOptions() } }

    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: window.width < 1180 ? 188 : 220
            Layout.fillHeight: true
            color: theme.sidebarA
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                Item { Layout.preferredHeight: 8 }
                RowLayout {
                    spacing: 10
                    Rectangle {
                        width: 36; height: 40; radius: 10; color: theme.accent
                        Text { anchors.centerIn: parent; text: "e"; font.family: "Georgia"; font.bold: true; font.pixelSize: 34; color: theme.darkMode ? "#102b23" : "white" }
                    }
                    Label { text: "ELLA"; color: theme.textPrimary; font.pixelSize: 24; font.letterSpacing: 3; font.bold: true }
                }
                Label { text: "A place for what you know."; color: theme.textSecondary; font.pixelSize: 11; Layout.topMargin: 5 }
                Item { Layout.preferredHeight: 24 }
                EllaButton { tokens: theme; text: "+  Add sources"; tone: "primary"; Layout.fillWidth: true; onClicked: importer.openChooser() }
                Item { Layout.preferredHeight: 16 }
                Repeater {
                    model: [{key:"home",label:"Overview",icon:"⌂"},{key:"library",label:"Library",icon:"▤"},{key:"favorites",label:"Favorites",icon:"☆"},{key:"collections",label:"Collections",icon:"▱"},{key:"notes",label:"Notes",icon:"✎"}]
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 44
                        Accessible.name: modelData.label
                        background: Rectangle { radius: 9; color: window.section === modelData.key ? theme.accentSoft : (parent.hovered ? theme.panelMuted : "transparent"); border.color: parent.activeFocus ? theme.accent : "transparent" }
                        contentItem: Row {
                            spacing: 12
                            Label { width: 21; text: modelData.icon; color: window.section === modelData.key ? theme.accent : theme.textSecondary; font.pixelSize: 20 }
                            Label { text: modelData.label; color: window.section === modelData.key ? theme.accent : theme.textPrimary; font.pixelSize: 14; font.bold: window.section === modelData.key }
                        }
                        onClicked: {
                            if (modelData.key === "home") window.showHome()
                            else if (modelData.key === "notes") window.showNotes()
                            else window.showLibrary("", modelData.key)
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border; Layout.topMargin: 16; Layout.bottomMargin: 10 }
                Label { text: "YOUR COLLECTIONS"; font.pixelSize: 10; font.letterSpacing: 1.2; color: theme.textMuted }
                Repeater {
                    model: window.collections.slice(0, 6)
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 34
                        text: "·  " + (modelData.name || modelData.label || "Collection")
                        font.pixelSize: 12
                        onClicked: window.showLibrary("", "collections", Number(modelData.id))
                    }
                }
                Label { visible: window.collections.length === 0; text: "Organize at your own pace."; color: theme.textMuted; font.pixelSize: 11; Layout.topMargin: 8 }
                Item { Layout.fillHeight: true }
                EllaButton { tokens: theme; Layout.fillWidth: true; text: libraryService.activity.running ? "◌  Indexing sources…" : "◌  Activity"; tone: "ghost"; onClicked: activity.open() }
                EllaButton { tokens: theme; Layout.fillWidth: true; text: "Settings"; tone: "ghost"; onClicked: window.showSettings() }
                Label { text: "Local by design. Yours to keep."; font.pixelSize: 10; color: theme.textMuted; Layout.topMargin: 12 }
                Label { text: "Commands  ·  Ctrl+K"; font.pixelSize: 10; color: theme.textMuted }
            }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: theme.border }
        StackView {
            id: stack
            Layout.fillWidth: true; Layout.fillHeight: true
            initialItem: homeComponent
            pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: preferences.reducedMotion ? 0 : 140 } }
            pushExit: Transition { }
            popEnter: Transition { }
            popExit: Transition { }
            replaceEnter: Transition { }
            replaceExit: Transition { }
        }
    }
    Component { id: homeComponent; HomePage { darkMode: preferences.darkMode; onSearchRequested: function(query) { window.showLibrary(query) } onOpenSource: function(id) { window.openReader(id) } onOpenNotes: function(id) { window.showNotes(id) } onAddSources: importer.openChooser() } }
    Component { id: browserComponent; BrowserPage { darkMode: preferences.darkMode; onRequestOpenReader: function(id, anchor) { window.openReader(id, anchor) } onAddSources: importer.openChooser(); onImportPathsRequested: function(paths) { importer.prepare(paths) } onNotify: function(message, undo) { window.notify(message, undo) } } }
    Component { id: readerComponent; ReaderPage { darkMode: preferences.darkMode; navigationStack: stack; onRequestBack: { stack.pop(); window.section = window.sectionBeforeReader } onSourceNoteCreated: function(id) { window.showNotes(id) } onOpenRelatedSource: function(id, anchor) { window.openReader(id, anchor) } } }
    Component { id: notesComponent; NotesPage { darkMode: preferences.darkMode; onOpenSource: function(id) { window.openReader(id) } onNotify: function(message) { window.notify(message) } } }
    Component { id: settingsComponent; SettingsPage { darkMode: preferences.darkMode; reducedMotion: preferences.reducedMotion; onDarkModeRequested: function(value) { preferences.darkMode = value } onReducedMotionRequested: function(value) { preferences.reducedMotion = value } onAddSources: importer.openChooser(); onNotify: function(message) { window.notify(message) } } }

    ImportDialog { id: importer; darkMode: preferences.darkMode; onImportStarted: { preferences.onboardingComplete = true; activity.open() } }
    ActivityDrawer { id: activity; darkMode: preferences.darkMode; width: Math.min(440, window.width * 0.6); height: window.height; edge: Qt.RightEdge }
    Timer { id: toastTimer; interval: 9000; onTriggered: window.toastMessage = "" }
    Rectangle {
        z: 50; visible: window.toastMessage !== ""
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 24
        width: Math.min(680, window.width - 80); height: toastRow.implicitHeight + 24; radius: 12; color: theme.textPrimary
        RowLayout {
            id: toastRow; anchors.fill: parent; anchors.margins: 12
            Label { Layout.fillWidth: true; text: window.toastMessage; color: theme.panel; wrapMode: Text.WordWrap }
            Button { visible: window.undoAvailable; text: "Undo"; onClicked: { libraryService.undoRemoval(); window.toastMessage = "" } }
            Button { text: "×"; Accessible.name: "Dismiss notification"; onClicked: window.toastMessage = "" }
        }
    }
    Shortcut { sequence: "Ctrl+K"; onActivated: { commands.open(); commandSearch.forceActiveFocus() } }
    Shortcut { sequence: "Ctrl+F"; onActivated: { window.showLibrary(""); if (stack.currentItem.focusSearch) stack.currentItem.focusSearch() } }
    Shortcut { sequence: "Ctrl+N"; onActivated: { window.showNotes(); if (stack.currentItem.createNote) stack.currentItem.createNote() } }
    Shortcut { sequence: "Ctrl+O"; onActivated: importer.openChooser() }
    Shortcut { sequence: "Alt+Left"; onActivated: { if (stack.depth > 1) { stack.pop(); if (window.section === "reader") window.section = window.sectionBeforeReader } } }
    Dialog {
        id: commands; modal: true; anchors.centerIn: Overlay.overlay; width: 540; title: "Go anywhere"
        background: Rectangle { radius: 16; color: theme.panel; border.color: theme.border }
        contentItem: ColumnLayout {
            EllaTextField { id: commandSearch; tokens: theme; Layout.fillWidth: true; placeholderText: "Search commands…"; onAccepted: { window.showLibrary(text); commands.close() } }
            Repeater {
                model: [{label:"Search your library",key:"Ctrl+F",action:"library"},{label:"Add sources",key:"Ctrl+O",action:"import"},{label:"New note",key:"Ctrl+N",action:"note"},{label:"Scan watched folders",key:"",action:"scan"},{label:"Appearance and settings",key:"",action:"settings"}]
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true; visible: modelData.label.toLowerCase().indexOf(commandSearch.text.toLowerCase()) >= 0
                    text: modelData.label + (modelData.key ? "     " + modelData.key : "")
                    onClicked: {
                        commands.close()
                        if (modelData.action === "import") importer.openChooser()
                        else if (modelData.action === "note") { window.showNotes(); if (stack.currentItem.createNote) stack.currentItem.createNote() }
                        else if (modelData.action === "scan") { libraryService.scanNow(); activity.open() }
                        else if (modelData.action === "settings") window.showSettings()
                        else window.showLibrary("")
                    }
                }
            }
        }
    }
    Dialog {
        id: welcome; modal: true; anchors.centerIn: Overlay.overlay; width: 570; title: "Welcome to your quieter library"
        background: Rectangle { radius: 18; color: theme.panel; border.color: theme.border }
        contentItem: ColumnLayout {
            spacing: 20
            Label { Layout.fillWidth: true; text: "Find what you already know."; color: theme.textPrimary; font.pixelSize: 28; font.bold: true; wrapMode: Text.WordWrap }
            Label { Layout.fillWidth: true; text: "Connect folders of papers, documents and notes. ELLA makes them searchable and helps you keep the useful parts."; color: theme.textSecondary; font.pixelSize: 15; wrapMode: Text.WordWrap }
            Label { Layout.fillWidth: true; text: "1   Choose your folders\n2   Review files and exclusions\n3   Start building your library"; color: theme.textPrimary; lineHeight: 1.7 }
            Label { Layout.fillWidth: true; text: "Your original files stay where they are. Core search works offline, without an account."; color: theme.textSecondary; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                EllaButton { tokens: theme; text: "Explore first"; onClicked: { preferences.onboardingComplete = true; welcome.close() } }
                EllaButton { tokens: theme; text: "Choose folders"; tone: "primary"; onClicked: { welcome.close(); importer.openChooser() } }
            }
        }
    }
}
