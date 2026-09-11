import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property bool darkMode: false
    property int initialNoteId: -1
    property int currentNoteId: -1
    property var currentNote: ({})
    property var notes: []
    property var bibliography: []
    property bool loading: false
    property bool dirty: false
    property string saveStatus: ""
    property bool saveFailed: false
    property bool rendered: false
    signal openSource(int fileId)
    signal notify(string message)
    EllaTokens { id: theme; darkMode: root.darkMode }
    background: Rectangle { color: theme.bg }
    function refresh() { notes = noteManager.listNotes(noteSearch.text, "") }
    function selectNote(id) {
        if (!flushSave()) return
        const note = noteManager.loadNote(id)
        if (!note.id) { root.notify("Could not load this note. Your existing draft is still open."); return }
        loading = true
        currentNoteId = id; currentNote = note
        noteTitle.text = note.title || "Untitled note"
        editor.text = note.markdown || ""
        bibliography = note.bibliography || []
        dirty = false; saveFailed = false; saveStatus = "All changes saved"
        loading = false
    }
    function createNote() {
        if (!flushSave()) return
        const note = noteManager.createNote("Untitled note", "", "", "", "", "")
        if (note.id) { refresh(); selectNote(Number(note.id)); noteTitle.forceActiveFocus(); noteTitle.selectAll() }
        else root.notify("Could not create a note. Check storage permissions and free disk space.")
    }
    function markDirty() {
        if (!loading && currentNoteId >= 0) { dirty = true; saveStatus = "Unsaved changes"; saveTimer.restart() }
    }
    function flushSave() {
        saveTimer.stop()
        if (!dirty || currentNoteId < 0) return true
        const html = noteManager.markdownToHtml(editor.text)
        const ok = noteManager.saveNote(currentNoteId, noteTitle.text, currentNote.technicalDomain || "", currentNote.subject || "", currentNote.subtopic || "", currentNote.location || "", html)
        saveFailed = !ok
        if (ok) { dirty = false; saveStatus = "All changes saved"; bibliography = noteManager.bibliographyForContent(editor.text); refresh(); fileListModel.refreshCurrentView() }
        else { saveStatus = "Save failed. Your draft is still here — try again."; root.notify(saveStatus) }
        return ok
    }
    function insertReference(target) {
        const title = String(target.title || target.name || "Source")
        editor.insert(editor.cursorPosition, "@file_" + Number(target.id || target.fileId) + " — " + title.replace(/\n/g, " "))
        referencesDialog.close()
        editor.forceActiveFocus()
    }
    Component.onCompleted: { refresh(); if (initialNoteId >= 0) selectNote(initialNoteId) }
    Timer { id: saveTimer; interval: 900; onTriggered: root.flushSave() }
    Timer { id: filterTimer; interval: 200; onTriggered: root.refresh() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Your notes"; color: theme.textPrimary; font.pixelSize: 30; font.bold: true }
                Label { text: "Keep the idea. Keep the source."; color: theme.textSecondary; font.pixelSize: 13 }
            }
            EllaButton { tokens: theme; text: "+  New note"; tone: "primary"; onClicked: root.createNote() }
        }
        SplitView {
            Layout.fillWidth: true; Layout.fillHeight: true
            handle: Rectangle { implicitWidth: 10; color: "transparent" }
            Rectangle {
                SplitView.preferredWidth: 255; SplitView.minimumWidth: 210; SplitView.maximumWidth: 360
                color: theme.panel; radius: 12; border.color: theme.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    EllaTextField { id: noteSearch; tokens: theme; Layout.fillWidth: true; placeholderText: "Find a note…"; onTextChanged: filterTimer.restart() }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5; model: root.notes
                        ScrollBar.vertical: ScrollBar { }
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view.width; implicitHeight: 74
                            background: Rectangle { radius: 8; color: Number(modelData.id) === root.currentNoteId ? theme.accentSoft : (parent.hovered ? theme.panelSoft : "transparent"); border.color: parent.activeFocus ? theme.accent : "transparent" }
                            contentItem: ColumnLayout {
                                Label { text: modelData.title || modelData.name; color: theme.textPrimary; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: String(modelData.modifiedAt || "").slice(0, 10); color: theme.textMuted; font.pixelSize: 11 }
                            }
                            onClicked: root.selectNote(Number(modelData.id))
                        }
                    }
                    Label { visible: root.notes.length === 0; text: "A blank page is a good start."; color: theme.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.bottomMargin: 16 }
                }
            }
            Rectangle {
                SplitView.fillWidth: true; SplitView.minimumWidth: 370
                color: theme.panel; radius: 12; border.color: theme.border
                ColumnLayout {
                    visible: root.currentNoteId < 0; anchors.centerIn: parent; width: parent.width - 70; spacing: 16
                    Label { text: "Give your thinking a place."; color: theme.textPrimary; font.pixelSize: 27; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "Start a note, or open a source and save a passage with its context. Your notes are stored as Markdown."; color: theme.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                    EllaButton { tokens: theme; Layout.alignment: Qt.AlignHCenter; text: "Write a note"; tone: "primary"; onClicked: root.createNote() }
                }
                ColumnLayout {
                    visible: root.currentNoteId >= 0; anchors.fill: parent; anchors.margins: 24; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: root.saveStatus; color: root.saveFailed ? theme.dangerText : theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        EllaButton { tokens: theme; text: root.rendered ? "Edit Markdown" : "Preview"; implicitHeight: 32; font.pixelSize: 11; onClicked: { root.flushSave(); root.rendered = !root.rendered } }
                        EllaButton { tokens: theme; text: "Save"; implicitHeight: 32; font.pixelSize: 11; enabled: root.dirty; onClicked: root.flushSave() }
                    }
                    EllaTextField { id: noteTitle; tokens: theme; Layout.fillWidth: true; font.pixelSize: 25; font.bold: true; implicitHeight: 54; placeholderText: "Untitled note"; onTextChanged: root.markDirty() }
                    RowLayout {
                        visible: !root.rendered; Layout.fillWidth: true
                        EllaButton { tokens: theme; text: "B"; font.bold: true; implicitHeight: 30; onClicked: { const s = editor.selectionStart; const e = editor.selectionEnd; editor.insert(e, "**"); editor.insert(s, "**"); editor.forceActiveFocus() } }
                        EllaButton { tokens: theme; text: "Heading"; implicitHeight: 30; font.pixelSize: 11; onClicked: { editor.insert(editor.cursorPosition, "\n## "); editor.forceActiveFocus() } }
                        EllaButton { tokens: theme; text: "Link source"; implicitHeight: 30; font.pixelSize: 11; onClicked: referencesDialog.open() }
                        Item { Layout.fillWidth: true }
                        Label { text: "Markdown"; color: theme.textMuted; font.pixelSize: 10 }
                    }
                    ScrollView {
                        visible: !root.rendered; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        TextArea { id: editor; color: theme.textPrimary; selectionColor: theme.accentSoft; selectedTextColor: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 16; wrapMode: Text.WordWrap; selectByMouse: true; persistentSelection: true; placeholderText: "What is worth remembering?"; background: Rectangle { color: theme.panel } padding: 8; onTextChanged: root.markDirty(); Accessible.name: "Note Markdown editor" }
                    }
                    ScrollView {
                        visible: root.rendered; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        TextArea { readOnly: true; selectByMouse: true; color: theme.textPrimary; font.pixelSize: 16; wrapMode: Text.WordWrap; textFormat: TextEdit.MarkdownText; text: editor.text; background: Rectangle { color: theme.panel } Accessible.name: "Rendered note"; onLinkActivated: function(link) { if (link.indexOf("ella://file/") === 0) root.openSource(Number(link.split("/").pop())); else Qt.openUrlExternally(link) } }
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        Repeater {
                            model: root.bibliography
                            delegate: EllaButton { required property var modelData; tokens: theme; text: "↗ " + (modelData.title || modelData.name || "Source"); implicitHeight: 30; font.pixelSize: 10; onClicked: root.openSource(Number(modelData.fileId || modelData.id)) }
                        }
                    }
                }
            }
        }
    }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.flushSave() }
    Dialog {
        id: referencesDialog; title: "Link a source"; modal: true; anchors.centerIn: Overlay.overlay; width: 530; height: 420
        property var results: []
        onOpened: { referenceSearch.clear(); results = noteManager.searchReferences("", 30); referenceSearch.forceActiveFocus() }
        contentItem: ColumnLayout {
            EllaTextField { id: referenceSearch; tokens: theme; Layout.fillWidth: true; placeholderText: "Find a source to cite…"; onTextChanged: referencesDialog.results = noteManager.searchReferences(text, 30) }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: referencesDialog.results; delegate: ItemDelegate { required property var modelData; width: ListView.view.width; text: modelData.title || modelData.name; onClicked: root.insertReference(modelData) } }
        }
    }
}
