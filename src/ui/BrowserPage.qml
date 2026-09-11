import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

Page {
    id: root
    property bool darkMode: false
    property string initialSearchText: ""
    property string viewMode: "library"
    property int selectedCollectionId: -1
    property int selectedFileId: -1
    property var selectedDetails: ({})
    property var collections: []
    property var savedSearches: []
    property bool selectedFavorite: false
    property bool filtersVisible: false
    property bool ready: false
    property string previewText: ""
    property string selectedSnippet: ""
    property string selectedReason: ""
    property var selectedAnchor: ({})
    property double semanticRequest: 0
    property string semanticError: ""
    signal requestOpenReader(int fileId, var anchor)
    signal addSources()
    signal importPathsRequested(var paths)
    signal notify(string message, bool undo)
    EllaTokens { id: theme; darkMode: root.darkMode }
    Settings { id: layoutSettings; category: "LibraryLayout"; property real previewWidth: 355; property bool cards: false }
    background: Rectangle { color: theme.bg }

    function focusSearch() { searchField.forceActiveFocus() }
    function refreshLists() { collections = fileListModel.getCollectionPickerOptions(); savedSearches = libraryService.savedSearches() }
    function applySearch() {
        if (!ready) return
        fileListModel.setLibraryFilters(viewMode === "favorites", folderFilter.text.trim(), tagFilter.text.trim())
        fileListModel.setAdvancedFilters(-1, typeFilter.currentIndex ? typeFilter.currentText : "", "", "modifiedAt", fromFilter.text.trim(), toFilter.text.trim())
        if (selectedCollectionId >= 0) fileListModel.filterByCollection(selectedCollectionId)
        else fileListModel.clearCollectionFilter()
        fileListModel.search(searchField.text)
        if (meaningSearch.checked && searchField.text.trim() !== "") {
            semanticError = ""
            semanticRequest = Number(semanticSearchService.search(searchField.text, 40))
        }
    }
    function selectFile(id, snippet, reason, anchor) {
        selectedFileId = id
        selectedDetails = fileListModel.getDetailsById(id)
        selectedFavorite = libraryService.isFavorite(id)
        tagsField.text = libraryService.tags(id).join(", ")
        selectedSnippet = snippet || ""
        selectedReason = reason || ""
        selectedAnchor = anchor || ({})
        previewText = selectedSnippet
        if (previewText === "") previewText = "Open this source to read, highlight and make a note."
    }
    function openSelection() { if (selectedFileId >= 0) root.requestOpenReader(selectedFileId, Object.keys(selectedAnchor).length ? selectedAnchor : ({quote: selectedSnippet})) }
    function favoriteSelection() {
        if (selectedFileId < 0) return
        if (libraryService.setFavorite(selectedFileId, !selectedFavorite)) {
            selectedFavorite = !selectedFavorite
            if (viewMode === "favorites") fileListModel.refreshCurrentView()
        }
    }
    Component.onCompleted: {
        refreshLists()
        searchField.text = initialSearchText
        fileListModel.clearHierarchyFilter()
        ready = true
        applySearch()
    }
    Connections {
        target: libraryService
        function onChanged() { root.refreshLists() }
        function onFilesChanged() { if (root.selectedFileId >= 0) root.selectFile(root.selectedFileId, root.selectedSnippet, root.selectedReason, root.selectedAnchor) }
    }
    Connections {
        target: semanticSearchService
        function onResultsReady(requestId, query, results, error) {
            if (Number(requestId) !== root.semanticRequest || query.trim() !== searchField.text.trim()) return
            root.semanticError = error || ""
            if (!error) fileListModel.applySemanticResults(query, results)
        }
    }
    Timer { id: searchDebounce; interval: 200; onTriggered: root.applySearch() }
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) { if (drop.hasUrls) { root.importPathsRequested(drop.urls); drop.acceptProposedAction() } }
        Rectangle { anchors.fill: parent; z: 20; visible: parent.containsDrag; color: theme.accentSoft; opacity: 0.95; border.width: 3; border.color: theme.accent; Label { anchors.centerIn: parent; text: "Drop sources to review your import"; color: theme.accent; font.pixelSize: 24 } }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 6
                Label { text: root.viewMode === "favorites" ? "Your favorites" : (root.viewMode === "collections" ? "Collections" : "Your library"); font.pixelSize: 30; font.bold: true; color: theme.textPrimary }
                Label { text: root.viewMode === "favorites" ? "The sources you return to." : "A useful passage is never far away."; color: theme.textSecondary; font.pixelSize: 13 }
            }
            EllaButton { tokens: theme; text: "+  Add sources"; tone: "primary"; onClicked: root.addSources() }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            EllaTextField {
                id: searchField; tokens: theme; Layout.fillWidth: true; implicitHeight: 48
                placeholderText: "Search words, filenames or \"an exact phrase\""
                onTextChanged: { if (root.ready) searchDebounce.restart() }
                onAccepted: { searchDebounce.stop(); root.applySearch() }
                Keys.onDownPressed: results.forceActiveFocus()
            }
            EllaButton { tokens: theme; text: root.filtersVisible ? "Hide filters" : "Filters"; implicitHeight: 48; onClicked: root.filtersVisible = !root.filtersVisible }
            EllaButton { tokens: theme; text: "+ Save"; implicitHeight: 48; enabled: searchField.text.trim() !== ""; onClicked: { savedName.text = searchField.text.trim(); saveSearchDialog.open() } }
            CheckBox { id: meaningSearch; text: semanticSearchService.available ? "Search by meaning" : "Meaning search is optional"; enabled: semanticSearchService.available; Accessible.description: "Combine keyword and local semantic passage rankings"; onToggled: root.applySearch() }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8; visible: root.savedSearches.length > 0
            Repeater {
                model: root.savedSearches
                delegate: EllaButton {
                    required property var modelData
                    tokens: theme; text: modelData.name; implicitHeight: 28; font.pixelSize: 11; radiusValue: 14
                    onClicked: { searchField.text = modelData.query || modelData.queryText || ""; root.applySearch() }
                    onPressAndHold: { libraryService.deleteSavedSearch(Number(modelData.id)); root.refreshLists() }
                    ToolTip.visible: hovered
                    ToolTip.text: "Open saved search. Press and hold to remove."
                }
            }
        }
        Rectangle {
            visible: root.filtersVisible
            Layout.fillWidth: true; implicitHeight: filterGrid.implicitHeight + 24
            color: theme.panel; border.color: theme.border; radius: 12
            GridLayout {
                id: filterGrid; anchors.fill: parent; anchors.margins: 12; columns: root.width > 1050 ? 4 : 3; rowSpacing: 8; columnSpacing: 8
                ComboBox { id: typeFilter; Layout.fillWidth: true; model: ["All file types", ".pdf", ".docx", ".pptx", ".md", ".txt", ".png", ".jpg", ".mp3", ".mp4"]; Accessible.name: "Filter by file type"; onActivated: root.applySearch() }
                EllaTextField { id: tagFilter; tokens: theme; Layout.fillWidth: true; placeholderText: "Tag"; onTextChanged: searchDebounce.restart() }
                EllaTextField { id: folderFilter; tokens: theme; Layout.fillWidth: true; placeholderText: "Folder path"; onTextChanged: searchDebounce.restart() }
                EllaTextField { id: fromFilter; tokens: theme; Layout.fillWidth: true; placeholderText: "Modified after: YYYY-MM-DD"; onEditingFinished: root.applySearch() }
                EllaTextField { id: toFilter; tokens: theme; Layout.fillWidth: true; placeholderText: "Modified before: YYYY-MM-DD"; onEditingFinished: root.applySearch() }
                EllaButton { tokens: theme; text: "Clear filters"; onClicked: { tagFilter.clear(); folderFilter.clear(); fromFilter.clear(); toFilter.clear(); typeFilter.currentIndex = 0; root.selectedCollectionId = -1; root.applySearch() } }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: fileListModel.searching ? "Searching your library…" : String(fileListModel.totalCount) + " sources"; color: theme.textSecondary; font.pixelSize: 12 }
            ComboBox {
                visible: root.viewMode === "collections"; Layout.maximumWidth: 235
                model: [{id: -1, name: "All collections"}].concat(root.collections); textRole: "name"
                Accessible.name: "Collection"
                Component.onCompleted: { for (let i = 0; i < count; ++i) if (Number(model[i].id) === root.selectedCollectionId) currentIndex = i }
                onActivated: { root.selectedCollectionId = Number(model[currentIndex].id); root.applySearch() }
            }
            EllaButton { tokens: theme; visible: root.viewMode === "collections"; text: "+ Collection"; implicitHeight: 32; onClicked: collectionDialog.open() }
            Item { Layout.fillWidth: true }
            ComboBox { model: ["Recently added", "Name A–Z", "Name Z–A", "Smallest first"]; implicitWidth: 150; Accessible.name: "Sort sources"; onActivated: fileListModel.setSort(currentIndex === 0 ? "indexedAt" : (currentIndex === 3 ? "size" : "name"), currentIndex === 1 || currentIndex === 3) }
            EllaButton { tokens: theme; text: layoutSettings.cards ? "▤ Rows" : "▦ Cards"; implicitHeight: 36; onClicked: layoutSettings.cards = !layoutSettings.cards }
        }
        SplitView {
            id: split; Layout.fillWidth: true; Layout.fillHeight: true; orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 10; color: "transparent"; Rectangle { width: 2; height: 40; anchors.centerIn: parent; radius: 1; color: SplitHandle.hovered ? theme.accent : theme.border } }
            Rectangle {
                SplitView.fillWidth: true; SplitView.minimumWidth: 310; color: theme.panel; radius: 12; border.color: theme.border
                ListView {
                    id: results; visible: !layoutSettings.cards
                    anchors.fill: parent; anchors.margins: 6; clip: true; spacing: 3; model: fileListModel
                    currentIndex: -1
                    ScrollBar.vertical: ScrollBar { }
                    Keys.onReturnPressed: root.openSelection()
                    onCurrentIndexChanged: { if (currentIndex >= 0 && activeFocus) { const item = fileListModel.get(currentIndex); root.selectFile(Number(item.id), item.searchSnippet, item.searchMatchReason, item.searchAnchor) } }
                    onAtYEndChanged: { if (atYEnd && fileListModel.hasMore && !fileListModel.searching) fileListModel.loadMore() }
                    delegate: ItemDelegate {
                        required property int index
                        required property int fileId
                        required property string name
                        required property string path
                        required property string extension
                        required property string searchSnippet
                        required property string searchMatchReason
                        required property var searchAnchor
                        required property int statusValue
                        width: ListView.view.width; implicitHeight: searchSnippet ? 112 : 82
                        Accessible.name: name + (statusValue === 1 ? ", source missing" : "")
                        background: Rectangle { color: root.selectedFileId === fileId ? theme.accentSoft : (parent.hovered ? theme.panelSoft : "transparent"); radius: 8; border.color: parent.activeFocus ? theme.accent : "transparent" }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle { width: 38; height: 46; radius: 6; color: theme.panelMuted; Label { anchors.centerIn: parent; text: extension.replace(".", "").toUpperCase().slice(0, 4); color: theme.accent; font.pixelSize: 9; font.bold: true } }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 6
                                Label { text: name; font.pixelSize: 13; font.bold: true; color: theme.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: path; font.pixelSize: 10; color: theme.textMuted; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { visible: searchSnippet !== ""; text: searchSnippet; textFormat: Text.PlainText; font.pixelSize: 12; color: theme.textSecondary; Layout.fillWidth: true; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight }
                            }
                            Label { visible: statusValue === 1; text: "Missing"; color: theme.warningText; font.pixelSize: 10 }
                        }
                        onClicked: { results.currentIndex = index; root.selectFile(fileId, searchSnippet, searchMatchReason, searchAnchor) }
                        onDoubleClicked: { root.selectFile(fileId, searchSnippet, searchMatchReason, searchAnchor); root.openSelection() }
                    }
                    footer: EllaButton { tokens: theme; width: results.width; visible: fileListModel.hasMore; text: "Load more sources"; onClicked: fileListModel.loadMore() }
                }
                GridView {
                    visible: layoutSettings.cards; anchors.fill: parent; anchors.margins: 8; clip: true
                    cellWidth: Math.max(180, Math.floor(width / Math.max(1, Math.floor(width / 205)))); cellHeight: 195
                    model: fileListModel
                    ScrollBar.vertical: ScrollBar { }
                    onAtYEndChanged: { if (atYEnd && fileListModel.hasMore && !fileListModel.searching) fileListModel.loadMore() }
                    delegate: ItemDelegate {
                        required property int fileId
                        required property string name
                        required property string extension
                        required property string mimeType
                        required property string searchSnippet
                        required property string searchMatchReason
                        required property var searchAnchor
                        width: GridView.view.cellWidth - 8; height: 186
                        background: Rectangle { radius: 10; color: root.selectedFileId === fileId ? theme.accentSoft : theme.panelSoft; border.color: parent.activeFocus ? theme.accent : theme.border }
                        contentItem: ColumnLayout {
                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; color: theme.panelMuted
                                Image { anchors.fill: parent; anchors.margins: 4; source: mimeType.startsWith("image/") ? fileListModel.fileUrlById(fileId) : ""; fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize.width: 300; sourceSize.height: 160 }
                                Label { visible: !mimeType.startsWith("image/"); anchors.centerIn: parent; text: extension.replace(".", "").toUpperCase(); color: theme.accent; font.pixelSize: 25; font.bold: true }
                            }
                            Label { Layout.fillWidth: true; text: name; color: theme.textPrimary; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight; font.pixelSize: 12 }
                        }
                        onClicked: root.selectFile(fileId, searchSnippet, searchMatchReason, searchAnchor)
                        onDoubleClicked: { root.selectFile(fileId, searchSnippet, searchMatchReason, searchAnchor); root.openSelection() }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent; width: parent.width - 56; spacing: 15
                    visible: fileListModel.totalCount === 0 && !fileListModel.searching
                    Label { Layout.alignment: Qt.AlignHCenter; text: searchField.text ? "No sources found" : (root.viewMode === "favorites" ? "A home for your favorites" : "Make room for your knowledge"); color: theme.textPrimary; font.pixelSize: 21; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                    Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: searchField.text ? "Try a shorter phrase, another word or fewer filters." : (root.viewMode === "favorites" ? "Star a source in the library to keep it here." : "Add a folder or drop files here to start."); color: theme.textSecondary }
                    EllaButton { tokens: theme; Layout.alignment: Qt.AlignHCenter; text: searchField.text ? "Clear search" : "Add sources"; onClicked: { if (searchField.text) searchField.clear(); else root.addSources() } }
                }
            }
            Rectangle {
                id: previewPane; SplitView.preferredWidth: layoutSettings.previewWidth; SplitView.minimumWidth: 265; SplitView.maximumWidth: 600
                color: theme.panel; border.color: theme.border; radius: 12
                onWidthChanged: { if (split.resizing) layoutSettings.previewWidth = width }
                ScrollView {
                    anchors.fill: parent; anchors.margins: 20; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width; spacing: 17
                        Label { text: root.selectedFileId < 0 ? "A little context" : "SOURCE PREVIEW"; color: theme.textMuted; font.pixelSize: 10; font.letterSpacing: 1.3; font.bold: true }
                        Label { visible: root.selectedFileId < 0; text: "Select a source to see the useful parts."; color: theme.textPrimary; font.pixelSize: 24; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { visible: root.selectedFileId < 0; text: "Read a matching passage, keep a favorite or open the original. Your files stay exactly where they belong."; color: theme.textSecondary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        ColumnLayout {
                            visible: root.selectedFileId >= 0; Layout.fillWidth: true; spacing: 16
                            Label { text: root.selectedDetails.name || ""; color: theme.textPrimary; font.pixelSize: 23; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { text: root.selectedDetails.path || ""; color: theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                            RowLayout {
                                Layout.fillWidth: true
                                EllaButton { tokens: theme; text: "Read source →"; tone: "primary"; Layout.fillWidth: true; onClicked: root.openSelection() }
                                EllaButton { tokens: theme; text: root.selectedFavorite ? "★" : "☆"; Accessible.name: root.selectedFavorite ? "Remove favorite" : "Add favorite"; onClicked: root.favoriteSelection() }
                            }
                            Label { visible: root.selectedReason !== ""; text: root.selectedReason; color: theme.accent; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { visible: root.semanticError !== ""; text: root.semanticError + " Keyword results remain available."; color: theme.warningText; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            TextArea {
                                text: root.previewText; readOnly: true; selectByMouse: true; wrapMode: Text.WordWrap
                                Layout.fillWidth: true; color: theme.textPrimary; font.pixelSize: 14; padding: 14
                                background: Rectangle { radius: 8; color: theme.panelSoft }
                            }
                            Label { text: "TAGS"; font.pixelSize: 10; font.letterSpacing: 1; color: theme.textMuted }
                            EllaTextField { id: tagsField; tokens: theme; Layout.fillWidth: true; placeholderText: "Add tags, separated by commas"; onEditingFinished: { if (root.selectedFileId >= 0 && !libraryService.setTags(root.selectedFileId, text.split(",").map(function(t) { return t.trim() }).filter(function(t) { return t !== "" }))) root.notify("Could not save tags.", false) } }
                            ComboBox { id: assignCollection; Layout.fillWidth: true; model: root.collections; textRole: "name"; Accessible.name: "Assign collection" }
                            EllaButton { tokens: theme; text: "Add to collection"; Layout.fillWidth: true; enabled: root.collections.length > 0; onClicked: { const ok = fileListModel.assignCollectionById(root.selectedFileId, Number(root.collections[assignCollection.currentIndex].id)); root.notify(ok ? "Added to collection." : "Could not update collection.", false) } }
                            Rectangle { height: 1; Layout.fillWidth: true; color: theme.border }
                            EllaButton { tokens: theme; text: "Open original"; Layout.fillWidth: true; onClicked: { if (!fileListModel.openFileById(root.selectedFileId)) root.notify("Source unavailable. Relink it to its current location.", false) } }
                            EllaButton { tokens: theme; text: "Show in folder"; Layout.fillWidth: true; onClicked: fileListModel.openContainingFolderById(root.selectedFileId) }
                            EllaButton { tokens: theme; text: "Relink source…"; Layout.fillWidth: true; onClicked: relinkDialog.open() }
                            EllaButton { tokens: theme; text: "Remove from library"; tone: "ghost"; Layout.fillWidth: true; onClicked: { if (libraryService.removeFile(root.selectedFileId)) { root.selectedFileId = -1; root.notify("Removed from ELLA. Your original file is untouched.", true) } else root.notify("Could not remove this source.", false) } }
                        }
                    }
                }
            }
        }
    }
    FileDialog { id: relinkDialog; title: "Locate this source"; onAccepted: { const ok = fileListModel.relinkFileById(root.selectedFileId, selectedFile.toString()); root.notify(ok ? "Source relinked." : "Could not relink this source.", false); root.selectFile(root.selectedFileId, "", "", ({})) } }
    Dialog {
        id: saveSearchDialog; title: "Save this search"; modal: true; anchors.centerIn: Overlay.overlay; width: 400; standardButtons: Dialog.Save | Dialog.Cancel
        contentItem: EllaTextField { id: savedName; tokens: theme; placeholderText: "Search name" }
        onAccepted: { libraryService.saveSearch(savedName.text.trim(), searchField.text); root.refreshLists() }
    }
    Dialog {
        id: collectionDialog; title: "New collection"; modal: true; anchors.centerIn: Overlay.overlay; width: 400; standardButtons: Dialog.Save | Dialog.Cancel
        contentItem: EllaTextField { id: collectionName; tokens: theme; placeholderText: "Collection name" }
        onAccepted: { if (collectionName.text.trim() && fileListModel.addCollection(collectionName.text.trim(), -1)) { collectionName.clear(); root.refreshLists() } }
    }
}
