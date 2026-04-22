import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    required property var navigationStack
    signal openKnowledgeLibrary(string initialSearchText, string launchAction, int focusFileId)

    EllaTokens { id: tokens }
    font.family: tokens.fontFamily

    property int historyLimit: 4
    property bool sidebarCollapsed: false
    property bool advancedExpanded: true
    property var recentSearches: []
    property var recentlyOpened: []
    property var indexStatusMap: ({})
    property var searchHealthMap: ({})
    property var importStatusMap: ({})
    property var syncStatusMap: ({})
    property var providerStatusList: []

    readonly property int indexedDocsCount: Math.max(Number(searchHealthMap.ftsDocuments || 0), Number(indexStatusMap.indexedCount || 0))
    readonly property int queueCount: Number(indexStatusMap.queued || 0)
    readonly property int indexErrorCount: Number(searchHealthMap.indexErrors || 0)
    readonly property int importedCount: Number(importStatusMap.importedCount || 0)
    readonly property int skippedCount: Number(importStatusMap.skippedCount || 0)
    readonly property int failedImportCount: Number(importStatusMap.failedCount || 0)
    readonly property int connectedProviders: Number(syncStatusMap.connectedProviders || 0)
    readonly property int syncPendingCount: Number(syncStatusMap.pendingJobs || 0)
    readonly property int syncFailedCount: Number(syncStatusMap.terminalFailedJobs || 0)
    readonly property bool searchPipelineReady: !!(searchHealthMap.ocrAvailable
                                                   && searchHealthMap.tessdataReady
                                                   && searchHealthMap.mediaTranscriptionReady
                                                   && searchHealthMap.pptConversionReady)
    readonly property real queueProgressValue: {
        const total = Number(indexStatusMap.total || 0)
        if (total <= 0) {
            return indexStatusMap.running ? 0.12 : 0.0
        }

        return Math.max(0, Math.min(1, Number(indexStatusMap.processed || 0) / total))
    }

    function openBrowser(queryText, launchAction, focusFileId) {
        root.openKnowledgeLibrary(
            String(queryText || "").trim(),
            String(launchAction || "").trim(),
            focusFileId !== undefined ? Number(focusFileId) : -1
        )
    }

    function runRecover() {
        openBrowser(searchField.text, "", -1)
    }

    function queueSubtitleText() {
        const total = Number(indexStatusMap.total || 0)
        if (total > 0) {
            return Number(indexStatusMap.processed || 0) + " / " + total + " processed"
        }

        return queueCount + (queueCount === 1 ? " pending item" : " pending items")
    }

    function indexStateText() {
        if (indexErrorCount > 0) {
            return "Needs attention"
        }
        if (indexStatusMap.running) {
            return "Optimizing"
        }
        if (queueCount > 0) {
            return "Queued"
        }
        return "Idle"
    }

    function indexStateDotColor() {
        if (indexErrorCount > 0) {
            return "#ef4444"
        }
        if (indexStatusMap.running || queueCount > 0) {
            return "#22c55e"
        }
        return "#94a3b8"
    }

    function importSubtitleText() {
        if (failedImportCount > 0) {
            return failedImportCount + (failedImportCount === 1 ? " failed" : " failed")
        }
        if (skippedCount > 0) {
            return skippedCount + (skippedCount === 1 ? " skipped" : " skipped")
        }
        if (importedCount > 0) {
            return "Last import"
        }
        return "No imports yet"
    }

    function toLocalTime(isoValue) {
        const iso = String(isoValue || "").trim()
        if (iso === "") {
            return "n/a"
        }

        const dt = new Date(iso)
        if (isNaN(dt.getTime())) {
            return iso
        }

        return dt.toLocaleString()
    }

    function providerDisplayName(providerId) {
        const provider = String(providerId || "").trim().toLowerCase()
        if (provider === "google_drive") {
            return "Google Drive"
        }
        if (provider === "onedrive") {
            return "OneDrive"
        }
        return provider
    }

    function statusAlertText() {
        const syncError = String(syncStatusMap.lastError || "").trim()
        if (syncError !== "") {
            return "Sync warning: " + syncError
        }

        if (indexErrorCount > 0) {
            const lastIndexError = String(searchHealthMap.lastIndexError || "").trim()
            if (lastIndexError !== "") {
                return "Index warning: " + lastIndexError
            }
            return "Index warning: " + indexErrorCount + (indexErrorCount === 1 ? " error" : " errors")
        }

        if (failedImportCount > 0) {
            return "Import warning: " + failedImportCount + (failedImportCount === 1 ? " failed file" : " failed files")
        }

        if (syncFailedCount > 0) {
            return "Sync warning: " + syncFailedCount + (syncFailedCount === 1 ? " failed job" : " failed jobs")
        }

        return ""
    }

    function normalizeRecentSearches(items) {
        const normalized = []
        const list = items || []

        for (let i = 0; i < list.length && normalized.length < historyLimit; ++i) {
            const entry = list[i]
            const text = String(entry && entry.queryText !== undefined ? entry.queryText : "").trim()
            if (text !== "") {
                normalized.push(entry)
            }
        }

        if (normalized.length === 0) {
            normalized.push({
                "queryText": "No recent searches yet",
                "disabled": true
            })
        }

        return normalized
    }

    function normalizeRecentlyOpened(items) {
        const normalized = []
        const list = items || []

        for (let i = 0; i < list.length && normalized.length < historyLimit; ++i) {
            const entry = list[i]
            const text = String(entry && entry.name !== undefined ? entry.name : "").trim()
            if (text !== "") {
                normalized.push(entry)
            }
        }

        if (normalized.length === 0) {
            normalized.push({
                "name": "No recently opened files",
                "disabled": true
            })
        }

        return normalized
    }

    function refreshHistory() {
        recentSearches = normalizeRecentSearches(fileListModel.recentRetrievalQueries(historyLimit))
        recentlyOpened = normalizeRecentlyOpened(fileListModel.recentOpenedSources(historyLimit))
    }

    function refreshStatus() {
        indexStatusMap = fileListModel.indexStatus()
        searchHealthMap = fileListModel.searchHealth()
        importStatusMap = fileListModel.importStatus()

        if (cloudSyncModel) {
            syncStatusMap = cloudSyncModel.status()
            providerStatusList = cloudSyncModel.providerStatuses()
        } else {
            syncStatusMap = ({})
            providerStatusList = []
        }
    }

    function refreshDashboard() {
        refreshHistory()
        refreshStatus()
    }

    Component.onCompleted: refreshDashboard()
    onVisibleChanged: {
        if (visible) {
            refreshDashboard()
        }
    }

    Connections {
        target: fileListModel
        function onIndexStatusChanged() {
            refreshStatus()
        }
    }

    Connections {
        target: cloudSyncModel
        function onStatusChanged() {
            refreshStatus()
        }
    }

    Timer {
        interval: 2500
        running: true
        repeat: true
        onTriggered: refreshStatus()
    }

    component GlyphIcon: Label {
        property string glyph: ""
        property int glyphSize: 16

        text: glyph
        color: tokens.sidebarText
        font.family: "Segoe MDL2 Assets"
        font.pixelSize: glyphSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        renderType: Text.NativeRendering
    }

    component SidebarEntry: Item {
        id: navItem
        property string text: ""
        property string iconGlyph: ""
        property bool active: false
        property bool child: false
        property bool compact: false
        property bool hasChevron: false
        property bool expanded: false
        signal clicked()

        implicitHeight: 40
        implicitWidth: parent ? parent.width : 220

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: navItem.active ? "#1f3047" : "transparent"
            border.color: navItem.active ? tokens.sidebarActiveBorder : "transparent"
            border.width: navItem.active ? 1 : 0
        }

        Rectangle {
            visible: navItem.active
            width: 3
            height: parent.height - 14
            radius: 2
            color: tokens.sidebarActiveBorder
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
        }

        RowLayout {
            visible: !navItem.compact
            anchors.fill: parent
            anchors.leftMargin: navItem.child ? 22 : 12
            anchors.rightMargin: 10
            spacing: 10

            GlyphIcon {
                visible: navItem.iconGlyph !== ""
                glyph: navItem.iconGlyph
                glyphSize: 15
                color: navItem.active ? tokens.sidebarActiveText : tokens.sidebarText
                Layout.preferredWidth: 16
            }

            Label {
                Layout.fillWidth: true
                text: navItem.text
                color: navItem.active ? tokens.sidebarActiveText : tokens.sidebarText
                font.family: tokens.fontFamily
                font.pixelSize: 13
                font.bold: navItem.active
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            GlyphIcon {
                visible: navItem.hasChevron
                glyph: navItem.expanded ? "\uE70D" : "\uE76C"
                glyphSize: 12
                color: tokens.sidebarMutedText
                Layout.preferredWidth: 12
            }
        }

        GlyphIcon {
            visible: navItem.compact
            anchors.centerIn: parent
            glyph: navItem.iconGlyph
            glyphSize: 15
            color: navItem.active ? tokens.sidebarActiveText : tokens.sidebarText
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: navItem.clicked()
        }
    }

    component SnapshotCard: EllaPanel {
        id: card
        tokens: tokens
        clip: true

        property string iconGlyph: ""
        property string titleText: ""
        property string valueText: ""
        property string subtitleText: ""
        property bool showProgressBar: false
        property real progressValue: 0.0
        property bool showStatus: false
        property string statusText: ""
        property color statusDotColor: "#22c55e"

        radiusValue: 10
        fillColor: tokens.panel
        borderLine: tokens.border
        Layout.fillWidth: true
        Layout.preferredHeight: 148

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 2

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 44
                height: 44
                radius: 22
                color: "#dceef0"
                border.color: "#c7dde2"
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: card.iconGlyph
                    color: tokens.accent
                    font.family: "Segoe MDL2 Assets"
                    font.pixelSize: 20
                    renderType: Text.NativeRendering
                }
            }

            Label {
                Layout.fillWidth: true
                text: card.titleText
                color: tokens.textPrimary
                font.family: tokens.fontFamily
                font.pixelSize: 11
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Label {
                visible: !card.showProgressBar && !card.showStatus
                Layout.fillWidth: true
                text: card.valueText
                color: tokens.textPrimary
                font.family: tokens.fontFamily
                font.pixelSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            ColumnLayout {
                visible: card.showProgressBar
                Layout.fillWidth: true
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 7
                    radius: 3
                    color: "#d8dee8"

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, card.progressValue))
                        height: parent.height
                        radius: parent.radius
                        color: tokens.accent
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: card.subtitleText
                    color: tokens.textPrimary
                    font.family: tokens.fontFamily
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            ColumnLayout {
                visible: card.showStatus
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: card.statusDotColor
                    }

                    Label {
                        text: card.statusText
                        color: tokens.textSecondary
                        font.family: tokens.fontFamily
                        font.pixelSize: 11
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: card.subtitleText
                    color: tokens.textPrimary
                    font.family: tokens.fontFamily
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Label {
                visible: !card.showProgressBar && !card.showStatus
                Layout.fillWidth: true
                text: card.subtitleText
                color: tokens.textSecondary
                font.family: tokens.fontFamily
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    component ListSection: Item {
        id: listSection
        property string heading: ""
        property var rows: []
        property string textRole: "name"
        property string actionLabel: "Open"
        signal rowAction(var rowData)

        function rowText(rowData) {
            if (rowData === undefined || rowData === null) {
                return ""
            }

            if (typeof rowData === "string") {
                return rowData
            }

            const roleValue = rowData[textRole]
            if (roleValue !== undefined && roleValue !== null) {
                const text = String(roleValue).trim()
                if (text !== "") {
                    return text
                }
            }

            return ""
        }

        function rowDisabled(rowData) {
            return !!(rowData && rowData.disabled === true)
        }

        implicitHeight: 208
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: listSection.heading
                color: tokens.textPrimary
                font.family: tokens.fontFamily
                font.pixelSize: 22
                font.bold: true
            }

            EllaPanel {
                tokens: tokens
                Layout.fillWidth: true
                Layout.fillHeight: true
                radiusValue: 8
                fillColor: tokens.panel
                borderLine: tokens.border

                Column {
                    anchors.fill: parent

                    Repeater {
                        model: listSection.rows

                        delegate: Item {
                            width: parent.width
                            height: 38

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: listSection.rowText(modelData)
                                    color: tokens.textPrimary
                                    font.family: tokens.fontFamily
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }

                                EllaButton {
                                    visible: !listSection.rowDisabled(modelData)
                                    tokens: tokens
                                    tone: "ghost"
                                    text: listSection.actionLabel
                                    implicitHeight: 24
                                    leftPadding: 12
                                    rightPadding: 12
                                    radiusValue: 7
                                    fillColor: "#eef2f6"
                                    borderLine: "#cdd6e1"
                                    labelColor: tokens.textPrimary
                                    font.pixelSize: 11
                                    onClicked: listSection.rowAction(modelData)
                                }
                            }

                            Rectangle {
                                visible: index < listSection.rows.length - 1
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: tokens.border
                            }
                        }
                    }
                }
            }
        }
    }

    background: Rectangle {
        color: tokens.bg
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.sidebarCollapsed ? 64 : 232
            Layout.fillHeight: true
            border.color: "#1f2b3d"
            border.width: 1

            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0f172a" }
                GradientStop { position: 0.55; color: "#10223f" }
                GradientStop { position: 1.0; color: "#0e1628" }
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 14
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 14
                    spacing: 7

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.bottomMargin: 10
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            radius: 7
                            color: collapseSidebarArea.containsMouse ? "#1d2d46" : "transparent"
                            border.color: collapseSidebarArea.containsMouse ? "#2f445f" : "transparent"
                            border.width: 1

                            GlyphIcon {
                                anchors.centerIn: parent
                                glyph: "\uE700"
                                glyphSize: 13
                                color: tokens.sidebarMutedText
                            }

                            MouseArea {
                                id: collapseSidebarArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.sidebarCollapsed = !root.sidebarCollapsed
                            }
                        }

                        Label {
                            visible: !root.sidebarCollapsed
                            text: "ELLA"
                            color: tokens.sidebarText
                            font.family: tokens.fontFamily
                            font.pixelSize: 23
                            font.bold: true
                            Layout.fillWidth: true
                        }
                    }

                    SidebarEntry {
                        text: "Home"
                        iconGlyph: "\uE80F"
                        compact: root.sidebarCollapsed
                        active: true
                    }

                    SidebarEntry {
                        text: "Memory Browser"
                        iconGlyph: "\uEA37"
                        compact: root.sidebarCollapsed
                        onClicked: openBrowser("", "", -1)
                    }

                    SidebarEntry {
                        text: "Import"
                        iconGlyph: "\uE898"
                        compact: root.sidebarCollapsed
                        onClicked: openBrowser("", "import_files", -1)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 6
                        height: 1
                        color: "#304156"
                        opacity: 0.7
                    }

                    SidebarEntry {
                        text: "Advanced"
                        iconGlyph: "\uE713"
                        compact: root.sidebarCollapsed
                        hasChevron: true
                        expanded: root.advancedExpanded
                        onClicked: root.advancedExpanded = !root.advancedExpanded
                    }

                    SidebarEntry {
                        visible: root.advancedExpanded && !root.sidebarCollapsed
                        text: "Manage"
                        iconGlyph: "\uE76C"
                        child: true
                        compact: root.sidebarCollapsed
                        onClicked: openBrowser("", "", -1)
                    }

                    SidebarEntry {
                        visible: root.advancedExpanded && !root.sidebarCollapsed
                        text: "Rebuild Index"
                        iconGlyph: "\uE9E9"
                        child: true
                        compact: root.sidebarCollapsed
                        onClicked: {
                            fileListModel.rebuildSearchIndex()
                            refreshStatus()
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                anchors.topMargin: 16
                anchors.bottomMargin: 12
                spacing: 18

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 252

                    ColumnLayout {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: Math.min(parent.width - 20, 820)
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: "Find what you already know."
                            color: tokens.textPrimary
                            font.family: tokens.fontFamily
                            font.pixelSize: 54
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Recovering exact files, snippets, and source locations from\nall your indexed documents and research."
                            color: "#1f2937"
                            font.family: tokens.fontFamily
                            font.pixelSize: 18
                            horizontalAlignment: Text.AlignHCenter
                        }

                        EllaTextField {
                            id: searchField
                            tokens: tokens
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 710
                            implicitHeight: 42
                            font.pixelSize: 18
                            placeholderText: "What are you trying to recover?"
                            onAccepted: runRecover()

                            background: Rectangle {
                                radius: 8
                                color: tokens.panel
                                border.color: searchField.activeFocus ? tokens.accentHover : tokens.accent
                                border.width: 1
                            }
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8

                            EllaButton {
                                tokens: tokens
                                text: "Recover"
                                tone: "primary"
                                implicitHeight: 34
                                Layout.preferredWidth: 100
                                radiusValue: 8
                                font.pixelSize: 13
                                font.bold: true
                                onClicked: runRecover()
                            }

                            EllaButton {
                                tokens: tokens
                                text: "Open Memory Browser"
                                tone: "secondary"
                                implicitHeight: 34
                                Layout.preferredWidth: 166
                                radiusValue: 8
                                fillColor: "#deefea"
                                borderLine: "#7ab6ad"
                                font.pixelSize: 13
                                onClicked: openBrowser(searchField.text, "", -1)
                            }

                            EllaButton {
                                tokens: tokens
                                text: "Import Files"
                                tone: "secondary"
                                implicitHeight: 34
                                Layout.preferredWidth: 112
                                radiusValue: 8
                                fillColor: "#f5f7fb"
                                borderLine: "#cfd6e0"
                                font.pixelSize: 13
                                onClicked: openBrowser(searchField.text, "import_files", -1)
                            }

                            EllaButton {
                                tokens: tokens
                                text: "Import Folder"
                                tone: "secondary"
                                implicitHeight: 34
                                Layout.preferredWidth: 112
                                radiusValue: 8
                                fillColor: "#f5f7fb"
                                borderLine: "#cfd6e0"
                                font.pixelSize: 13
                                onClicked: openBrowser(searchField.text, "import_folder", -1)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    spacing: 18

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        Label {
                            text: "Session Snapshot Tiles"
                            color: tokens.textPrimary
                            font.family: tokens.fontFamily
                            font.pixelSize: 22
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8

                            SnapshotCard {
                                iconGlyph: "\uE8A5"
                                titleText: "Indexed docs"
                                valueText: String(root.indexedDocsCount)
                                subtitleText: "Documents"
                            }

                            SnapshotCard {
                                iconGlyph: "\uE8FD"
                                titleText: "Queue"
                                showProgressBar: true
                                progressValue: root.queueProgressValue
                                subtitleText: root.queueSubtitleText()
                            }

                            SnapshotCard {
                                iconGlyph: "\uE895"
                                titleText: "Index state"
                                showStatus: true
                                statusText: root.indexStateText()
                                subtitleText: root.indexErrorCount > 0
                                              ? (root.indexErrorCount + (root.indexErrorCount === 1 ? " error" : " errors"))
                                              : "Status"
                                statusDotColor: root.indexStateDotColor()
                            }

                            SnapshotCard {
                                iconGlyph: "\uE898"
                                titleText: "New import"
                                valueText: String(root.importedCount)
                                subtitleText: root.importSubtitleText()
                            }
                        }
                    }

                    EllaPanel {
                        tokens: tokens
                        Layout.preferredWidth: 340
                        Layout.fillHeight: true
                        radiusValue: 10
                        fillColor: tokens.panel
                        borderLine: tokens.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            Label {
                                Layout.fillWidth: true
                                text: "Trust & Local Status Card"
                                color: tokens.textPrimary
                                font.family: tokens.fontFamily
                                font.pixelSize: 17
                                font.bold: true
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Live system health from indexing, search, import, and cloud sync (Experimental)."
                                color: "#374151"
                                font.family: tokens.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Indexed " + root.indexedDocsCount
                                      + " • Queue " + root.queueCount
                                      + " • Errors " + root.indexErrorCount
                                      + "\nLast index: " + root.toLocalTime(indexStatusMap.lastIndexedAt)
                                color: "#374151"
                                font.family: tokens.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                EllaChip {
                                    tokens: tokens
                                    labelText: root.connectedProviders > 0
                                               ? ("Cloud linked (Experimental) (" + root.connectedProviders + ")")
                                               : "Local-only mode"
                                    chipColor: root.connectedProviders > 0 ? "#e8f3fb" : "#dff4e5"
                                    chipBorder: root.connectedProviders > 0 ? "#84c5ee" : "#8fd4a3"
                                    chipText: root.connectedProviders > 0 ? "#0f5f99" : "#166534"
                                    bulletColor: root.connectedProviders > 0 ? "#0f5f99" : "#16a34a"
                                }

                                EllaChip {
                                    tokens: tokens
                                    labelText: root.searchPipelineReady
                                               ? ("Search ready (" + root.indexedDocsCount + " docs)")
                                               : "Search setup needed"
                                    chipColor: root.searchPipelineReady ? "#dff4e5" : "#fef1f1"
                                    chipBorder: root.searchPipelineReady ? "#8fd4a3" : "#ef7d7d"
                                    chipText: root.searchPipelineReady ? "#166534" : "#b42318"
                                    bulletColor: root.searchPipelineReady ? "#16a34a" : "#ef4444"
                                }

                                EllaChip {
                                    tokens: tokens
                                    labelText: "Index " + root.indexStateText()
                                    chipColor: root.indexErrorCount > 0 ? "#fef1f1" : "#dff4e5"
                                    chipBorder: root.indexErrorCount > 0 ? "#ef7d7d" : "#8fd4a3"
                                    chipText: root.indexErrorCount > 0 ? "#b42318" : "#166534"
                                    bulletColor: root.indexErrorCount > 0 ? "#ef4444" : "#16a34a"
                                }

                                EllaChip {
                                    tokens: tokens
                                    labelText: "Import " + root.importedCount + " ok / " + root.failedImportCount + " fail"
                                    chipColor: root.failedImportCount > 0 ? "#fef1f1" : "#dff4e5"
                                    chipBorder: root.failedImportCount > 0 ? "#ef7d7d" : "#8fd4a3"
                                    chipText: root.failedImportCount > 0 ? "#b42318" : "#166534"
                                    bulletColor: root.failedImportCount > 0 ? "#ef4444" : "#16a34a"
                                }

                                EllaChip {
                                    tokens: tokens
                                    labelText: "Sync pending " + root.syncPendingCount + " / failed " + root.syncFailedCount
                                    chipColor: root.syncFailedCount > 0 ? "#fef1f1"
                                                                        : (root.syncPendingCount > 0 ? "#fdf1c8" : "#dff4e5")
                                    chipBorder: root.syncFailedCount > 0 ? "#ef7d7d"
                                                                         : (root.syncPendingCount > 0 ? "#f1c66f" : "#8fd4a3")
                                    chipText: root.syncFailedCount > 0 ? "#b42318"
                                                                       : (root.syncPendingCount > 0 ? "#7a5200" : "#166534")
                                    bulletColor: root.syncFailedCount > 0 ? "#ef4444"
                                                                          : (root.syncPendingCount > 0 ? "#eab308" : "#16a34a")
                                }

                                Repeater {
                                    model: providerStatusList

                                    EllaChip {
                                        required property var modelData
                                        tokens: tokens
                                        labelText: root.providerDisplayName(modelData.provider)
                                                   + " (" + Number(modelData.pendingJobs || 0) + " pending)"
                                        chipColor: Number(modelData.pendingJobs || 0) > 0 ? "#fdf1c8" : "#e8f3fb"
                                        chipBorder: Number(modelData.pendingJobs || 0) > 0 ? "#f1c66f" : "#84c5ee"
                                        chipText: Number(modelData.pendingJobs || 0) > 0 ? "#7a5200" : "#0f5f99"
                                        bulletColor: Number(modelData.pendingJobs || 0) > 0 ? "#eab308" : "#0f5f99"
                                    }
                                }
                            }

                            Label {
                                visible: root.statusAlertText() !== ""
                                Layout.fillWidth: true
                                text: root.statusAlertText()
                                color: "#b42318"
                                font.family: tokens.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 232
                    spacing: 18

                    ListSection {
                        heading: "Recent Searches"
                        rows: root.recentSearches
                        textRole: "queryText"
                        actionLabel: "Recover"
                        onRowAction: function(rowData) {
                            const queryText = String(rowData && rowData.queryText !== undefined ? rowData.queryText : "").trim()
                            if (queryText !== "") {
                                openBrowser(queryText, "", -1)
                            }
                        }
                    }

                    ListSection {
                        heading: "Recently Opened"
                        rows: root.recentlyOpened
                        textRole: "name"
                        actionLabel: "Open"
                        onRowAction: function(rowData) {
                            const fileId = Number(rowData && rowData.fileId !== undefined ? rowData.fileId : -1)
                            if (fileId >= 0) {
                                openBrowser("", "", fileId)
                                return
                            }

                            const fileName = String(rowData && rowData.name !== undefined ? rowData.name : "").trim()
                            if (fileName !== "") {
                                openBrowser(fileName, "", -1)
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
