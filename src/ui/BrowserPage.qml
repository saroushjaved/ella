import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Page {
    id: root
    EllaTokens { id: theme }

    required property var navigationStack
    property string initialSearchText: ""
    property string launchAction: ""
    property int initialFocusFileId: -1
    property bool launchIntentHandled: false
    font.family: theme.fontFamily

    signal requestOpenReader(int fileIndex, var fileDetails)

    background: Rectangle {
        color: bgColor
    }

    property string pendingRelinkPath: ""
    property string sidebarMode: "collections"   // collections | hierarchy
    property var collectionTreeModel: []
    property var collectionPickerModel: []
    property var hierarchyTreeModel: []
    property var selectedCollectionRules: []
    property int selectedCollectionIdForAssign: -1
    property int selectedSidebarCollectionId: -1
    property string selectedSidebarCollectionName: ""
    property string selectedHierarchyLabel: "All Files"
    property int detailsRefreshCounter: 0
    property string integrityMessage: ""
    property string actionMessage: ""
    property string toolingWarningMessage: ""
    property bool filtersExpanded: false
    property bool fileInfoExpanded: false
    property bool metadataExpanded: false
    property bool collectionsExpanded: true
    property bool hierarchyExpanded: true
    property bool manageExpanded: true
    property bool compactListMode: false
    property bool hierarchyManageExpanded: false
    property int selectedHierarchyManageCollectionId: -1
    property string selectedHierarchyManageCollectionName: ""
    property var selectedStatusFilters: []
    property var selectedExtensionFilters: []
    property var selectedDocumentTypeFilters: []
    property var statusFilterOptions: [
        { label: "Indexed", value: "indexed" },
        { label: "Failed", value: "failed" },
        { label: "In Progress", value: "in_progress" }
    ]
    property var extensionFilterOptions: [".pdf", ".docx", ".txt", ".pptx", ".xlsx", ".md", ".m4a", ".mp4", ".ellanote"]
    property var documentTypeFilterOptions: [
        "Research Report",
        "Meeting Notes",
        "Technical Spec",
        "Presentation",
        "Audio",
        "Video",
        "Ella Note"
    ]
    property string personName: "Your Name"
    property var indexStatusMap: ({})
    property var searchHealthMap: ({})
    property var importStatusMap: ({})
    property var syncStatusMap: ({})
    property var providerStatusList: []
    property string cloudRedirectUri: "http://localhost:53682/callback"
    property string pendingOauthProvider: ""
    property string pendingOauthDisplayName: ""
    property string pendingOauthCodeVerifier: ""
    property string pendingOauthState: ""
    property string pendingOauthAuthUrl: ""
    property bool oauthConnectInProgress: false

    readonly property color bgColor: theme.bg
    readonly property color panelColor: theme.panel
    readonly property color panelSoft: theme.panelSoft
    readonly property color panelMuted: theme.panelMuted
    readonly property color borderColor: theme.border
    readonly property color textPrimary: theme.textPrimary
    readonly property color textSecondary: theme.textSecondary
    readonly property color accentColor: theme.accent
    readonly property color accentHover: theme.accentHover
    readonly property color accentSoft: theme.accentSoft
    readonly property color accentBorder: theme.accentBorder
    readonly property color sidebarBg: theme.sidebarA
    readonly property color sidebarCard: theme.sidebarB
    readonly property color sidebarBorder: theme.sidebarC
    readonly property color sidebarText: theme.sidebarText
    readonly property color sidebarMutedText: theme.sidebarMutedText
    readonly property color sidebarActiveBg: theme.sidebarActiveBg
    readonly property color sidebarActiveBorder: theme.sidebarActiveBorder
    readonly property color sidebarActiveText: theme.sidebarActiveText
    readonly property color successBg: theme.successBg
    readonly property color successBorder: theme.successBorder
    readonly property color successText: theme.successText
    readonly property color infoBg: theme.infoBg
    readonly property color infoBorder: theme.infoBorder
    readonly property color infoText: theme.infoText
    readonly property color dangerBg: theme.dangerBg
    readonly property color dangerBorder: theme.dangerBorder
    readonly property color dangerText: theme.dangerText
    readonly property string currentContextLabel: selectedSidebarCollectionName !== ""
                                                ? selectedSidebarCollectionName
                                                : (selectedHierarchyLabel !== "All Files"
                                                   ? selectedHierarchyLabel
                                                   : "All Files")
    readonly property bool searchReady: !!(searchHealthMap.ocrAvailable && searchHealthMap.tessdataReady)
    readonly property int indexProgressPercent: {
        const total = Number(indexStatusMap.total || 0)
        if (total <= 0) {
            return indexStatusMap.running ? 1 : 0
        }
        return Math.max(0, Math.min(100, Math.round((Number(indexStatusMap.processed || 0) * 100) / total)))
    }

    function refreshCollections() {
        collectionTreeModel = fileListModel.getCollectionTree()
        collectionPickerModel = fileListModel.getCollectionPickerOptions()

        if (collectionPickerModel.length > 0) {
            let found = false
            for (let i = 0; i < collectionPickerModel.length; ++i) {
                if (collectionPickerModel[i].id === selectedCollectionIdForAssign) {
                    found = true
                    break
                }
            }
            if (!found) {
                selectedCollectionIdForAssign = collectionPickerModel[0].id
            }
        } else {
            selectedCollectionIdForAssign = -1
        }
    }

    function refreshHierarchy() {
        hierarchyTreeModel = fileListModel.getHierarchyTree()
    }

    function refreshRules() {
        if (selectedSidebarCollectionId >= 0) {
            selectedCollectionRules = fileListModel.getCollectionRules(selectedSidebarCollectionId)
        } else {
            selectedCollectionRules = []
        }
    }

    function refreshEverything() {
        fileListModel.refreshCurrentView()
        refreshCollections()
        refreshHierarchy()
        refreshRules()
        refreshServiceStatus()
        detailsRefreshCounter += 1
    }

    function refreshServiceStatus() {
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
        updateToolingWarnings()
    }

    function updateToolingWarnings() {
        let warnings = []
        if (!searchHealthMap.ocrAvailable) {
            warnings.push("OCR unavailable")
        } else if (!searchHealthMap.tessdataReady) {
            warnings.push("OCR language data incomplete")
        }

        if (!searchHealthMap.mediaTranscriptionReady) {
            warnings.push("Media transcription tools unavailable")
        }

        if (!searchHealthMap.pptConversionReady) {
            warnings.push("Presentation conversion tools unavailable")
        }

        toolingWarningMessage = warnings.join(" | ")
    }

    function formatSize(bytes) {
        if (bytes < 1024)
            return bytes + " B"
        else if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        else if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        else
            return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    function localFileUrl(path) {
        const normalized = String(path || "").replace(/\\/g, "/")
        if (normalized === "")
            return ""
        return "file:///" + normalized
    }

    function statusText(statusValue) {
        return statusValue === 1 ? "Missing" : "Active"
    }

    function statusBadgeLabel(statusValue, matchReason, indexedAtValue) {
        const hasIndexedAt = String(indexedAtValue || "").trim() !== ""
        if (statusValue === 1) {
            return "Error"
        }
        if (String(matchReason || "").trim() !== "") {
            return "Success"
        }
        if (statusValue === 0 && hasIndexedAt) {
            return "Success"
        }
        if (statusValue === 0 && !hasIndexedAt) {
            return "Info"
        }
        return "Neutral"
    }

    function statusBadgeBg(statusValue, matchReason, indexedAtValue) {
        const label = statusBadgeLabel(statusValue, matchReason, indexedAtValue)
        if (label === "Success") {
            return successBg
        }
        if (label === "Error") {
            return dangerBg
        }
        if (label === "Info") {
            return infoBg
        }
        return "#f4efe0"
    }

    function statusBadgeBorder(statusValue, matchReason, indexedAtValue) {
        const label = statusBadgeLabel(statusValue, matchReason, indexedAtValue)
        if (label === "Success") {
            return successBorder
        }
        if (label === "Error") {
            return dangerBorder
        }
        if (label === "Info") {
            return infoBorder
        }
        return "#d6c6a5"
    }

    function statusBadgeTextColor(statusValue, matchReason, indexedAtValue) {
        const label = statusBadgeLabel(statusValue, matchReason, indexedAtValue)
        if (label === "Success") {
            return successText
        }
        if (label === "Error") {
            return dangerText
        }
        if (label === "Info") {
            return infoText
        }
        return "#5b4b2f"
    }

    function formatIndexedDate(value) {
        const raw = String(value || "").trim()
        if (raw === "") {
            return "-"
        }

        if (raw.length > 16) {
            return raw.substring(0, 16).replace("T", " ")
        }
        return raw.replace("T", " ")
    }

    function fileTypeLabel(extensionValue) {
        const ext = String(extensionValue || "").trim().replace(".", "").toUpperCase()
        return ext !== "" ? ext : "FILE"
    }

    function fileIconGlyph(extensionValue) {
        const ext = fileTypeLabel(extensionValue)
        if (ext === "PDF") return "\uE130"
        if (ext === "DOC" || ext === "DOCX") return "\uE8A5"
        if (ext === "PPT" || ext === "PPTX") return "\uE7C3"
        if (ext === "TXT" || ext === "MD") return "\uE8A5"
        if (ext === "XLS" || ext === "XLSX") return "\uE9D5"
        if (ext === "MP4") return "\uE768"
        if (ext === "M4A" || ext === "MP3" || ext === "WAV") return "\uEC4F"
        return "\uE8A5"
    }

    function toggleChip(listRef, value) {
        const normalized = String(value || "").trim()
        if (normalized === "") {
            return
        }
        const next = listRef.slice(0)
        const idx = next.indexOf(normalized)
        if (idx >= 0) {
            next.splice(idx, 1)
        } else {
            next.push(normalized)
        }
        return next
    }

    function toggleStatusFilter(value) {
        selectedStatusFilters = toggleChip(selectedStatusFilters, value)
        applyAdvancedFilters()
    }

    function toggleExtensionFilter(value) {
        selectedExtensionFilters = toggleChip(selectedExtensionFilters, value)
        applyAdvancedFilters()
    }

    function toggleDocumentTypeFilter(value) {
        selectedDocumentTypeFilters = toggleChip(selectedDocumentTypeFilters, value)
        applyAdvancedFilters()
    }

    function clearChipFilters() {
        selectedStatusFilters = []
        selectedExtensionFilters = []
        selectedDocumentTypeFilters = []
    }

    function activeFilterSummary() {
        const parts = []
        if (selectedStatusFilters.length > 0) {
            parts.push("Status: " + selectedStatusFilters.join(", "))
        }
        if (selectedExtensionFilters.length > 0) {
            parts.push("Ext: " + selectedExtensionFilters.join(", "))
        }
        if (selectedDocumentTypeFilters.length > 0) {
            parts.push("Type: " + selectedDocumentTypeFilters.join(", "))
        }
        if (dateFieldCombo.currentIndex > 0) {
            parts.push("Date: " + dateFieldCombo.currentText)
        }
        return parts.length > 0 ? parts.join(" | ") : "No advanced filters applied"
    }

    function providerStatus(providerId) {
        for (let i = 0; i < providerStatusList.length; ++i) {
            const item = providerStatusList[i]
            if (item && item.provider === providerId) {
                return item
            }
        }
        return null
    }

    function providerIsConnected(providerId) {
        return providerStatus(providerId) !== null
    }

    function providerAccountEmail(providerId) {
        const item = providerStatus(providerId)
        if (!item || !item.accountEmail) {
            return ""
        }
        return String(item.accountEmail)
    }

    function beginCloudProviderConnect(providerId, displayName) {
        if (!cloudSyncModel) {
            actionMessage = "Cloud sync (Experimental) service is unavailable"
            return
        }

        if (!oauthCallbackServer) {
            actionMessage = "OAuth callback server is unavailable"
            return
        }

        const redirectUri = cloudRedirectUri.trim()
        if (redirectUri === "") {
            actionMessage = "OAuth redirect URI is empty"
            return
        }

        if (!oauthCallbackServer.startListening(redirectUri)) {
            actionMessage = displayName + " (Experimental) connect failed: " + String(oauthCallbackServer.lastError || "Unable to start localhost callback")
            return
        }

        const oauth = cloudSyncModel.beginOAuthConnect(providerId, redirectUri)
        if (!oauth || !oauth.ok) {
            actionMessage = (displayName + " (Experimental) connect failed: " + String(oauth && oauth.error ? oauth.error : "Unknown error"))
            oauthCallbackServer.stopListening()
            refreshServiceStatus()
            return
        }

        pendingOauthProvider = providerId
        pendingOauthDisplayName = displayName
        pendingOauthCodeVerifier = String(oauth.codeVerifier || "")
        pendingOauthState = String(oauth.state || "")
        pendingOauthAuthUrl = String(oauth.authUrl || "")
        oauthConnectInProgress = true

        oauthDialog.open()
        Qt.openUrlExternally(pendingOauthAuthUrl)
        actionMessage = displayName + " authorization opened in browser, waiting for callback..."
    }

    function resetPendingOAuthState() {
        pendingOauthProvider = ""
        pendingOauthDisplayName = ""
        pendingOauthCodeVerifier = ""
        pendingOauthState = ""
        pendingOauthAuthUrl = ""
        oauthConnectInProgress = false
    }

    function handleOAuthCallback(code, state, error, errorDescription) {
        if (!oauthConnectInProgress || pendingOauthProvider === "") {
            return
        }

        if (error && String(error).trim() !== "") {
            actionMessage = pendingOauthDisplayName + " authorization failed: " + String(errorDescription || error)
            if (oauthCallbackServer) {
                oauthCallbackServer.stopListening()
            }
            oauthDialog.close()
            resetPendingOAuthState()
            refreshServiceStatus()
            return
        }

        const expectedState = String(pendingOauthState || "")
        const receivedState = String(state || "")
        if (expectedState !== "" && receivedState !== expectedState) {
            actionMessage = pendingOauthDisplayName + " (Experimental) connect failed: state mismatch"
            if (oauthCallbackServer) {
                oauthCallbackServer.stopListening()
            }
            oauthDialog.close()
            resetPendingOAuthState()
            refreshServiceStatus()
            return
        }

        const authCode = String(code || "").trim()
        if (authCode === "") {
            actionMessage = pendingOauthDisplayName + " (Experimental) connect failed: missing authorization code"
            if (oauthCallbackServer) {
                oauthCallbackServer.stopListening()
            }
            oauthDialog.close()
            resetPendingOAuthState()
            refreshServiceStatus()
            return
        }

        const ok = cloudSyncModel.completeOAuthConnect(
            pendingOauthProvider,
            authCode,
            cloudRedirectUri.trim(),
            pendingOauthCodeVerifier
        )

        actionMessage = ok
                        ? (pendingOauthDisplayName + " (Experimental) connected")
                        : ("Failed to connect " + pendingOauthDisplayName + " (Experimental)")

        if (oauthCallbackServer) {
            oauthCallbackServer.stopListening()
        }
        oauthDialog.close()
        resetPendingOAuthState()
        refreshServiceStatus()
    }

    function disconnectCloudProvider(providerId, displayName) {
        if (!cloudSyncModel) {
            return
        }
        const ok = cloudSyncModel.disconnectProvider(providerId)
        if (ok) {
            actionMessage = displayName + " (Experimental) disconnected"
            refreshServiceStatus()
        } else {
            actionMessage = "Failed to disconnect " + displayName + " (Experimental)"
            refreshServiceStatus()
        }
    }

    function applyAdvancedFilters() {
        const dateFieldMap = ["", "indexedAt", "modifiedAt", "createdAt"]

        fileListModel.setAdvancedFiltersV2(
            selectedStatusFilters,
            selectedExtensionFilters,
            selectedDocumentTypeFilters,
            dateFieldMap[dateFieldCombo.currentIndex],
            dateFromField.text.trim(),
            dateToField.text.trim()
        )
    }

    function applySorting() {
        const sortFieldMap = ["indexedAt", "name", "modifiedAt", "createdAt", "sizeBytes", "path"]
        fileListModel.setSort(
            sortFieldMap[sortFieldCombo.currentIndex],
            sortOrderCombo.currentIndex === 0
        )
    }

    function clearRetrievalControls() {
        searchField.text = ""
        clearChipFilters()
        dateFieldCombo.currentIndex = 0
        dateFromField.text = ""
        dateToField.text = ""
        sortFieldCombo.currentIndex = 0
        sortOrderCombo.currentIndex = 1

        fileListModel.search("")
        fileListModel.clearAdvancedFilters()
        fileListModel.setSort("indexedAt", false)
    }

    function toLocalPath(urlOrPath) {
        const raw = String(urlOrPath || "")
        if (raw.startsWith("file:///")) {
            return decodeURIComponent(raw.substring(8))
        }
        return decodeURIComponent(raw)
    }

    function openReaderForIndex(index) {
        if (index < 0)
            return

        const details = fileListModel.getDetails(index)
        if (!details || details.statusValue === 1)
            return

        fileListModel.trackRetrievalEvent(
            "result_opened",
            searchField.text.trim(),
            Number(details.id || -1),
            -1,
            "",
            "{\"surface\":\"browser_reader\"}"
        )
        requestOpenReader(index, details)
    }

    function applyLaunchIntent() {
        if (launchIntentHandled)
            return
        launchIntentHandled = true

        const query = String(initialSearchText || "").trim()
        if (query !== "") {
            searchField.text = query
        }

        if (initialFocusFileId >= 0) {
            const idx = fileListModel.indexOfFileId(initialFocusFileId)
            if (idx >= 0) {
                listView.currentIndex = idx
                listView.positionViewAtIndex(idx, ListView.Center)
            }
        }

        if (launchAction === "import_files") {
            Qt.callLater(function() { importFilesDialog.open() })
        } else if (launchAction === "import_folder") {
            Qt.callLater(function() { importFolderDialog.open() })
        }
    }

    Component.onCompleted: {
        refreshCollections()
        refreshHierarchy()
        refreshRules()
        applySorting()
        refreshServiceStatus()
        Qt.callLater(applyLaunchIntent)
    }

    Connections {
        target: fileListModel
        function onIndexStatusChanged() {
            refreshServiceStatus()
        }
    }

    Connections {
        target: cloudSyncModel
        function onStatusChanged() {
            refreshServiceStatus()
        }
    }

    Connections {
        target: oauthCallbackServer
        function onCallbackReceived(code, state, error, errorDescription) {
            handleOAuthCallback(code, state, error, errorDescription)
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: refreshServiceStatus()
    }

    component SectionTitle: Label {
        color: textPrimary
        font.pixelSize: 14
        font.bold: true
    }

    component MetaLabel: Label {
        color: textSecondary
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    component ValueText: Text {
        Layout.fillWidth: true
        color: textPrimary
        wrapMode: Text.WrapAnywhere
    }

    component TinyStatusIcon: Rectangle {
        id: iconRoot
        property string glyph: "?"
        property color glyphColor: textSecondary
        property color fill: panelMuted
        property color lineColor: sidebarBorder

        width: 16
        height: 16
        radius: 8
        color: iconRoot.fill
        border.color: iconRoot.lineColor
        border.width: 1

        Label {
            anchors.centerIn: parent
            text: iconRoot.glyph
            color: iconRoot.glyphColor
            font.pixelSize: 10
            font.bold: true
        }
    }

    component AppButton: EllaButton {
        tokens: theme
        implicitHeight: 38
        font.pixelSize: 13
    }

    component AppTextField: EllaTextField {
        tokens: theme
        implicitHeight: 40
        font.pixelSize: 13
    }

    component AppComboBox: ComboBox {
        id: combo
        implicitHeight: 40
        font.pixelSize: 13

        background: Rectangle {
            radius: 10
            color: panelColor
            border.color: combo.activeFocus ? accentBorder : borderColor
            border.width: 1
        }

        contentItem: Text {
            text: combo.displayText
            color: textPrimary
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            rightPadding: 30
            elide: Text.ElideRight
            font: combo.font
        }

        indicator: Canvas {
            x: combo.width - width - 10
            y: (combo.height - height) / 2
            width: 12
            height: 8
            contextType: "2d"

            onPaint: {
                context.clearRect(0, 0, width, height)
                context.beginPath()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fillStyle = textSecondary
                context.fill()
            }
        }

    }

    FileDialog {
        id: importFilesDialog
        title: "Import files from your archive"
        fileMode: FileDialog.OpenFiles

        onAccepted: {
            const localPaths = []
            if (selectedFiles && selectedFiles.length > 0) {
                for (let i = 0; i < selectedFiles.length; ++i) {
                    localPaths.push(toLocalPath(selectedFiles[i]))
                }
            } else if (selectedFile) {
                localPaths.push(toLocalPath(selectedFile))
            }

            const result = fileListModel.importFiles(localPaths)
            importStatusMap = result
            actionMessage = String(result.lastMessage || "")
            refreshEverything()
        }
    }

    FolderDialog {
        id: importFolderDialog
        title: "Import folder from your archive"

        onAccepted: {
            const folderPath = toLocalPath(selectedFolder)
            const result = fileListModel.importFolder(folderPath)
            importStatusMap = result
            actionMessage = String(result.lastMessage || "")
            refreshEverything()
        }
    }

    FileDialog {
        id: relinkFileDialog
        title: "Select replacement file"

        onAccepted: {
            if (selectedFile) {
                let localPath = selectedFile.toString()

                if (localPath.startsWith("file:///"))
                    localPath = localPath.substring(8)

                localPath = decodeURIComponent(localPath)
                pendingRelinkPath = localPath
                relinkPathField.text = localPath
            }
        }
    }

    Dialog {
        id: oauthDialog
        title: pendingOauthDisplayName === "" ? "Connect Google Account (Experimental)" : ("Connect " + pendingOauthDisplayName + " Account (Experimental)")
        modal: true
        width: 560
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: "ELLA is waiting for your authorization from Google (Experimental sync)."
                color: textPrimary
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                text: "This usually takes just a few seconds."
                color: textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            BusyIndicator {
                running: oauthConnectInProgress
                visible: oauthConnectInProgress
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                visible: pendingOauthState !== ""
                text: "State: " + pendingOauthState
                color: sidebarMutedText
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }

            Label {
                text: "Redirect URI: " + cloudRedirectUri
                color: sidebarMutedText
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }

            RowLayout {
                Layout.fillWidth: true

                AppButton {
                    text: "Open Browser Again"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: {
                        if (pendingOauthAuthUrl !== "") {
                            Qt.openUrlExternally(pendingOauthAuthUrl)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: {
                        if (oauthCallbackServer) {
                            oauthCallbackServer.stopListening()
                        }
                        oauthDialog.close()
                        actionMessage = pendingOauthDisplayName + " (Experimental) connect canceled"
                        resetPendingOAuthState()
                        refreshServiceStatus()
                    }
                }
            }
        }
    }

    Dialog {
        id: supportSopDialog
        modal: true
        title: "Report an Issue"
        width: 680
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Please include all fields below when reporting a beta issue:"
                color: textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "1. App version/build (see footer)\n2. Reproduction steps\n3. Expected result vs actual result\n4. Whether it crashes\n5. Diagnostics bundle path (Tools -> Export Diagnostics)\n6. Screenshot or video if available"
                color: textSecondary
                font.pixelSize: 13
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Cloud sync issues should mention that sync is Experimental."
                color: warningText
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: "Close"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: supportSopDialog.close()
                }
            }
        }
    }

    Dialog {
        id: editMetadataDialog
        title: "Edit File Metadata"
        modal: true
        width: 560
        height: 720
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ScrollView {
            clip: true

            ColumnLayout {
                width: editMetadataDialog.width - 48
                spacing: 10

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    Label { text: "Technical Domain *"; color: textPrimary; font.bold: true }
                    Label { text: "Subject *"; color: textPrimary; font.bold: true }
                    AppTextField { id: editTechnicalDomainField; Layout.fillWidth: true; placeholderText: "Technical Domain" }
                    AppTextField { id: editSubjectField; Layout.fillWidth: true; placeholderText: "Subject" }

                    Label { text: "Subtopic *"; color: textPrimary; font.bold: true }
                    Label { text: "Document Type"; color: textPrimary; font.bold: true }
                    AppTextField { id: editSubtopicField; Layout.fillWidth: true; placeholderText: "Subtopic" }
                    AppTextField { id: editDocumentTypeField; Layout.fillWidth: true; placeholderText: "Document Type" }

                    Label { text: "Location *"; color: textPrimary; font.bold: true }
                    Label { text: "Source *"; color: textPrimary; font.bold: true }
                    AppTextField { id: editLocationField; Layout.fillWidth: true; placeholderText: "Location" }
                    AppTextField { id: editSourceField; Layout.fillWidth: true; placeholderText: "Source" }

                    Label { text: "Author *"; color: textPrimary; font.bold: true }
                    Item {}
                    AppTextField { id: editAuthorField; Layout.fillWidth: true; placeholderText: "Author" }
                    Item {}
                }

                Label { text: "Remarks"; color: textPrimary; font.bold: true }
                TextArea {
                    id: editRemarksField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    wrapMode: TextEdit.Wrap
                    color: textPrimary
                    background: Rectangle {
                        radius: 10
                        color: panelColor
                        border.color: borderColor
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: "Secondary"
                        fillColor: panelMuted
                        borderLine: borderColor
                        onClicked: editMetadataDialog.close()
                    }

                    AppButton {
                        text: "Save Changes"
                        enabled:
                            listView.currentIndex >= 0 &&
                            editTechnicalDomainField.text.trim() !== "" &&
                            editSubjectField.text.trim() !== "" &&
                            editSubtopicField.text.trim() !== "" &&
                            editLocationField.text.trim() !== "" &&
                            editSourceField.text.trim() !== "" &&
                            editAuthorField.text.trim() !== ""
                        fillColor: accentColor
                        borderLine: accentColor
                        labelColor: "#ffffff"
                        font.bold: true
                        onClicked: {
                            const ok = fileListModel.updateFileMetadata(
                                listView.currentIndex,
                                editTechnicalDomainField.text.trim(),
                                editSubjectField.text.trim(),
                                editSubtopicField.text.trim(),
                                editLocationField.text.trim(),
                                editSourceField.text.trim(),
                                editAuthorField.text.trim(),
                                editDocumentTypeField.text.trim(),
                                editRemarksField.text.trim()
                            )

                            if (ok) {
                                editMetadataDialog.close()
                                refreshEverything()
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: addCollectionDialog
        title: addCollectionParentId >= 0 ? "Add Subcollection" : "Add Collection"
        modal: true
        width: 420
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        property int addCollectionParentId: -1

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: addCollectionDialog.addCollectionParentId >= 0
                      ? "Subcollection Name"
                      : "Collection Name"
                font.bold: true
                color: textPrimary
            }

            AppTextField {
                id: collectionNameField
                Layout.fillWidth: true
                placeholderText: "Enter name"
                onAccepted: createCollectionButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: {
                        collectionNameField.text = ""
                        addCollectionDialog.close()
                    }
                }

                AppButton {
                    id: createCollectionButton
                    text: "Add Collection"
                    enabled: collectionNameField.text.trim() !== ""
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    font.bold: true

                    onClicked: {
                        const ok = fileListModel.addCollection(
                            collectionNameField.text.trim(),
                            addCollectionDialog.addCollectionParentId
                        )
                        if (ok) {
                            collectionNameField.text = ""
                            addCollectionDialog.close()
                            refreshCollections()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: renameCollectionDialog
        title: "Rename Collection"
        modal: true
        width: 420
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: "New Collection Name"
                font.bold: true
                color: textPrimary
            }

            AppTextField {
                id: renameCollectionField
                Layout.fillWidth: true
                placeholderText: "Enter new name"
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: renameCollectionDialog.close()
                }

                AppButton {
                    text: "Rename"
                    enabled: selectedSidebarCollectionId >= 0 && renameCollectionField.text.trim() !== ""
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    font.bold: true
                    onClicked: {
                        const ok = fileListModel.renameCollection(
                            selectedSidebarCollectionId,
                            renameCollectionField.text.trim()
                        )
                        if (ok) {
                            selectedSidebarCollectionName = renameCollectionField.text.trim()
                            renameCollectionDialog.close()
                            refreshCollections()
                            refreshRules()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: relinkDialog
        title: "Relink Missing File"
        modal: true
        width: 560
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: "New File Path"
                font.bold: true
                color: textPrimary
            }

            AppTextField {
                id: relinkPathField
                Layout.fillWidth: true
                placeholderText: "Choose replacement file"
            }

            AppButton {
                text: "Browse..."
                fillColor: panelMuted
                borderLine: borderColor
                onClicked: relinkFileDialog.open()
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: relinkDialog.close()
                }

                AppButton {
                    text: "Relink"
                    enabled: listView.currentIndex >= 0 && relinkPathField.text.trim() !== ""
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    font.bold: true
                    onClicked: {
                        const ok = fileListModel.relinkFile(
                            listView.currentIndex,
                            relinkPathField.text.trim()
                        )
                        if (ok) {
                            pendingRelinkPath = ""
                            relinkPathField.text = ""
                            relinkDialog.close()
                            refreshEverything()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: addRuleDialog
        title: "Add Smart Rule"
        modal: true
        width: 460
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        background: Rectangle {
            color: panelColor
            radius: 16
            border.color: borderColor
        }

        property var fieldOptions: [
            { label: "Technical Domain", value: "technical_domain" },
            { label: "Subject", value: "subject" },
            { label: "SubTopic", value: "subtopic" },
            { label: "Location", value: "location" },
            { label: "Source", value: "source" },
            { label: "Author", value: "author" },
            { label: "Document Type", value: "document_type" },
            { label: "Remarks", value: "remarks" }
        ]

        property var operatorOptions: [
            { label: "exact", value: "exact" },
            { label: "contains", value: "contains" }
        ]

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: selectedSidebarCollectionName !== ""
                      ? "Collection: " + selectedSidebarCollectionName
                      : "No collection selected"
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: textPrimary
            }

            Label { text: "Field"; font.bold: true; color: textPrimary }
            AppComboBox {
                id: ruleFieldCombo
                Layout.fillWidth: true
                model: addRuleDialog.fieldOptions
                textRole: "label"
            }

            Label { text: "Operator"; font.bold: true; color: textPrimary }
            AppComboBox {
                id: ruleOperatorCombo
                Layout.fillWidth: true
                model: addRuleDialog.operatorOptions
                textRole: "label"
            }

            Label { text: "Value"; font.bold: true; color: textPrimary }
            AppTextField {
                id: ruleValueField
                Layout.fillWidth: true
                placeholderText: "Enter match value"
                onAccepted: createRuleButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    fillColor: panelMuted
                    borderLine: borderColor
                    onClicked: {
                        ruleValueField.text = ""
                        addRuleDialog.close()
                    }
                }

                AppButton {
                    id: createRuleButton
                    text: "Add Rule"
                    enabled: selectedSidebarCollectionId >= 0 && ruleValueField.text.trim() !== ""
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    font.bold: true

                    onClicked: {
                        const fieldValue = addRuleDialog.fieldOptions[ruleFieldCombo.currentIndex].value
                        const operatorValue = addRuleDialog.operatorOptions[ruleOperatorCombo.currentIndex].value
                        const ok = fileListModel.addCollectionRule(
                            selectedSidebarCollectionId,
                            fieldValue,
                            operatorValue,
                            ruleValueField.text.trim()
                        )

                        if (ok) {
                            ruleValueField.text = ""
                            addRuleDialog.close()
                            refreshRules()
                            fileListModel.refreshCurrentView()
                        }
                    }
                }
            }
        }
    }

    header: Item {
        implicitHeight: 0
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        anchors.margins: 0

        Rectangle {
            Layout.preferredWidth: 278
            Layout.fillHeight: true
            radius: 0
            border.color: sidebarBorder
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0f172a" }
                GradientStop { position: 0.55; color: "#10223f" }
                GradientStop { position: 1.0; color: "#0e1628" }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 10
                    color: backNavArea.containsMouse ? "#1c2b43" : "transparent"
                    border.color: backNavArea.containsMouse ? sidebarBorder : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            text: "\uE72B"
                            color: sidebarText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 14
                        }

                        Label {
                            text: "Back"
                            color: sidebarText
                            font.pixelSize: 14
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }
                    }

                    MouseArea {
                        id: backNavArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (navigationStack)
                                navigationStack.pop()
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Welcome, " + personName
                    color: sidebarText
                    font.pixelSize: 17
                    font.bold: true
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#28405f"
                    opacity: 0.85
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 9
                    color: (selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files") ? "#1f3047" : "transparent"
                    border.color: (selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files") ? sidebarActiveBorder : "transparent"
                    border.width: (selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files") ? 1 : 0

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            text: "\uE8B7"
                            color: (selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files") ? sidebarActiveText : sidebarText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 14
                        }

                        Label {
                            text: "All Files"
                            color: (selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files") ? sidebarActiveText : sidebarText
                            font.pixelSize: 14
                            font.bold: selectedSidebarCollectionId < 0 && selectedHierarchyLabel === "All Files"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            sidebarMode = "collections"
                            selectedSidebarCollectionId = -1
                            selectedSidebarCollectionName = ""
                            selectedHierarchyLabel = "All Files"
                            fileListModel.clearCollectionFilter()
                            fileListModel.clearHierarchyFilter()
                            refreshRules()
                            detailsRefreshCounter += 1
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 9
                    color: sidebarMode === "collections" ? "#16263c" : "transparent"
                    border.color: sidebarMode === "collections" ? sidebarBorder : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            text: "\uE8B7"
                            color: sidebarText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 14
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Collections"
                            color: sidebarText
                            font.pixelSize: 14
                            font.bold: sidebarMode === "collections"
                        }

                        Label {
                            text: collectionsExpanded ? "\uE70D" : "\uE76C"
                            color: sidebarMutedText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            sidebarMode = "collections"
                            fileListModel.clearHierarchyFilter()
                            collectionsExpanded = !collectionsExpanded
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: collectionsExpanded ? Math.min(190, collectionsList.contentHeight + 4) : 0
                    visible: collectionsExpanded
                    clip: true

                    ListView {
                        id: collectionsList
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        model: collectionTreeModel
                        spacing: 6
                        clip: true

                        delegate: Rectangle {
                            width: parent.width
                            height: 34
                            radius: 8
                            color: selectedSidebarCollectionId === modelData.id ? sidebarActiveBg : "transparent"
                            border.color: selectedSidebarCollectionId === modelData.id ? sidebarActiveBorder : "transparent"
                            border.width: selectedSidebarCollectionId === modelData.id ? 1 : 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6

                                Label {
                                    text: "\uE8B7"
                                    color: selectedSidebarCollectionId === modelData.id ? sidebarActiveText : sidebarMutedText
                                    font.family: "Segoe MDL2 Assets"
                                    font.pixelSize: 12
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.displayName
                                    color: selectedSidebarCollectionId === modelData.id ? sidebarActiveText : sidebarText
                                    font.pixelSize: 13
                                    font.bold: selectedSidebarCollectionId === modelData.id
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    selectedSidebarCollectionId = modelData.id
                                    selectedSidebarCollectionName = modelData.name
                                    selectedHierarchyLabel = "All Files"
                                    fileListModel.filterByCollection(modelData.id)
                                    refreshRules()
                                    detailsRefreshCounter += 1
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    radius: 9
                    color: sidebarMode === "hierarchy" ? "#16263c" : "transparent"
                    border.color: sidebarMode === "hierarchy" ? sidebarBorder : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            text: "\uEC7A"
                            color: sidebarText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 14
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Hierarchy"
                            color: sidebarText
                            font.pixelSize: 14
                            font.bold: sidebarMode === "hierarchy"
                        }

                        Label {
                            text: hierarchyExpanded ? "\uE70D" : "\uE76C"
                            color: sidebarMutedText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            sidebarMode = "hierarchy"
                            fileListModel.clearCollectionFilter()
                            selectedSidebarCollectionId = -1
                            selectedSidebarCollectionName = ""
                            refreshRules()
                            hierarchyExpanded = !hierarchyExpanded
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hierarchyExpanded ? Math.min(170, hierarchyList.contentHeight + 4) : 0
                    visible: hierarchyExpanded
                    clip: true

                    ListView {
                        id: hierarchyList
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        model: hierarchyTreeModel
                        spacing: 6
                        clip: true

                        delegate: Rectangle {
                            width: parent.width
                            height: 34
                            radius: 8
                            color: selectedHierarchyLabel === modelData.label ? sidebarActiveBg : "transparent"
                            border.color: selectedHierarchyLabel === modelData.label ? sidebarActiveBorder : "transparent"
                            border.width: selectedHierarchyLabel === modelData.label ? 1 : 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6

                                Label {
                                    text: "\uE8FD"
                                    color: selectedHierarchyLabel === modelData.label ? sidebarActiveText : sidebarMutedText
                                    font.family: "Segoe MDL2 Assets"
                                    font.pixelSize: 12
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.displayName
                                    color: selectedHierarchyLabel === modelData.label ? sidebarActiveText : sidebarText
                                    font.pixelSize: 13
                                    font.bold: selectedHierarchyLabel === modelData.label
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    selectedSidebarCollectionId = -1
                                    selectedSidebarCollectionName = ""
                                    selectedHierarchyLabel = modelData.label
                                    fileListModel.filterByHierarchy(
                                        modelData.technicalDomain,
                                        modelData.subject,
                                        modelData.subtopic
                                    )
                                    refreshRules()
                                    detailsRefreshCounter += 1
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#0f1e36"
                    border.color: "#2b3f5d"
                    implicitHeight: manageColumn.implicitHeight + 16

                    ColumnLayout {
                        id: manageColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "Manage"
                                color: sidebarText
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: manageExpanded ? "\uE70D" : "\uE76C"
                                color: sidebarMutedText
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: 12
                            }
                        }

                        ColumnLayout {
                            visible: manageExpanded
                            Layout.fillWidth: true
                            spacing: 6

                            AppButton {
                                text: "Collections"
                                Layout.fillWidth: true
                                fillColor: sidebarCard
                                borderLine: sidebarBorder
                                labelColor: sidebarText
                                onClicked: {
                                    addCollectionDialog.addCollectionParentId = selectedSidebarCollectionId >= 0
                                                                                ? selectedSidebarCollectionId : -1
                                    addCollectionDialog.open()
                                }
                            }

                            AppButton {
                                text: "Smart Rule"
                                Layout.fillWidth: true
                                enabled: selectedSidebarCollectionId >= 0
                                fillColor: sidebarCard
                                borderLine: sidebarBorder
                                labelColor: sidebarText
                                onClicked: addRuleDialog.open()
                            }

                            AppButton {
                                text: "Rebuild Index"
                                Layout.fillWidth: true
                                fillColor: sidebarCard
                                borderLine: sidebarBorder
                                labelColor: sidebarText
                                onClicked: {
                                    fileListModel.rebuildSearchIndex()
                                    actionMessage = "Search index rebuild queued"
                                    refreshServiceStatus()
                                }
                            }
                        }

                        MouseArea {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 28
                            cursorShape: Qt.PointingHandCursor
                            onClicked: manageExpanded = !manageExpanded
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 0
            color: bgColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        EllaChip {
                            tokens: theme
                            labelText: "Local-First"
                            chipColor: accentSoft
                            chipBorder: accentBorder
                            chipText: accentColor
                            bulletColor: accentColor
                        }

                        EllaChip {
                            tokens: theme
                            labelText: "Indexing: " + indexProgressPercent + "%"
                            chipColor: panelMuted
                            chipBorder: borderColor
                            chipText: textSecondary
                            bulletColor: indexStatusMap.running ? accentColor : textSecondary
                        }

                        EllaChip {
                            tokens: theme
                            labelText: "Search: " + (searchReady ? "Ready" : "Needs setup")
                            chipColor: searchReady ? successBg : dangerBg
                            chipBorder: searchReady ? successBorder : dangerBorder
                            chipText: searchReady ? successText : dangerText
                            bulletColor: searchReady ? successText : dangerText
                        }

                        EllaChip {
                            tokens: theme
                            labelText: "Imports: " + Number(importStatusMap.importedCount || 0)
                            chipColor: panelMuted
                            chipBorder: borderColor
                            chipText: textSecondary
                            bulletColor: accentColor
                        }

                        EllaChip {
                            tokens: theme
                            labelText: Number(syncStatusMap.connectedProviders || 0) > 0
                                       ? "Google Sync (Experimental): Active"
                                       : "Google Sync (Experimental): Disconnected"
                            chipColor: Number(syncStatusMap.connectedProviders || 0) > 0 ? successBg : dangerBg
                            chipBorder: Number(syncStatusMap.connectedProviders || 0) > 0 ? successBorder : dangerBorder
                            chipText: Number(syncStatusMap.connectedProviders || 0) > 0 ? successText : dangerText
                            bulletColor: Number(syncStatusMap.connectedProviders || 0) > 0 ? successText : dangerText
                        }
                    }

                    Rectangle {
                        visible: integrityMessage !== ""
                        radius: 10
                        color: panelSoft
                        border.color: borderColor
                        implicitHeight: integrityLabel.implicitHeight + 10
                        implicitWidth: integrityLabel.implicitWidth + 16

                        Label {
                            id: integrityLabel
                            anchors.centerIn: parent
                            text: integrityMessage
                            color: textSecondary
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        visible: actionMessage !== ""
                        radius: 10
                        color: accentSoft
                        border.color: accentBorder
                        implicitHeight: actionLabel.implicitHeight + 10
                        implicitWidth: actionLabel.implicitWidth + 16

                        Label {
                            id: actionLabel
                            anchors.centerIn: parent
                            text: actionMessage
                            color: accentColor
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    Rectangle {
                        visible: toolingWarningMessage !== ""
                        radius: 10
                        color: warningBg
                        border.color: warningBorder
                        implicitHeight: toolingWarningLabel.implicitHeight + 10
                        implicitWidth: Math.min(420, toolingWarningLabel.implicitWidth + 16)

                        Label {
                            id: toolingWarningLabel
                            anchors.centerIn: parent
                            text: "Tooling warning: " + toolingWarningMessage
                            color: warningText
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                            width: Math.min(404, implicitWidth)
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    AppTextField {
                        id: searchField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 560
                        placeholderText: "What are you trying to recover?"
                        onTextChanged: fileListModel.search(text)
                    }

                    AppButton {
                        text: "Clear"
                        fillColor: panelMuted
                        borderLine: "transparent"
                        onClicked: clearRetrievalControls()
                    }

                    AppButton {
                        text: "Import Files"
                        fillColor: panelColor
                        borderLine: borderColor
                        onClicked: importFilesDialog.open()
                    }

                    AppButton {
                        text: "Import Folder"
                        fillColor: panelColor
                        borderLine: borderColor
                        onClicked: importFolderDialog.open()
                    }

                    AppButton {
                        text: filtersExpanded ? "Hide Filters" : "Show Filters"
                        fillColor: panelMuted
                        borderLine: borderColor
                        onClicked: filtersExpanded = !filtersExpanded
                    }

                    AppButton {
                        id: toolsButton
                        text: "Tools"
                        fillColor: panelMuted
                        borderLine: borderColor
                        onClicked: {
                            const popupParent = toolsPopover.parent
                            const p = toolsButton.mapToItem(popupParent, 0, toolsButton.height + 6)
                            const maxX = Math.max(8, popupParent.width - toolsPopover.width - 8)
                            const maxY = Math.max(8, popupParent.height - toolsPopover.height - 8)
                            toolsPopover.x = Math.max(8, Math.min(p.x, maxX))
                            toolsPopover.y = Math.max(8, Math.min(p.y, maxY))
                            toolsPopover.open()
                        }
                    }

                    AppButton {
                        text: "\u22EE"
                        fillColor: panelMuted
                        borderLine: borderColor
                        implicitWidth: 44
                        font.family: "Segoe UI"
                        font.pixelSize: 18
                        onClicked: overflowMenu.popup()
                    }

                    Popup {
                        id: toolsPopover
                        parent: Overlay.overlay
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        x: 0
                        y: 0
                        width: 300
                        padding: 12

                        background: Rectangle {
                            radius: 12
                            color: panelColor
                            border.color: borderColor
                        }

                        contentItem: ColumnLayout {
                            spacing: 8

                            AppButton {
                                Layout.fillWidth: true
                                text: "Run Integrity Check"
                                fillColor: panelMuted
                                borderLine: borderColor
                                onClicked: {
                                    const result = fileListModel.runIntegrityScan()
                                    integrityMessage =
                                        "Integrity scan: " +
                                        result.activeCount + " active, " +
                                        result.missingCount + " missing"
                                    refreshEverything()
                                    toolsPopover.close()
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: "Rebuild Index"
                                fillColor: panelMuted
                                borderLine: borderColor
                                onClicked: {
                                    fileListModel.rebuildSearchIndex()
                                    actionMessage = "Search index rebuild queued"
                                    refreshServiceStatus()
                                    toolsPopover.close()
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: "Sync Now (Experimental)"
                                enabled: cloudSyncModel && Number(syncStatusMap.connectedProviders || 0) > 0
                                fillColor: panelMuted
                                borderLine: borderColor
                                onClicked: {
                                    cloudSyncModel.syncNow()
                                    actionMessage = "Cloud sync (Experimental) triggered"
                                    refreshServiceStatus()
                                    toolsPopover.close()
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: "Export Diagnostics"
                                fillColor: panelMuted
                                borderLine: borderColor
                                onClicked: {
                                    const exportResult = fileListModel.exportDiagnosticsBundle()
                                    if (exportResult.ok) {
                                        actionMessage = "Diagnostics exported: " + exportResult.path
                                        Qt.openUrlExternally(localFileUrl(exportResult.path))
                                    } else {
                                        actionMessage = "Diagnostics export failed: " + exportResult.error
                                        if (String(exportResult.path || "") !== "") {
                                            Qt.openUrlExternally(localFileUrl(exportResult.path))
                                        }
                                    }
                                    toolsPopover.close()
                                }
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: "Reload View"
                                fillColor: panelMuted
                                borderLine: borderColor
                                onClicked: {
                                    integrityMessage = ""
                                    actionMessage = ""
                                    fileListModel.reload()
                                    refreshCollections()
                                    refreshHierarchy()
                                    refreshRules()
                                    selectedSidebarCollectionId = -1
                                    selectedSidebarCollectionName = ""
                                    selectedHierarchyLabel = "All Files"
                                    clearRetrievalControls()
                                    refreshServiceStatus()
                                    detailsRefreshCounter += 1
                                    toolsPopover.close()
                                }
                            }
                        }
                    }

                    Menu {
                        id: overflowMenu

                        MenuItem {
                            text: compactListMode ? "Comfortable Rows" : "Compact Rows"
                            onTriggered: compactListMode = !compactListMode
                        }

                        MenuItem {
                            text: providerIsConnected("google_drive")
                                  ? "Disconnect Google (Experimental)"
                                  : "Connect Google (Experimental)"
                            onTriggered: {
                                if (providerIsConnected("google_drive")) {
                                    disconnectCloudProvider("google_drive", "Google Drive")
                                } else {
                                    beginCloudProviderConnect("google_drive", "Google Drive")
                                }
                            }
                        }

                        MenuItem {
                            text: "Report an Issue"
                            onTriggered: {
                                supportSopDialog.open()
                            }
                        }

                        MenuItem {
                            text: "Clear Alerts"
                            onTriggered: {
                                integrityMessage = ""
                                actionMessage = ""
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: filtersExpanded
                    radius: 14
                    color: panelSoft
                    border.color: borderColor
                    Layout.topMargin: 2
                    implicitHeight: filtersColumn.implicitHeight + 24

                    ColumnLayout {
                        id: filtersColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Label {
                            text: "ADVANCED / NON-PRIMARY"
                            color: textSecondary
                            font.pixelSize: 20
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 440
                                radius: 12
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: filterPanelColumn.implicitHeight + 18

                                ColumnLayout {
                                    id: filterPanelColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    SectionTitle { text: "Filters" }

                                    Label { text: "STATUS"; color: textSecondary; font.bold: true; font.pixelSize: 12 }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: statusFilterOptions
                                            delegate: Rectangle {
                                                radius: 14
                                                color: selectedStatusFilters.indexOf(modelData.value) >= 0 ? successBg : panelMuted
                                                border.color: selectedStatusFilters.indexOf(modelData.value) >= 0 ? successBorder : borderColor
                                                implicitHeight: 28
                                                implicitWidth: statusChipText.implicitWidth + 18

                                                Label {
                                                    id: statusChipText
                                                    anchors.centerIn: parent
                                                    text: modelData.label
                                                    color: selectedStatusFilters.indexOf(modelData.value) >= 0 ? successText : textSecondary
                                                    font.pixelSize: 12
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: toggleStatusFilter(modelData.value)
                                                }
                                            }
                                        }
                                    }

                                    Label { text: "EXTENSION"; color: textSecondary; font.bold: true; font.pixelSize: 12 }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: extensionFilterOptions
                                            delegate: Rectangle {
                                                radius: 14
                                                color: selectedExtensionFilters.indexOf(modelData) >= 0 ? panelColor : panelMuted
                                                border.color: selectedExtensionFilters.indexOf(modelData) >= 0 ? accentBorder : borderColor
                                                implicitHeight: 28
                                                implicitWidth: extChipText.implicitWidth + 18

                                                Label {
                                                    id: extChipText
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    color: selectedExtensionFilters.indexOf(modelData) >= 0 ? accentColor : textSecondary
                                                    font.pixelSize: 12
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: toggleExtensionFilter(modelData)
                                                }
                                            }
                                        }
                                    }

                                    Label { text: "DOCUMENT TYPE"; color: textSecondary; font.bold: true; font.pixelSize: 12 }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: documentTypeFilterOptions
                                            delegate: Rectangle {
                                                radius: 14
                                                color: selectedDocumentTypeFilters.indexOf(modelData) >= 0 ? panelColor : panelMuted
                                                border.color: selectedDocumentTypeFilters.indexOf(modelData) >= 0 ? accentBorder : borderColor
                                                implicitHeight: 28
                                                implicitWidth: docTypeChipText.implicitWidth + 18

                                                Label {
                                                    id: docTypeChipText
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    color: selectedDocumentTypeFilters.indexOf(modelData) >= 0 ? accentColor : textSecondary
                                                    font.pixelSize: 12
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: toggleDocumentTypeFilter(modelData)
                                                }
                                            }
                                        }
                                    }

                                    Label { text: "DATE RANGE"; color: textSecondary; font.bold: true; font.pixelSize: 12 }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        AppComboBox {
                                            id: dateFieldCombo
                                            Layout.fillWidth: true
                                            model: ["No Date Filter", "Indexed", "Modified", "Created"]
                                            onActivated: applyAdvancedFilters()
                                        }

                                        AppTextField {
                                            id: dateFromField
                                            Layout.fillWidth: true
                                            placeholderText: "From YYYY-MM-DD"
                                            onEditingFinished: applyAdvancedFilters()
                                        }

                                        AppTextField {
                                            id: dateToField
                                            Layout.fillWidth: true
                                            placeholderText: "To YYYY-MM-DD"
                                            onEditingFinished: applyAdvancedFilters()
                                        }
                                    }

                                    Label { text: "SORTING"; color: textSecondary; font.bold: true; font.pixelSize: 12 }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        AppComboBox {
                                            id: sortFieldCombo
                                            Layout.fillWidth: true
                                            model: ["Indexed Date", "Name", "Modified Date", "Created Date", "Size", "Path"]
                                            onActivated: applySorting()
                                        }

                                        AppComboBox {
                                            id: sortOrderCombo
                                            Layout.preferredWidth: 160
                                            model: ["Ascending", "Descending"]
                                            currentIndex: 1
                                            onActivated: applySorting()
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        AppButton {
                                            Layout.fillWidth: true
                                            text: "Apply Filters"
                                            fillColor: accentColor
                                            borderLine: accentColor
                                            labelColor: "#ffffff"
                                            font.bold: true
                                            onClicked: {
                                                applyAdvancedFilters()
                                                applySorting()
                                            }
                                        }

                                        AppButton {
                                            Layout.fillWidth: true
                                            text: "Reset to Default"
                                            fillColor: panelMuted
                                            borderLine: borderColor
                                            onClicked: clearRetrievalControls()
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        radius: 10
                                        color: panelMuted
                                        border.color: borderColor
                                        implicitHeight: appliedFiltersRow.implicitHeight + 12

                                        RowLayout {
                                            id: appliedFiltersRow
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 8

                                            Label {
                                                Layout.fillWidth: true
                                                text: activeFilterSummary()
                                                color: textSecondary
                                                elide: Text.ElideRight
                                            }

                                            AppButton {
                                                text: "Clear Hierarchy Filter"
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: {
                                                    selectedHierarchyLabel = "All Files"
                                                    fileListModel.clearHierarchyFilter()
                                                    detailsRefreshCounter += 1
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 320
                                radius: 12
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: toolsPanelColumn.implicitHeight + 18

                                ColumnLayout {
                                    id: toolsPanelColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    SectionTitle { text: "Tools Menu" }
                                    Label {
                                        text: "Advanced actions are hidden until needed."
                                        color: textSecondary
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }

                                    AppButton {
                                        Layout.fillWidth: true
                                        text: "Run Integrity Check"
                                        fillColor: panelMuted
                                        borderLine: borderColor
                                        onClicked: {
                                            const result = fileListModel.runIntegrityScan()
                                            integrityMessage =
                                                "Integrity scan: " +
                                                result.activeCount + " active, " +
                                                result.missingCount + " missing"
                                            refreshEverything()
                                        }
                                    }

                                    AppButton {
                                        Layout.fillWidth: true
                                        text: "Rebuild Index"
                                        fillColor: panelMuted
                                        borderLine: borderColor
                                        onClicked: {
                                            fileListModel.rebuildSearchIndex()
                                            actionMessage = "Search index rebuild queued"
                                            refreshServiceStatus()
                                        }
                                    }

                                    AppButton {
                                        Layout.fillWidth: true
                                        text: "Reload View"
                                        fillColor: panelMuted
                                        borderLine: borderColor
                                        onClicked: {
                                            integrityMessage = ""
                                            actionMessage = ""
                                            fileListModel.reload()
                                            refreshEverything()
                                        }
                                    }

                                    AppButton {
                                        Layout.fillWidth: true
                                        text: "Sync Now (Experimental)"
                                        enabled: cloudSyncModel && Number(syncStatusMap.connectedProviders || 0) > 0
                                        fillColor: panelMuted
                                        borderLine: borderColor
                                        onClicked: {
                                            cloudSyncModel.syncNow()
                                            actionMessage = "Cloud sync (Experimental) triggered"
                                            refreshServiceStatus()
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 420
                                radius: 12
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: collectionToolsColumn.implicitHeight + 18

                                ColumnLayout {
                                    id: collectionToolsColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    RowLayout {
                                        Layout.fillWidth: true
                                        SectionTitle { text: "Collection Tools (Hierarchy Mode)" }
                                        Item { Layout.fillWidth: true }
                                        AppButton {
                                            text: hierarchyManageExpanded ? "Hide" : "Manage"
                                            fillColor: panelMuted
                                            borderLine: borderColor
                                            onClicked: hierarchyManageExpanded = !hierarchyManageExpanded
                                        }
                                    }

                                    ColumnLayout {
                                        visible: hierarchyManageExpanded
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Rectangle {
                                            Layout.fillWidth: true
                                            radius: 10
                                            color: panelSoft
                                            border.color: borderColor
                                            implicitHeight: Math.min(190, hierarchyManageList.contentHeight + 12)

                                            ListView {
                                                id: hierarchyManageList
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                model: collectionTreeModel
                                                spacing: 6
                                                clip: true

                                                delegate: Rectangle {
                                                    width: hierarchyManageList.width
                                                    height: 34
                                                    radius: 8
                                                    color: selectedSidebarCollectionId === modelData.id ? panelMuted : "transparent"
                                                    border.color: selectedSidebarCollectionId === modelData.id ? accentBorder : "transparent"

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 4
                                                        spacing: 8

                                                        Label {
                                                            text: modelData.displayName
                                                            Layout.fillWidth: true
                                                            color: selectedSidebarCollectionId === modelData.id ? textPrimary : textSecondary
                                                            elide: Text.ElideRight
                                                        }

                                                        AppButton {
                                                            text: "\u22EF"
                                                            fillColor: panelColor
                                                            borderLine: borderColor
                                                            implicitWidth: 30
                                                            onClicked: {
                                                                selectedSidebarCollectionId = modelData.id
                                                                selectedSidebarCollectionName = modelData.name
                                                                selectedHierarchyManageCollectionId = modelData.id
                                                                selectedHierarchyManageCollectionName = modelData.name
                                                                fileListModel.filterByCollection(modelData.id)
                                                                refreshRules()
                                                                detailsRefreshCounter += 1
                                                                collectionContextMenu.popup()
                                                            }
                                                        }
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            selectedSidebarCollectionId = modelData.id
                                                            selectedSidebarCollectionName = modelData.name
                                                            selectedHierarchyManageCollectionId = modelData.id
                                                            selectedHierarchyManageCollectionName = modelData.name
                                                            sidebarMode = "collections"
                                                            fileListModel.filterByCollection(modelData.id)
                                                            refreshRules()
                                                            detailsRefreshCounter += 1
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Menu {
                                            id: collectionContextMenu

                                            MenuItem {
                                                text: "Rename Collection"
                                                enabled: selectedSidebarCollectionId >= 0
                                                onTriggered: {
                                                    renameCollectionField.text = selectedSidebarCollectionName
                                                    renameCollectionDialog.open()
                                                }
                                            }

                                            MenuItem {
                                                text: "Delete Collection"
                                                enabled: selectedSidebarCollectionId >= 0
                                                onTriggered: {
                                                    const ok = fileListModel.deleteCollection(selectedSidebarCollectionId)
                                                    if (ok) {
                                                        selectedSidebarCollectionId = -1
                                                        selectedSidebarCollectionName = ""
                                                        refreshEverything()
                                                    }
                                                }
                                            }

                                            MenuSeparator {}

                                            MenuItem {
                                                text: "Add Root Collection"
                                                onTriggered: {
                                                    addCollectionDialog.addCollectionParentId = -1
                                                    addCollectionDialog.open()
                                                }
                                            }

                                            MenuItem {
                                                text: "Add Child Collection"
                                                enabled: selectedSidebarCollectionId >= 0
                                                onTriggered: {
                                                    addCollectionDialog.addCollectionParentId = selectedSidebarCollectionId
                                                    addCollectionDialog.open()
                                                }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            AppButton {
                                                Layout.fillWidth: true
                                                text: "Clear Hierarchy Filter"
                                                fillColor: panelMuted
                                                borderLine: borderColor
                                                onClicked: {
                                                    selectedHierarchyLabel = "All Files"
                                                    fileListModel.clearHierarchyFilter()
                                                    detailsRefreshCounter += 1
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            radius: 10
                                            color: panelSoft
                                            border.color: borderColor
                                            implicitHeight: smartRulesColumn.implicitHeight + 14

                                            ColumnLayout {
                                                id: smartRulesColumn
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 8

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    SectionTitle { text: "Smart Rules (Advanced)" }
                                                    Item { Layout.fillWidth: true }
                                                    Label {
                                                        text: "Subtle visual styling"
                                                        color: textSecondary
                                                        font.pixelSize: 11
                                                    }
                                                }

                                                AppButton {
                                                    text: "Add New Smart Rule"
                                                    fillColor: panelMuted
                                                    borderLine: borderColor
                                                    enabled: selectedSidebarCollectionId >= 0
                                                    onClicked: addRuleDialog.open()
                                                }

                                                Repeater {
                                                    model: selectedCollectionRules
                                                    delegate: Rectangle {
                                                        Layout.fillWidth: true
                                                        radius: 8
                                                        color: panelColor
                                                        border.color: borderColor
                                                        implicitHeight: 46

                                                        RowLayout {
                                                            anchors.fill: parent
                                                            anchors.margins: 6
                                                            spacing: 8

                                                            Label {
                                                                Layout.fillWidth: true
                                                                text: modelData.displayText
                                                                color: textPrimary
                                                                elide: Text.ElideRight
                                                            }

                                                            AppButton {
                                                                text: "Delete"
                                                                fillColor: dangerBg
                                                                borderLine: dangerBorder
                                                                labelColor: dangerText
                                                                onClicked: {
                                                                    const ok = fileListModel.deleteCollectionRule(modelData.id)
                                                                    if (ok) {
                                                                        refreshRules()
                                                                        fileListModel.refreshCurrentView()
                                                                        detailsRefreshCounter += 1
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }

                                                Label {
                                                    visible: selectedCollectionRules.length === 0
                                                    text: selectedSidebarCollectionId >= 0
                                                          ? "No smart rules yet"
                                                          : "Select a collection to manage its rules"
                                                    color: textSecondary
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 16
                    color: panelColor
                    border.color: borderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 12
                            color: panelMuted
                            border.color: borderColor
                            implicitHeight: summaryRow.implicitHeight + 16

                            RowLayout {
                                id: summaryRow
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                Label {
                                    text: listView.count + " Results"
                                    color: textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Rectangle {
                                    radius: 12
                                    color: panelColor
                                    border.color: borderColor
                                    implicitHeight: contextLabel.implicitHeight + 10
                                    implicitWidth: contextLabel.implicitWidth + 18

                                    Label {
                                        id: contextLabel
                                        anchors.centerIn: parent
                                        text: "Context: " + currentContextLabel
                                        color: textSecondary
                                        font.pixelSize: 12
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                AppButton {
                                    text: "View Options"
                                    fillColor: panelColor
                                    borderLine: borderColor
                                    onClicked: viewOptionsMenu.popup()
                                }

                                Menu {
                                    id: viewOptionsMenu

                                    MenuItem {
                                        text: compactListMode ? "Comfortable Rows" : "Compact Rows"
                                        onTriggered: compactListMode = !compactListMode
                                    }

                                    MenuItem {
                                        text: "Reset View"
                                        onTriggered: {
                                            compactListMode = false
                                            listView.positionViewAtBeginning()
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: listView
                                anchors.fill: parent
                                model: fileListModel
                                spacing: 6
                                clip: true

                                delegate: Rectangle {
                                    width: listView.width
                                    implicitHeight: cardContent.implicitHeight + (compactListMode ? 10 : 16)
                                    radius: 12
                                    color: ListView.isCurrentItem ? "#f4fbfa" : panelColor
                                    border.color: ListView.isCurrentItem ? accentBorder : borderColor
                                    border.width: 1

                                    Rectangle {
                                        width: 4
                                        radius: 2
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 8
                                        color: accentColor
                                        visible: ListView.isCurrentItem
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton

                                        onClicked: {
                                            listView.currentIndex = index
                                            detailsRefreshCounter += 1
                                        }

                                        onDoubleClicked: {
                                            listView.currentIndex = index
                                            detailsRefreshCounter += 1
                                            openReaderForIndex(index)
                                        }
                                    }

                                    ColumnLayout {
                                        id: cardContent
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        anchors.topMargin: compactListMode ? 7 : 10
                                        anchors.bottomMargin: compactListMode ? 7 : 10
                                        spacing: compactListMode ? 3 : 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Rectangle {
                                                Layout.preferredWidth: 44
                                                Layout.preferredHeight: 44
                                                radius: 10
                                                color: panelMuted
                                                border.color: accentBorder

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: fileIconGlyph(extension)
                                                    color: accentColor
                                                    font.family: "Segoe MDL2 Assets"
                                                    font.pixelSize: 17
                                                    font.bold: true
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: compactListMode ? 2 : 4

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: name
                                                    color: textPrimary
                                                    font.pixelSize: 18
                                                    font.bold: true
                                                    maximumLineCount: 1
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: path
                                                    color: textSecondary
                                                    elide: Text.ElideMiddle
                                                    wrapMode: Text.NoWrap
                                                    font.pixelSize: 12
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: "Source: " + (source && source !== "" ? source : "All Files")
                                                    color: textSecondary
                                                    font.pixelSize: 12
                                                    maximumLineCount: 1
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.preferredWidth: 158
                                                spacing: compactListMode ? 1 : 2

                                                Label {
                                                    text: "Type/Size:"
                                                    color: textSecondary
                                                    font.pixelSize: 11
                                                }

                                                Label {
                                                    text: fileTypeLabel(extension) + " / " + formatSize(sizeBytes)
                                                    color: textPrimary
                                                    font.pixelSize: 12
                                                }

                                                Label {
                                                    text: "Indexed Date:"
                                                    color: textSecondary
                                                    font.pixelSize: 11
                                                }

                                                Label {
                                                    text: formatIndexedDate(indexedAt)
                                                    color: textPrimary
                                                    font.pixelSize: 12
                                                }
                                            }

                                            Rectangle {
                                                radius: 12
                                                color: statusBadgeBg(statusValue, searchMatchReason, indexedAt)
                                                border.color: statusBadgeBorder(statusValue, searchMatchReason, indexedAt)
                                                implicitWidth: statusBadgeRow.implicitWidth + 16
                                                implicitHeight: 28

                                                RowLayout {
                                                    id: statusBadgeRow
                                                    anchors.centerIn: parent
                                                    spacing: 4

                                                    Label {
                                                        visible: statusValue === 1
                                                        text: "\uE814"
                                                        color: statusBadgeTextColor(statusValue, searchMatchReason, indexedAt)
                                                        font.family: "Segoe MDL2 Assets"
                                                        font.pixelSize: 11
                                                    }

                                                    Label {
                                                        id: statusLabel
                                                        text: statusBadgeLabel(statusValue, searchMatchReason, indexedAt)
                                                        color: statusBadgeTextColor(statusValue, searchMatchReason, indexedAt)
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }
                                                }
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: searchField.text.trim() !== "" && searchMatchReason !== ""
                                            text: "Why matched: " + searchMatchReason
                                            color: accentColor
                                            font.pixelSize: 12
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            font.bold: true
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: searchField.text.trim() !== "" && searchSnippet !== ""
                                            text: searchSnippet
                                            color: textSecondary
                                            font.pixelSize: 12
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                visible: listView.count === 0
                                spacing: 10

                                Image {
                                    source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/no-results-recovery.svg"
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize.width: 220
                                    sourceSize.height: 150
                                    Layout.preferredWidth: 220
                                    Layout.preferredHeight: 150
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Label {
                                    text: "No memories recovered yet."
                                    font.pixelSize: 20
                                    color: textSecondary
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                AppButton {
                                    text: "Search memories"
                                    fillColor: accentColor
                                    borderLine: accentColor
                                    labelColor: "#ffffff"
                                    Layout.alignment: Qt.AlignHCenter
                                    onClicked: searchField.forceActiveFocus()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: detailsPanel
                    Layout.preferredWidth: 380
                    Layout.fillHeight: true
                    radius: 16
                    color: panelColor
                    border.color: borderColor

                    property var selectedFile: {
                        let _ = detailsRefreshCounter
                        return listView.currentIndex >= 0
                                ? fileListModel.getDetails(listView.currentIndex)
                                : null
                    }

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 14
                        clip: true

                        ColumnLayout {
                            width: detailsPanel.width - 28
                            spacing: 14

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 14
                                color: panelMuted
                                border.color: borderColor
                                implicitHeight: detailsHeaderColumn.implicitHeight + 24

                                ColumnLayout {
                                    id: detailsHeaderColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    Label {
                                        text: "Memory Selected: " + (detailsPanel.selectedFile ? detailsPanel.selectedFile.name : "None")
                                        color: textPrimary
                                        font.pixelSize: 36
                                        font.bold: true
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: "Context: " + currentContextLabel
                                        color: textSecondary
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }

                                    RowLayout {
                                        visible: detailsPanel.selectedFile !== null
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Rectangle {
                                            Layout.preferredWidth: 54
                                            Layout.preferredHeight: 54
                                            radius: 10
                                            color: panelColor
                                            border.color: borderColor

                                            Label {
                                                anchors.centerIn: parent
                                                text: fileIconGlyph(detailsPanel.selectedFile ? detailsPanel.selectedFile.extension : "")
                                                font.family: "Segoe MDL2 Assets"
                                                font.pixelSize: 20
                                                color: accentColor
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            Label {
                                                Layout.fillWidth: true
                                                text: detailsPanel.selectedFile ? detailsPanel.selectedFile.name : ""
                                                color: textPrimary
                                                font.pixelSize: 24
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: detailsPanel.selectedFile ? detailsPanel.selectedFile.path : ""
                                                color: textSecondary
                                                font.pixelSize: 12
                                                elide: Text.ElideMiddle
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        visible: detailsPanel.selectedFile === null
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Image {
                                            source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/empty-memory-state.svg"
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: 220
                                            sourceSize.height: 140
                                            Layout.preferredWidth: 220
                                            Layout.preferredHeight: 140
                                            Layout.alignment: Qt.AlignHCenter
                                        }

                                        Label {
                                            text: "Select an item to view detailed memory information."
                                            color: textSecondary
                                            font.pixelSize: 12
                                            wrapMode: Text.Wrap
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.fillWidth: true
                                        }

                                        AppButton {
                                            text: "Start new recovery"
                                            fillColor: accentColor
                                            borderLine: accentColor
                                            labelColor: "#ffffff"
                                            Layout.alignment: Qt.AlignHCenter
                                            onClicked: {
                                                clearRetrievalControls()
                                                searchField.forceActiveFocus()
                                            }
                                        }
                                    }

                                    Rectangle {
                                        visible: detailsPanel.selectedFile !== null
                                        radius: 14
                                        color: statusBadgeBg(detailsPanel.selectedFile ? detailsPanel.selectedFile.statusValue : -1,
                                                             detailsPanel.selectedFile ? detailsPanel.selectedFile.searchMatchReason : "",
                                                             detailsPanel.selectedFile ? detailsPanel.selectedFile.indexedAt : "")
                                        border.color: statusBadgeBorder(detailsPanel.selectedFile ? detailsPanel.selectedFile.statusValue : -1,
                                                                        detailsPanel.selectedFile ? detailsPanel.selectedFile.searchMatchReason : "",
                                                                        detailsPanel.selectedFile ? detailsPanel.selectedFile.indexedAt : "")
                                        implicitWidth: detailsStatusLabel.implicitWidth + 24
                                        implicitHeight: 30

                                        Label {
                                            id: detailsStatusLabel
                                            anchors.centerIn: parent
                                            text: detailsPanel.selectedFile
                                                  ? statusBadgeLabel(detailsPanel.selectedFile.statusValue,
                                                                     detailsPanel.selectedFile.searchMatchReason,
                                                                     detailsPanel.selectedFile.indexedAt)
                                                  : ""
                                            color: statusBadgeTextColor(detailsPanel.selectedFile ? detailsPanel.selectedFile.statusValue : -1,
                                                                        detailsPanel.selectedFile ? detailsPanel.selectedFile.searchMatchReason : "",
                                                                        detailsPanel.selectedFile ? detailsPanel.selectedFile.indexedAt : "")
                                            font.bold: true
                                            font.pixelSize: 12
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        AppButton {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 42
                                            text: "Reader Mode"
                                            enabled: detailsPanel.selectedFile !== null && detailsPanel.selectedFile.statusValue === 0
                                            fillColor: accentColor
                                            borderLine: accentColor
                                            labelColor: "#ffffff"
                                            font.bold: true
                                            onClicked: openReaderForIndex(listView.currentIndex)
                                        }

                                        SectionTitle {
                                            visible: detailsPanel.selectedFile !== null
                                            text: "Actions"
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 10
                                            rowSpacing: 10

                                            AppButton {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 40
                                                text: "Open Original"
                                                enabled: detailsPanel.selectedFile !== null && detailsPanel.selectedFile.statusValue === 0
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: {
                                                    const ok = fileListModel.openFile(listView.currentIndex)
                                                    actionMessage = ok ? "Original source opened" : "Unable to open source"
                                                }
                                            }

                                            AppButton {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 40
                                                text: "Open Folder"
                                                enabled: detailsPanel.selectedFile !== null
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: {
                                                    const ok = fileListModel.openContainingFolder(listView.currentIndex)
                                                    actionMessage = ok ? "Folder opened" : "Unable to open folder"
                                                }
                                            }

                                            AppButton {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 40
                                                text: "Remove File"
                                                enabled: detailsPanel.selectedFile !== null
                                                fillColor: dangerBg
                                                borderLine: dangerBorder
                                                labelColor: dangerText
                                                onClicked: {
                                                    const ok = fileListModel.removeFile(listView.currentIndex)
                                                    actionMessage = ok ? "File removed from ELLA" : "Unable to remove file"
                                                    if (ok)
                                                        listView.currentIndex = -1
                                                }
                                            }

                                            AppButton {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 40
                                                text: "Edit Metadata"
                                                enabled: detailsPanel.selectedFile !== null
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: {
                                                    if (!detailsPanel.selectedFile)
                                                        return

                                                    editTechnicalDomainField.text = detailsPanel.selectedFile.technicalDomain || ""
                                                    editSubjectField.text = detailsPanel.selectedFile.subject || ""
                                                    editSubtopicField.text = detailsPanel.selectedFile.subtopic || ""
                                                    editLocationField.text = detailsPanel.selectedFile.location || ""
                                                    editSourceField.text = detailsPanel.selectedFile.source || ""
                                                    editAuthorField.text = detailsPanel.selectedFile.author || ""
                                                    editDocumentTypeField.text = detailsPanel.selectedFile.documentType || ""
                                                    editRemarksField.text = detailsPanel.selectedFile.remarks || ""
                                                    editMetadataDialog.open()
                                                }
                                            }
                                        }

                                        AppButton {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 40
                                            text: "Relink Missing File"
                                            visible: detailsPanel.selectedFile && detailsPanel.selectedFile.statusValue === 1
                                            enabled: detailsPanel.selectedFile && detailsPanel.selectedFile.statusValue === 1
                                            fillColor: dangerBg
                                            borderLine: dangerBorder
                                            labelColor: dangerText
                                            onClicked: {
                                                relinkPathField.text = ""
                                                pendingRelinkPath = ""
                                                relinkDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 16
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: fileInfoHeader.implicitHeight + 24 + (fileInfoExpanded ? fileInfoContent.implicitHeight + 8 : 0)

                                ColumnLayout {
                                    id: fileInfoColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    RowLayout {
                                        id: fileInfoHeader
                                        Layout.fillWidth: true

                                        SectionTitle { text: "File information" }
                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: fileInfoExpanded ? "\uE70D" : "\uE76C"
                                            color: textSecondary
                                            font.family: "Segoe MDL2 Assets"
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: fileInfoExpanded = !fileInfoExpanded
                                        }
                                    }

                                    ColumnLayout {
                                        id: fileInfoContent
                                        visible: fileInfoExpanded
                                        Layout.fillWidth: true
                                        spacing: 10

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 6

                                            MetaLabel { text: "Path" }
                                            ValueText { text: detailsPanel.selectedFile ? detailsPanel.selectedFile.path : "-" }

                                            MetaLabel { text: "Extension" }
                                            ValueText {
                                                text: detailsPanel.selectedFile
                                                      ? (detailsPanel.selectedFile.extension !== "" ? detailsPanel.selectedFile.extension : "unknown")
                                                      : "-"
                                            }

                                            MetaLabel { text: "MIME" }
                                            ValueText { text: detailsPanel.selectedFile ? detailsPanel.selectedFile.mimeType : "-" }

                                            MetaLabel { text: "Size" }
                                            ValueText { text: detailsPanel.selectedFile ? formatSize(detailsPanel.selectedFile.sizeBytes) : "-" }

                                            MetaLabel { text: "Indexed" }
                                            ValueText { text: detailsPanel.selectedFile ? detailsPanel.selectedFile.indexedAt : "-" }

                                            MetaLabel { text: "Created" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.createdAt !== ""
                                                      ? detailsPanel.selectedFile.createdAt
                                                      : "-"
                                            }

                                            MetaLabel { text: "Modified" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.modifiedAt !== ""
                                                      ? detailsPanel.selectedFile.modifiedAt
                                                      : "-"
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 16
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: metadataHeader.implicitHeight + 24 + (metadataExpanded ? metadataContent.implicitHeight + 8 : 0)

                                ColumnLayout {
                                    id: metadataColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    RowLayout {
                                        id: metadataHeader
                                        Layout.fillWidth: true

                                        SectionTitle { text: "Metadata" }
                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: metadataExpanded ? "\uE70D" : "\uE76C"
                                            color: textSecondary
                                            font.family: "Segoe MDL2 Assets"
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: metadataExpanded = !metadataExpanded
                                        }
                                    }

                                    ColumnLayout {
                                        id: metadataContent
                                        visible: metadataExpanded
                                        Layout.fillWidth: true
                                        spacing: 10

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 6

                                            MetaLabel { text: "Technical Subject" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.technicalDomain !== ""
                                                      ? detailsPanel.selectedFile.technicalDomain
                                                      : "-"
                                            }

                                            MetaLabel { text: "Subject" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.subject !== ""
                                                      ? detailsPanel.selectedFile.subject
                                                      : "-"
                                            }

                                            MetaLabel { text: "Subtopic" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.subtopic !== ""
                                                      ? detailsPanel.selectedFile.subtopic
                                                      : "-"
                                            }

                                            MetaLabel { text: "Location" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.location !== ""
                                                      ? detailsPanel.selectedFile.location
                                                      : "-"
                                            }

                                            MetaLabel { text: "Source" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.source !== ""
                                                      ? detailsPanel.selectedFile.source
                                                      : "-"
                                            }

                                            MetaLabel { text: "Author" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.author !== ""
                                                      ? detailsPanel.selectedFile.author
                                                      : "-"
                                            }

                                            MetaLabel { text: "Document Type" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.documentType !== ""
                                                      ? detailsPanel.selectedFile.documentType
                                                      : "-"
                                            }

                                            MetaLabel { text: "Remarks" }
                                            ValueText {
                                                text: detailsPanel.selectedFile && detailsPanel.selectedFile.remarks !== ""
                                                      ? detailsPanel.selectedFile.remarks
                                                      : "-"
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                radius: 16
                                color: panelColor
                                border.color: borderColor
                                implicitHeight: assignmentColumn.implicitHeight + 24

                                ColumnLayout {
                                    id: assignmentColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    SectionTitle { text: "Assigned collections" }

                                    Label {
                                        text: "Current Assignments"
                                        color: textSecondary
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Repeater {
                                            model: detailsPanel.selectedFile && detailsPanel.selectedFile.collectionItems
                                                   ? detailsPanel.selectedFile.collectionItems
                                                   : []

                                            delegate: Rectangle {
                                                radius: 14
                                                color: panelMuted
                                                border.color: borderColor
                                                implicitHeight: 30
                                                implicitWidth: assignmentChipText.implicitWidth + 22

                                                RowLayout {
                                                    anchors.centerIn: parent
                                                    spacing: 8

                                                    Label {
                                                        id: assignmentChipText
                                                        text: modelData.displayName
                                                        color: textPrimary
                                                        font.pixelSize: 12
                                                    }

                                                    Label {
                                                        text: "\uE711"
                                                        color: textSecondary
                                                        font.family: "Segoe MDL2 Assets"
                                                        font.pixelSize: 10
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        const ok = fileListModel.removeFileFromCollection(
                                                            listView.currentIndex,
                                                            modelData.id
                                                        )
                                                        if (ok) {
                                                            refreshEverything()
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        visible: !detailsPanel.selectedFile || !detailsPanel.selectedFile.collectionItems || detailsPanel.selectedFile.collectionItems.length === 0
                                        text: "No collection assignments"
                                        color: textSecondary
                                    }

                                    Label {
                                        text: "New Assignment"
                                        color: textSecondary
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        AppComboBox {
                                            id: collectionCombo
                                            Layout.fillWidth: true
                                            model: collectionPickerModel
                                            textRole: "displayName"
                                            enabled: collectionPickerModel.length > 0

                                            onActivated: {
                                                if (currentIndex >= 0) {
                                                    selectedCollectionIdForAssign = collectionPickerModel[currentIndex].id
                                                }
                                            }
                                        }

                                        AppButton {
                                            Layout.preferredHeight: 40
                                            text: "Assign"
                                            enabled: listView.currentIndex >= 0 && collectionPickerModel.length > 0
                                            fillColor: accentColor
                                            borderLine: accentColor
                                            labelColor: "#ffffff"
                                            font.bold: true
                                            onClicked: {
                                                const ok = fileListModel.assignCollection(listView.currentIndex, selectedCollectionIdForAssign)
                                                if (ok) {
                                                    refreshEverything()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
}


