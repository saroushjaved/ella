import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property bool darkMode: false
    EllaTokens { id: theme; darkMode: root.darkMode }
    signal searchRequested(string query)
    signal openSource(int fileId)
    signal openNotes(int fileId)
    signal addSources()
    property var recent: []
    property var favorites: []
    property var notes: []
    property var queries: []
    background: Rectangle { color: theme.bg }
    function refresh() {
        recent = fileListModel.recentOpenedSources(4)
        favorites = libraryService.favorites().slice(0, 4)
        notes = noteManager.listNotes("", "").slice(0, 4)
        queries = fileListModel.recentRetrievalQueries(4)
    }
    Component.onCompleted: refresh()
    Connections { target: libraryService; function onChanged() { root.refresh() } }
    ScrollView {
        anchors.fill: parent; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 26
            Item { Layout.preferredHeight: 24 }
            ColumnLayout {
                Layout.fillWidth: true; Layout.leftMargin: 38; Layout.rightMargin: 38; spacing: 10
                Label { text: "YOUR PERSONAL LIBRARY"; color: theme.accent; font.pixelSize: 11; font.letterSpacing: 1.8; font.bold: true }
                Label { text: "Find what you already know."; color: theme.textPrimary; font.pixelSize: root.width > 920 ? 38 : 30; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { text: "Your papers, passages and ideas. Close at hand."; color: theme.textSecondary; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 15; spacing: 10
                    EllaTextField { id: homeSearch; tokens: theme; Layout.fillWidth: true; implicitHeight: 54; placeholderText: "A topic, a phrase, a half-remembered idea…"; onAccepted: root.searchRequested(text) }
                    EllaButton { tokens: theme; text: "Search library"; tone: "primary"; implicitHeight: 54; onClicked: root.searchRequested(homeSearch.text) }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    Repeater {
                        model: root.queries
                        delegate: EllaButton { required property var modelData; tokens: theme; text: modelData.queryText; implicitHeight: 30; font.pixelSize: 11; radiusValue: 15; onClicked: root.searchRequested(modelData.queryText) }
                    }
                }
            }
            Rectangle {
                visible: root.recent.length === 0 && root.favorites.length === 0
                Layout.fillWidth: true; Layout.leftMargin: 38; Layout.rightMargin: 38
                implicitHeight: intro.implicitHeight + 44; color: theme.accentSoft; border.color: theme.accentBorder; radius: 16
                RowLayout {
                    id: intro; anchors.fill: parent; anchors.margins: 22; spacing: 24
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: "Your next good idea may already be here."; font.pixelSize: 20; color: theme.textPrimary; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { text: "Add a folder to make documents searchable. Open a source, highlight something useful and turn it into a note."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    EllaButton { tokens: theme; text: "+  Add your first sources"; tone: "primary"; onClicked: root.addSources() }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.leftMargin: 38; Layout.rightMargin: 38; spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Continue reading"; font.pixelSize: 20; font.bold: true; color: theme.textPrimary }
                    Item { Layout.fillWidth: true }
                    EllaButton { tokens: theme; text: "View library →"; tone: "ghost"; implicitHeight: 32; onClicked: root.searchRequested("") }
                }
                Label { visible: root.recent.length === 0; text: "Sources you open will be waiting here next time."; color: theme.textSecondary; Layout.bottomMargin: 12 }
                Repeater {
                    model: root.recent
                    delegate: SourceCard { required property var modelData; Layout.fillWidth: true; entry: modelData; onClicked: root.openSource(Number(modelData.fileId || modelData.id)) }
                }
            }
            GridLayout {
                columns: root.width > 940 ? 2 : 1
                Layout.fillWidth: true; Layout.leftMargin: 38; Layout.rightMargin: 38; columnSpacing: 24; rowSpacing: 24
                Rectangle {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; implicitHeight: favoritesColumn.implicitHeight + 36
                    radius: 14; color: theme.panel; border.color: theme.border
                    ColumnLayout {
                        id: favoritesColumn; anchors.fill: parent; anchors.margins: 18; spacing: 10
                        Label { text: "☆  Favorites"; font.pixelSize: 18; color: theme.textPrimary; font.bold: true }
                        Label { visible: root.favorites.length === 0; text: "Keep your most useful sources one click away.\nStar a source in the library to add it here."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; Layout.topMargin: 12; Layout.bottomMargin: 16 }
                        Repeater { model: root.favorites; delegate: SourceCard { required property var modelData; Layout.fillWidth: true; entry: modelData; compact: true; onClicked: root.openSource(Number(modelData.id || modelData.fileId)) } }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; implicitHeight: notesColumn.implicitHeight + 36
                    radius: 14; color: theme.panel; border.color: theme.border
                    ColumnLayout {
                        id: notesColumn; anchors.fill: parent; anchors.margins: 18; spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "✎  Recent notes"; font.pixelSize: 18; color: theme.textPrimary; font.bold: true }
                            Item { Layout.fillWidth: true }
                            EllaButton { tokens: theme; text: "All notes"; implicitHeight: 28; font.pixelSize: 11; onClicked: root.openNotes(-1) }
                        }
                        Label { visible: root.notes.length === 0; text: "Make a little room for your own thinking.\nSave a passage or start a note from scratch."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; Layout.topMargin: 12; Layout.bottomMargin: 16 }
                        Repeater { model: root.notes; delegate: SourceCard { required property var modelData; Layout.fillWidth: true; entry: modelData; compact: true; onClicked: root.openNotes(Number(modelData.id)) } }
                    }
                }
            }
            Item { Layout.preferredHeight: 24 }
        }
    }
    component SourceCard: ItemDelegate {
        id: card
        property var entry: ({})
        property bool compact: false
        implicitHeight: compact ? 60 : 78
        Accessible.name: String(entry.title || entry.name || "Untitled")
        background: Rectangle { color: card.hovered ? theme.accentSoft : theme.panel; radius: 10; border.color: card.activeFocus ? theme.accent : (card.compact ? "transparent" : theme.border) }
        contentItem: RowLayout {
            spacing: 14
            Rectangle {
                width: card.compact ? 34 : 42; height: card.compact ? 40 : 48; radius: 7; color: theme.panelMuted
                Label { anchors.centerIn: parent; text: String(card.entry.extension || "note").replace(".", "").toUpperCase().slice(0, 4); font.pixelSize: 10; font.bold: true; color: theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Label { text: card.entry.title || card.entry.name || "Untitled"; color: theme.textPrimary; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: card.entry.path || ""; color: theme.textMuted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
            }
            Label { text: "↗"; color: theme.textMuted; font.pixelSize: 17 }
        }
    }
}
