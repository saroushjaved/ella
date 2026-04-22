import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Pdf

Page {
    id: root
    EllaTokens { id: theme }

    required property int fileIndex
    required property var fileDetails
    required property var navigationStack
    font.family: theme.fontFamily

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
    readonly property color successBg: theme.successBg
    readonly property color successBorder: theme.successBorder
    readonly property color successText: theme.successText
    readonly property color warningBg: theme.warningBg
    readonly property color warningBorder: theme.warningBorder
    readonly property color warningText: theme.warningText
    readonly property color infoBg: theme.infoBg
    readonly property color infoBorder: theme.infoBorder
    readonly property color infoText: theme.infoText
    readonly property color dangerBg: theme.dangerBg
    readonly property color dangerBorder: theme.dangerBorder
    readonly property color dangerText: theme.dangerText
    readonly property color noteColorDefault: "#fff59d"
    readonly property color pinColorDefault: "#ff6b6b"

    property int fileId: fileDetails && fileDetails.id !== undefined ? Number(fileDetails.id) : -1
    property string textPreview: ""
    property string renderedTextHtml: ""
    property string resolvedPdfPreviewUrl: ""
    property string pdfPreviewErrorText: ""
    property int mediaRangeStartMs: -1
    property var mediaControllerRef: null
    property var mediaPreviewPanelComponent: null
    property string mediaPreviewErrorText: ""

    property real imageZoom: 1.0
    property real textZoom: 1.0
    property real pdfZoomFactor: 1.0

    readonly property real minZoom: 0.5
    readonly property real maxZoom: 8.0

    property var documentNotes: []
    property var visualAnnotations: []
    property var textAnnotations: []
    property var textContentRef: null
    property var textFlickRef: null
    property string annotationTool: ""
    property bool annotationSecondaryExpanded: false
    property string readerStatusMessage: ""
    property string toolingWarningMessage: ""
    property bool readerFileInfoExpanded: true
    property bool readerMetadataExpanded: true
    property bool readerNotesExpanded: true
    property string annotationSearchText: ""
    property int selectedVisualAnnotationId: -1
    property int pdfCurrentPage: 0

    property var indexStatusMap: ({})
    property var searchHealthMap: ({})
    property var importStatusMap: ({})
    property var syncStatusMap: ({})

    property int textSelectionStartCached: -1
    property int textSelectionEndCached: -1
    property string textSelectionAnchorCached: ""

    function escapeHtml(text) {
        if (text === null || text === undefined)
            return ""

        return String(text)
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#39;")
    }

    function normalizeSelectedText(text) {
        if (text === null || text === undefined)
            return ""

        return String(text).replace(/\u2029/g, "\n")
    }

    function clampZoom(value) {
        return Math.max(minZoom, Math.min(maxZoom, value))
    }

    function refreshServiceStatus() {
        indexStatusMap = fileListModel.indexStatus()
        searchHealthMap = fileListModel.searchHealth()
        importStatusMap = fileListModel.importStatus()
        if (cloudSyncModel) {
            syncStatusMap = cloudSyncModel.status()
        } else {
            syncStatusMap = ({})
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

        if (!searchHealthMap.mediaTranscriptionReady && isMediaFile()) {
            warnings.push("Media transcription tools unavailable")
        }

        if (!searchHealthMap.pptConversionReady && isPresentationFile()) {
            warnings.push("Presentation conversion tools unavailable")
        }

        toolingWarningMessage = warnings.join(" | ")
    }

    function normalizedExtension() {
        let ext = String(fileDetails && fileDetails.extension ? fileDetails.extension : "").trim().toLowerCase()
        if (ext.startsWith("."))
            ext = ext.substring(1)
        return ext
    }

    function syncChipState() {
        const indexingRunning = !!indexStatusMap.running
        const connectedProviders = Number(syncStatusMap.connectedProviders || 0)
        const syncRunning = !!syncStatusMap.running
        if (indexingRunning || syncRunning) {
            return "processing"
        }
        if (connectedProviders > 0) {
            return "up_to_date"
        }
        return "disconnected"
    }

    function syncChipText() {
        const state = syncChipState()
        if (state === "processing")
            return "SYNC (EXPERIMENTAL): PROCESSING"
        if (state === "up_to_date")
            return "SYNC (EXPERIMENTAL): UP-TO-DATE"
        return "SYNC (EXPERIMENTAL): GOOGLE DISCONNECTED"
    }

    function syncChipBg() {
        const state = syncChipState()
        if (state === "processing")
            return infoBg
        if (state === "up_to_date")
            return successBg
        return dangerBg
    }

    function syncChipBorder() {
        const state = syncChipState()
        if (state === "processing")
            return infoBorder
        if (state === "up_to_date")
            return successBorder
        return dangerBorder
    }

    function syncChipTextColor() {
        const state = syncChipState()
        if (state === "processing")
            return infoText
        if (state === "up_to_date")
            return successText
        return dangerText
    }

    function isImageFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        return mime.startsWith("image/")
    }

    function isPdfFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        const ext = normalizedExtension()
        return mime === "application/pdf" || ext === "pdf"
    }

    function isPresentationFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        const ext = normalizedExtension()

        if (ext === "ppt" || ext === "pptx" || ext === "odp")
            return true

        return mime === "application/vnd.ms-powerpoint"
                || mime === "application/vnd.openxmlformats-officedocument.presentationml.presentation"
                || mime === "application/vnd.oasis.opendocument.presentation"
    }

    function isVideoFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        const ext = normalizedExtension()

        if (mime.startsWith("video/"))
            return true

        const knownVideoExtensions = ["mp4", "mkv", "mov", "avi", "wmv", "webm", "m4v"]
        return knownVideoExtensions.indexOf(ext) >= 0
    }

    function isAudioFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        const ext = normalizedExtension()

        if (mime.startsWith("audio/"))
            return true

        const knownAudioExtensions = ["mp3", "wav", "m4a", "aac", "flac", "ogg", "opus", "wma"]
        return knownAudioExtensions.indexOf(ext) >= 0
    }

    function isTextFile() {
        const mime = (fileDetails && fileDetails.mimeType ? fileDetails.mimeType : "").toLowerCase()
        const ext = normalizedExtension()

        if (mime.startsWith("text/"))
            return true

        const knownTextExtensions = [
            "txt", "md", "markdown", "log", "csv", "json", "xml", "yaml", "yml",
            "ini", "cfg", "conf", "qml", "cpp", "cc", "c", "h", "hpp",
            "js", "ts", "py", "java", "kt", "rs", "go", "sh", "bat"
        ]

        return knownTextExtensions.indexOf(ext) >= 0
    }

    function hasPreview() {
        return isImageFile() || isPdfFile() || isPresentationFile() || isVideoFile() || isAudioFile() || isTextFile()
    }

    function activeTargetType() {
        if (isImageFile())
            return "image"
        if (isPdfFile())
            return "pdf"
        if (isPresentationFile())
            return "presentation"
        if (isVideoFile())
            return "video"
        if (isAudioFile())
            return "audio"
        if (isTextFile())
            return "text"
        return ""
    }

    function isPinAnnotationType(annotationType) {
        return annotationType === "pin" || annotationType === "pin-note"
    }

    function isRectAnnotationType(annotationType) {
        return annotationType === "rect"
                || annotationType === "rect-note"
                || annotationType === "area-highlight"
    }

    function hasCachedTextSelection() {
        return isTextFile()
                && textSelectionStartCached >= 0
                && textSelectionEndCached > textSelectionStartCached
    }

    function updateCachedTextSelection() {
        if (!textContentRef) {
            clearCachedTextSelection()
            return
        }

        const start = Math.min(textContentRef.selectionStart, textContentRef.selectionEnd)
        const end = Math.max(textContentRef.selectionStart, textContentRef.selectionEnd)

        if (start < 0 || end <= start) {
            clearCachedTextSelection()
            return
        }

        textSelectionStartCached = start
        textSelectionEndCached = end
        textSelectionAnchorCached = normalizeSelectedText(textContentRef.selectedText)
    }

    function clearCachedTextSelection() {
        textSelectionStartCached = -1
        textSelectionEndCached = -1
        textSelectionAnchorCached = ""
    }

    function currentZoomText() {
        if (isImageFile())
            return Math.round(imageZoom * 100) + "%"
        if (isTextFile())
            return Math.round(textZoom * 100) + "%"
        if (isPdfFile() || isPresentationFile())
            return Math.round(pdfZoomFactor * 100) + "%"
        return "100%"
    }

    function zoomIn() {
        if (isImageFile()) {
            imageZoom = clampZoom(imageZoom * 1.15)
        } else if (isTextFile()) {
            textZoom = clampZoom(textZoom * 1.15)
        } else if (isPdfFile() || isPresentationFile()) {
            pdfZoomFactor = clampZoom(pdfZoomFactor * 1.15)
        }
    }

    function zoomOut() {
        if (isImageFile()) {
            imageZoom = clampZoom(imageZoom / 1.15)
        } else if (isTextFile()) {
            textZoom = clampZoom(textZoom / 1.15)
        } else if (isPdfFile() || isPresentationFile()) {
            pdfZoomFactor = clampZoom(pdfZoomFactor / 1.15)
        }
    }

    function resetZoom() {
        imageZoom = 1.0
        textZoom = 1.0
        pdfZoomFactor = 1.0
    }

    function formatDurationMs(msValue) {
        const ms = Math.max(0, Number(msValue || 0))
        const totalSeconds = Math.floor(ms / 1000)
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60

        function pad2(v) {
            return v < 10 ? "0" + v : String(v)
        }

        if (hours > 0)
            return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(seconds)

        return pad2(minutes) + ":" + pad2(seconds)
    }

    function formatDateShort(value) {
        const raw = String(value || "").trim()
        if (raw === "")
            return "-"
        const parsed = new Date(raw)
        if (isNaN(parsed.getTime()))
            return raw
        return Qt.formatDate(parsed, "MMM d, yyyy")
    }

    function formatDateTimeShort(value) {
        const raw = String(value || "").trim()
        if (raw === "")
            return "-"
        const parsed = new Date(raw)
        if (isNaN(parsed.getTime()))
            return raw
        return Qt.formatDateTime(parsed, "MMM d, yyyy hh:mm")
    }

    function formatFileSize(bytesValue) {
        const bytes = Math.max(0, Number(bytesValue || 0))
        if (bytes < 1024)
            return bytes + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    function isMediaFile() {
        return isVideoFile() || isAudioFile()
    }

    function isMediaPlaybackReady() {
        return isMediaFile()
                && mediaControllerRef
                && mediaControllerRef.currentPositionMs
    }

    function currentMediaPositionMs() {
        if (!mediaControllerRef || !mediaControllerRef.currentPositionMs)
            return 0

        return Math.max(0, Math.round(Number(mediaControllerRef.currentPositionMs())))
    }

    function seekMediaRelative(deltaMs) {
        if (!isMediaFile())
            return

        if (!mediaControllerRef || !mediaControllerRef.seekRelativeMs)
            return

        mediaControllerRef.seekRelativeMs(Number(deltaMs || 0))
    }

    function addMediaTimestampAnnotation() {
        if (!isMediaFile())
            return
        if (!isMediaPlaybackReady())
            return

        const atMs = currentMediaPositionMs()
        openNewAnnotationDialog(activeTargetType(), "time-note", -1, -1, -1, "", 0, 0, 0, 0, atMs, atMs)
    }

    function markMediaRangeStart() {
        if (!isMediaFile())
            return
        if (!isMediaPlaybackReady())
            return
        mediaRangeStartMs = currentMediaPositionMs()
    }

    function createMediaRangeAnnotationToCurrent() {
        if (!isMediaFile() || mediaRangeStartMs < 0)
            return
        if (!isMediaPlaybackReady())
            return

        const endMs = currentMediaPositionMs()
        if (endMs <= mediaRangeStartMs)
            return

        openNewAnnotationDialog(activeTargetType(), "time-range", -1, -1, -1, "", 0, 0, 0, 0, mediaRangeStartMs, endMs)
        mediaRangeStartMs = -1
    }

    function jumpToMediaAnnotation(annotationItem) {
        if (!isMediaFile())
            return

        const startMs = annotationItem.timeStartMs !== undefined ? Number(annotationItem.timeStartMs) : -1
        if (startMs < 0)
            return

        if (!mediaControllerRef)
            return

        if (mediaControllerRef.seekToMs)
            mediaControllerRef.seekToMs(startMs)
        if (mediaControllerRef.play)
            mediaControllerRef.play()
    }

    function ensureMediaPreviewComponent() {
        mediaPreviewPanelComponent = null
        mediaPreviewErrorText = ""

        if (!isMediaFile())
            return

        const component = Qt.createComponent("qrc:/qt/qml/SecondBrain/src/ui/MediaPreviewPanel.qml")
        if (component.status === Component.Ready) {
            mediaPreviewPanelComponent = component
            return
        }

        if (component.status === Component.Error) {
            mediaPreviewErrorText = component.errorString()
            return
        }

        component.statusChanged.connect(function() {
            if (component.status === Component.Ready) {
                mediaPreviewPanelComponent = component
            } else if (component.status === Component.Error) {
                mediaPreviewErrorText = component.errorString()
            }
        })
    }

    function refreshAnnotations() {
        if (fileId < 0)
            return

        documentNotes = fileListModel.getDocumentNotes(fileId)

        if (isTextFile()) {
            textAnnotations = fileListModel.getAnnotations(fileId, "text")
            renderedTextHtml = fileListModel.buildAnnotatedTextHtml(fileId, textPreview)
            visualAnnotations = []
        } else {
            textAnnotations = []
            renderedTextHtml = escapeHtml(textPreview).replace(/\n/g, "<br>")
            visualAnnotations = fileListModel.getAnnotations(fileId, activeTargetType())
        }
    }

    function clearAnnotationTool() {
        annotationTool = ""
    }

    function openNewDocumentNoteDialog() {
        documentNoteDialog.editingNoteId = -1
        noteTitleField.text = ""
        noteBodyField.text = ""
        documentNoteDialog.open()
    }

    function openEditDocumentNoteDialog(noteItem) {
        documentNoteDialog.editingNoteId = noteItem.id
        noteTitleField.text = noteItem.title || ""
        noteBodyField.text = noteItem.body || ""
        documentNoteDialog.open()
    }

    function removeDocumentNote(noteId) {
        if (fileListModel.deleteDocumentNote(noteId)) {
            refreshAnnotations()
            readerStatusMessage = "Document note deleted"
        }
    }

    function openNewAnnotationDialog(targetType, annotationType, pageNumber, charStart, charEnd, anchorText, x, y, width, height, timeStartMs, timeEndMs) {
        annotationDialog.editingAnnotationId = -1
        annotationDialog.pendingTargetType = targetType
        annotationDialog.pendingAnnotationType = annotationType
        annotationDialog.pendingPageNumber = pageNumber
        annotationDialog.pendingCharStart = charStart
        annotationDialog.pendingCharEnd = charEnd
        annotationDialog.pendingAnchorText = anchorText
        annotationDialog.pendingX = x
        annotationDialog.pendingY = y
        annotationDialog.pendingWidth = width
        annotationDialog.pendingHeight = height
        annotationDialog.pendingTimeStartMs = timeStartMs !== undefined ? Number(timeStartMs) : -1
        annotationDialog.pendingTimeEndMs = timeEndMs !== undefined ? Number(timeEndMs) : -1
        annotationDialog.selectedColor = String((targetType === "text") ? noteColorDefault : pinColorDefault)
        annotationContentField.text = ""
        annotationDialog.open()
    }

    function openEditAnnotationDialog(annotationItem) {
        annotationDialog.editingAnnotationId = annotationItem.id
        annotationDialog.pendingTargetType = annotationItem.targetType || ""
        annotationDialog.pendingAnnotationType = annotationItem.annotationType || ""
        annotationDialog.pendingPageNumber = annotationItem.pageNumber !== undefined ? Number(annotationItem.pageNumber) : -1
        annotationDialog.pendingCharStart = annotationItem.charStart !== undefined ? Number(annotationItem.charStart) : -1
        annotationDialog.pendingCharEnd = annotationItem.charEnd !== undefined ? Number(annotationItem.charEnd) : -1
        annotationDialog.pendingAnchorText = annotationItem.anchorText || ""
        annotationDialog.pendingX = annotationItem.x !== undefined ? Number(annotationItem.x) : 0
        annotationDialog.pendingY = annotationItem.y !== undefined ? Number(annotationItem.y) : 0
        annotationDialog.pendingWidth = annotationItem.width !== undefined ? Number(annotationItem.width) : 0
        annotationDialog.pendingHeight = annotationItem.height !== undefined ? Number(annotationItem.height) : 0
        annotationDialog.pendingTimeStartMs = annotationItem.timeStartMs !== undefined ? Number(annotationItem.timeStartMs) : -1
        annotationDialog.pendingTimeEndMs = annotationItem.timeEndMs !== undefined ? Number(annotationItem.timeEndMs) : -1
        annotationDialog.selectedColor = String(annotationItem.color || ((annotationItem.targetType === "text") ? noteColorDefault : pinColorDefault))
        annotationContentField.text = annotationItem.content || ""
        annotationDialog.open()
    }

    function removeAnnotation(annotationId) {
        if (fileListModel.deleteAnnotation(annotationId)) {
            refreshAnnotations()
            readerStatusMessage = "Annotation deleted"
        }
    }

    function createTextAnnotationFromSelection() {
        if (!isTextFile())
            return
        if (!hasCachedTextSelection())
            return

        openNewAnnotationDialog(
            "text",
            "text-highlight",
            -1,
            textSelectionStartCached,
            textSelectionEndCached,
            textSelectionAnchorCached,
            0, 0, 0, 0,
            -1, -1
        )
    }

    function annotationsForPage(pageNumber) {
        const list = []
        for (let i = 0; i < visualAnnotations.length; ++i) {
            const item = visualAnnotations[i]
            const itemPage = item.pageNumber !== undefined ? Number(item.pageNumber) : -1
            if (itemPage === pageNumber)
                list.push(item)
        }
        return list
    }

    function focusTextAnnotation(annotationItem) {
        if (!isTextFile())
            return
        const start = annotationItem.charStart !== undefined ? Number(annotationItem.charStart) : -1
        const end = annotationItem.charEnd !== undefined ? Number(annotationItem.charEnd) : -1
        if (start < 0 || end <= start)
            return

        if (!textContentRef || !textFlickRef)
            return

        textContentRef.select(start, end)
        updateCachedTextSelection()
        const rect = textContentRef.positionToRectangle(start)
        textFlickRef.contentX = Math.max(0, rect.x - 32)
        textFlickRef.contentY = Math.max(0, rect.y - 32)
    }

    function annotationShortLabel(annotationItem) {
        const type = annotationItem.annotationType || "note"
        const readableType = type === "text-highlight" ? "Text highlight"
                           : (type === "pin-note" ? "Pin note"
                              : (type === "rect-note" ? "Rect note"
                                 : (type === "area-highlight" ? "Area highlight"
                                    : (type === "time-note" ? "Timestamp note"
                                       : (type === "time-range" ? "Range note" : type)))))
        if (annotationItem.targetType === "text")
            return readableType + " | chars " + annotationItem.charStart + "-" + annotationItem.charEnd
        if (annotationItem.targetType === "pdf" || annotationItem.targetType === "presentation")
            return readableType + " | page " + (Number(annotationItem.pageNumber) + 1)
        if (annotationItem.targetType === "video" || annotationItem.targetType === "audio") {
            const startMs = annotationItem.timeStartMs !== undefined ? Number(annotationItem.timeStartMs) : -1
            const endMs = annotationItem.timeEndMs !== undefined ? Number(annotationItem.timeEndMs) : -1
            if (startMs >= 0 && endMs > startMs)
                return readableType + " | " + formatDurationMs(startMs) + " - " + formatDurationMs(endMs)
            if (startMs >= 0)
                return readableType + " | " + formatDurationMs(startMs)
        }
        return readableType
    }

    function filteredAnnotationItems() {
        const baseList = isTextFile() ? textAnnotations : visualAnnotations
        const query = String(annotationSearchText || "").trim().toLowerCase()
        if (query === "")
            return baseList

        const out = []
        for (let i = 0; i < baseList.length; ++i) {
            const item = baseList[i]
            const haystack = [
                annotationShortLabel(item),
                item.content || "",
                item.anchorText || "",
                item.updatedAt || "",
                item.createdAt || ""
            ].join(" ").toLowerCase()
            if (haystack.indexOf(query) >= 0)
                out.push(item)
        }
        return out
    }

    function annotationTargetContextText(targetType,
                                         annotationType,
                                         pageNumber,
                                         charStart,
                                         charEnd,
                                         timeStartMs,
                                         timeEndMs) {
        const normalizedTarget = String(targetType || "").toLowerCase()
        if (normalizedTarget === "text" && Number(charStart) >= 0 && Number(charEnd) > Number(charStart)) {
            return "Target: [Text selection chars " + Number(charStart) + "-" + Number(charEnd) + "]"
        }
        if ((normalizedTarget === "pdf" || normalizedTarget === "presentation") && Number(pageNumber) >= 0) {
            return "Target: [Page " + (Number(pageNumber) + 1) + "]"
        }
        if (normalizedTarget === "image") {
            return "Target: [Annotated region on image]"
        }
        if (normalizedTarget === "video" || normalizedTarget === "audio") {
            const startMs = Number(timeStartMs)
            const endMs = Number(timeEndMs)
            if (String(annotationType || "") === "time-range" && endMs > startMs && startMs >= 0) {
                return "Target: [Media range " + formatDurationMs(startMs) + " - " + formatDurationMs(endMs) + "]"
            }
            if (startMs >= 0) {
                return "Target: [Media time " + formatDurationMs(startMs) + "]"
            }
        }
        return "Target: [Context selected]"
    }

    function hasPdfPreviewUnavailableState() {
        if (!(isPdfFile() || isPresentationFile()))
            return false
        if (String(resolvedPdfPreviewUrl || "").trim() === "")
            return true
        return pdfDocument.status === PdfDocument.Error
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

    Component.onCompleted: {
        pdfPreviewErrorText = ""
        if (isTextFile())
            textPreview = fileListModel.readTextFile(fileIndex)
        if (isPdfFile()) {
            resolvedPdfPreviewUrl = fileListModel.fileUrl(fileIndex)
            if (String(resolvedPdfPreviewUrl || "").trim() === "")
                pdfPreviewErrorText = "PDF preview unavailable. Source file is missing. Use Open Externally."
        } else if (isPresentationFile()) {
            const presentationPreview = fileListModel.presentationPdfPreview(fileIndex)
            resolvedPdfPreviewUrl = presentationPreview.url || ""
            if (resolvedPdfPreviewUrl === "") {
                pdfPreviewErrorText = presentationPreview.error && presentationPreview.error !== ""
                                    ? presentationPreview.error
                                    : "Presentation conversion to PDF failed"
                readerStatusMessage = presentationPreview.error && presentationPreview.error !== ""
                        ? ("Presentation preview unavailable. " + presentationPreview.error + " Use Open Externally.")
                        : "Presentation preview unavailable. Use Open Externally."
            }
        } else
            resolvedPdfPreviewUrl = ""

        mediaRangeStartMs = -1
        mediaControllerRef = null
        ensureMediaPreviewComponent()
        clearCachedTextSelection()
        refreshAnnotations()
        refreshServiceStatus()
        pdfCurrentPage = 0
    }

    Connections {
        target: fileListModel
        function onIndexStatusChanged() { refreshServiceStatus() }
        function onImportStatusChanged() { refreshServiceStatus() }
    }

    Connections {
        target: cloudSyncModel
        ignoreUnknownSignals: true
        function onStatusChanged() { refreshServiceStatus() }
    }

    background: Rectangle {
        color: bgColor
    }

    PdfDocument {
        id: pdfDocument
        source: (isPdfFile() || isPresentationFile()) ? resolvedPdfPreviewUrl : ""
    }

    Dialog {
        id: documentNoteDialog
        title: editingNoteId >= 0 ? "Edit Document Note" : "Add Document Note"
        modal: true
        width: 560
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        property int editingNoteId: -1
        property bool showValidation: false

        onOpened: showValidation = false
        onClosed: showValidation = false

        background: Rectangle {
            color: panelColor
            radius: 12
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: "Title (optional)"
                color: textPrimary
                font.bold: true
            }

            AppTextField {
                id: noteTitleField
                Layout.fillWidth: true
                placeholderText: "Optional text input"
            }

            Label {
                text: "Body (required) *"
                color: textPrimary
                font.bold: true
            }

            TextArea {
                id: noteBodyField
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                wrapMode: TextEdit.Wrap
                placeholderText: "Required text area for note body"
                color: textPrimary
                font.family: theme.fontFamily
                font.pixelSize: 13
                background: Rectangle {
                    radius: 8
                    color: panelColor
                    border.color: (documentNoteDialog.showValidation && noteBodyField.text.trim() === "")
                                  ? dangerBorder
                                  : (noteBodyField.activeFocus ? accentBorder : borderColor)
                }
            }

            Label {
                visible: documentNoteDialog.showValidation && noteBodyField.text.trim() === ""
                text: "Body is required."
                color: dangerText
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    Layout.preferredWidth: 112
                    fillColor: panelSoft
                    borderLine: borderColor
                    onClicked: documentNoteDialog.close()
                }

                AppButton {
                    text: editingNoteId >= 0 ? "Save Changes" : "Add Note"
                    Layout.preferredWidth: 132
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    enabled: noteBodyField.text.trim() !== ""
                    onClicked: {
                        documentNoteDialog.showValidation = true
                        if (noteBodyField.text.trim() === "")
                            return

                        let ok = false
                        if (documentNoteDialog.editingNoteId >= 0) {
                            ok = fileListModel.updateDocumentNote(
                                        documentNoteDialog.editingNoteId,
                                        noteTitleField.text.trim(),
                                        noteBodyField.text.trim())
                        } else {
                            ok = fileListModel.addDocumentNote(
                                        fileId,
                                        noteTitleField.text.trim(),
                                        noteBodyField.text.trim())
                        }

                        if (ok) {
                            documentNoteDialog.close()
                            refreshAnnotations()
                            readerStatusMessage = documentNoteDialog.editingNoteId >= 0
                                    ? "Document note updated"
                                    : "Document note added"
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: annotationDialog
        title: editingAnnotationId >= 0 ? "Edit Annotation" : "Add Annotation"
        modal: true
        width: 620
        anchors.centerIn: Overlay.overlay
        padding: 18
        Overlay.modal: Rectangle { color: "#660f172a" }

        property int editingAnnotationId: -1
        property string pendingTargetType: ""
        property string pendingAnnotationType: ""
        property int pendingPageNumber: -1
        property int pendingCharStart: -1
        property int pendingCharEnd: -1
        property string pendingAnchorText: ""
        property real pendingX: 0
        property real pendingY: 0
        property real pendingWidth: 0
        property real pendingHeight: 0
        property int pendingTimeStartMs: -1
        property int pendingTimeEndMs: -1
        property string selectedColor: String(noteColorDefault)

        background: Rectangle {
            color: panelColor
            radius: 12
            border.color: borderColor
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: "Contextual helper text acts the contextual helper"
                color: textSecondary
                font.pixelSize: 12
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: panelSoft
                border.color: borderColor
                implicitHeight: contextTargetLabel.implicitHeight + 10

                Label {
                    id: contextTargetLabel
                    anchors.fill: parent
                    anchors.margins: 6
                    text: annotationTargetContextText(
                              annotationDialog.pendingTargetType,
                              annotationDialog.pendingAnnotationType,
                              annotationDialog.pendingPageNumber,
                              annotationDialog.pendingCharStart,
                              annotationDialog.pendingCharEnd,
                              annotationDialog.pendingTimeStartMs,
                              annotationDialog.pendingTimeEndMs)
                    color: textPrimary
                    wrapMode: Text.Wrap
                }
            }

            Label {
                text: "Highlighter Color"
                color: textPrimary
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: ["#fef1f1", "#fbe3de", "#fce7c6", "#f8edc9", "#d8f3dc", "#bce6d2", "#a8dcd1", "#bde0fe", "#b7c6f1", "#cdb4db"]

                    delegate: Rectangle {
                        width: 30
                        height: 24
                        radius: 6
                        color: modelData
                        border.color: String(annotationDialog.selectedColor).trim().toLowerCase() === String(modelData).toLowerCase()
                                      ? textPrimary
                                      : borderColor
                        border.width: String(annotationDialog.selectedColor).trim().toLowerCase() === String(modelData).toLowerCase() ? 2 : 1

                        MouseArea {
                            anchors.fill: parent
                            onClicked: annotationDialog.selectedColor = modelData
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: panelSoft
                border.color: borderColor
                implicitHeight: 34

                Label {
                    anchors.centerIn: parent
                    text: "Selected color: " + String(annotationDialog.selectedColor)
                    color: textSecondary
                    font.pixelSize: 12
                }
            }

            Label {
                text: "Note"
                color: textPrimary
                font.bold: true
            }

            TextArea {
                id: annotationContentField
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                wrapMode: TextEdit.Wrap
                placeholderText: "Note text area"
                color: textPrimary
                font.family: theme.fontFamily
                font.pixelSize: 13
                background: Rectangle {
                    radius: 8
                    color: panelColor
                    border.color: annotationContentField.activeFocus ? accentBorder : borderColor
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Cancel"
                    Layout.preferredWidth: 112
                    fillColor: panelSoft
                    borderLine: borderColor
                    onClicked: annotationDialog.close()
                }

                AppButton {
                    text: annotationDialog.editingAnnotationId >= 0 ? "Save Changes" : "Add Annotation"
                    Layout.preferredWidth: 132
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    onClicked: {
                        let ok = false
                        if (annotationDialog.editingAnnotationId >= 0) {
                            ok = fileListModel.updateAnnotation(
                                        annotationDialog.editingAnnotationId,
                                        String(annotationDialog.selectedColor).trim(),
                                        annotationContentField.text.trim())
                        } else {
                            ok = fileListModel.addAnnotation(
                                        fileId,
                                        annotationDialog.pendingTargetType,
                                        annotationDialog.pendingAnnotationType,
                                        annotationDialog.pendingPageNumber,
                                        annotationDialog.pendingCharStart,
                                        annotationDialog.pendingCharEnd,
                                        annotationDialog.pendingAnchorText,
                                        annotationDialog.pendingX,
                                        annotationDialog.pendingY,
                                        annotationDialog.pendingWidth,
                                        annotationDialog.pendingHeight,
                                        String(annotationDialog.selectedColor).trim(),
                                        annotationContentField.text.trim(),
                                        annotationDialog.pendingTimeStartMs,
                                        annotationDialog.pendingTimeEndMs)
                        }

                        if (ok) {
                            annotationDialog.close()
                            clearAnnotationTool()
                            refreshAnnotations()
                            if (annotationDialog.pendingTargetType === "text" && textContentRef) {
                                textContentRef.deselect()
                                clearCachedTextSelection()
                            }
                            readerStatusMessage = annotationDialog.editingAnnotationId >= 0
                                    ? "Annotation updated"
                                    : "Annotation added"
                        }
                    }
                }
            }
        }
    }

    header: Rectangle {
        color: panelColor
        border.color: borderColor
        implicitHeight: readerHeaderLayout.implicitHeight + 22

        RowLayout {
            id: readerHeaderLayout
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            AppButton {
                text: "\u2039  Back"
                fillColor: panelSoft
                borderLine: borderColor
                labelColor: textPrimary
                implicitHeight: 36
                radiusValue: 8
                onClicked: {
                    if (navigationStack)
                        navigationStack.pop()
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: borderColor
                opacity: 0.8
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Label {
                    text: "READER MODE"
                    color: textSecondary
                    font.bold: true
                    font.pixelSize: 12
                    font.letterSpacing: 0.3
                }

                Label {
                    text: "Memory: [" + (fileDetails && fileDetails.name ? fileDetails.name : "Untitled") + "]"
                    color: textPrimary
                    font.bold: true
                    font.pixelSize: 34
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label {
                    text: "Source: " + (fileDetails && fileDetails.source ? fileDetails.source : "Unknown Source")
                    color: textSecondary
                    font.pixelSize: 18
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
            }

            Rectangle {
                radius: 10
                color: panelSoft
                border.color: borderColor
                visible: isImageFile() || isTextFile() || isPdfFile() || isPresentationFile()
                implicitHeight: zoomBar.implicitHeight + 10
                implicitWidth: zoomBar.implicitWidth + 12

                RowLayout {
                    id: zoomBar
                    anchors.centerIn: parent
                    spacing: 5

                    AppButton {
                        text: "-"
                        implicitHeight: 30
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        radiusValue: 6
                        onClicked: zoomOut()
                    }

                    AppButton {
                        text: "+"
                        implicitHeight: 30
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        radiusValue: 6
                        onClicked: zoomIn()
                    }

                    AppButton {
                        text: "%"
                        implicitHeight: 30
                        Layout.preferredWidth: 30
                        leftPadding: 0
                        rightPadding: 0
                        radiusValue: 6
                        fillColor: panelColor
                        borderLine: borderColor
                    }

                    Rectangle {
                        radius: 6
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: 30
                        implicitWidth: Math.max(52, zoomPercentLabel.implicitWidth + 10)

                        Label {
                            id: zoomPercentLabel
                            anchors.centerIn: parent
                            text: currentZoomText()
                            color: textPrimary
                            font.bold: true
                            font.pixelSize: 12
                        }
                    }

                    AppButton {
                        text: "Reset"
                        implicitHeight: 30
                        radiusValue: 6
                        onClicked: resetZoom()
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: borderColor
                opacity: 0.8
            }

            RowLayout {
                spacing: 6

                AppButton {
                    text: "Open Original"
                    implicitHeight: 36
                    fillColor: panelSoft
                    borderLine: borderColor
                    onClicked: fileListModel.openFile(fileIndex)
                }

                AppButton {
                    text: "Open Folder"
                    implicitHeight: 36
                    fillColor: panelSoft
                    borderLine: borderColor
                    onClicked: fileListModel.openContainingFolder(fileIndex)
                }

                AppButton {
                    text: "Remove File"
                    implicitHeight: 36
                    fillColor: dangerBg
                    borderLine: dangerBorder
                    labelColor: dangerText
                    onClicked: {
                        const ok = fileListModel.removeFile(fileIndex)
                        if (ok) {
                            if (navigationStack)
                                navigationStack.pop()
                        } else {
                            readerStatusMessage = "Unable to remove file from ELLA"
                        }
                    }
                }
            }

            Rectangle {
                visible: toolingWarningMessage !== ""
                radius: 10
                color: warningBg
                border.color: warningBorder
                implicitHeight: toolingWarningLabel.implicitHeight + 10
                implicitWidth: Math.min(360, toolingWarningLabel.implicitWidth + 16)

                Label {
                    id: toolingWarningLabel
                    anchors.centerIn: parent
                    text: "Tooling warning: " + toolingWarningMessage
                    color: warningText
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    width: Math.min(344, implicitWidth)
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Rectangle {
                radius: 10
                color: syncChipBg()
                border.color: syncChipBorder()
                implicitHeight: 36
                implicitWidth: syncStateLabel.implicitWidth + 18

                Label {
                    id: syncStateLabel
                    anchors.centerIn: parent
                    text: syncChipText()
                    color: syncChipTextColor()
                    font.bold: true
                    font.pixelSize: 12
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: panelColor
            border.color: borderColor

            Loader {
                anchors.fill: parent
                anchors.margins: 12
                sourceComponent: isImageFile()
                                 ? imagePreviewComponent
                                 : (isTextFile()
                                    ? textPreviewComponent
                                    : ((isPdfFile() || isPresentationFile())
                                       ? pdfPreviewComponent
                                       : (isVideoFile()
                                          ? mediaPreviewComponent
                                          : (isAudioFile()
                                             ? mediaPreviewComponent
                                             : unsupportedPreviewComponent))))
            }
        }

        Rectangle {
            Layout.preferredWidth: 390
            Layout.fillHeight: true
            radius: 16
            color: panelColor
            border.color: borderColor

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10
                clip: true

                ColumnLayout {
                    width: 390 - 22
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: annotationToolsColumn.implicitHeight + 16

                        ColumnLayout {
                            id: annotationToolsColumn
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            Label {
                                text: "Annotation Tools"
                                color: textPrimary
                                font.bold: true
                                font.pixelSize: 18
                            }

                            AppButton {
                                visible: isTextFile()
                                text: hasCachedTextSelection()
                                      ? "Add note from selected text"
                                      : "Select text to enable notes"
                                fillColor: accentColor
                                borderLine: accentColor
                                labelColor: "#ffffff"
                                enabled: hasCachedTextSelection()
                                onClicked: createTextAnnotationFromSelection()
                            }

                            Flow {
                                visible: isImageFile()
                                width: parent.width
                                spacing: 8

                                AppButton {
                                    text: annotationTool === "pin" ? "Add Pin \u2713" : "Add Pin"
                                    fillColor: annotationTool === "pin" ? accentSoft : panelSoft
                                    borderLine: annotationTool === "pin" ? accentBorder : borderColor
                                    labelColor: annotationTool === "pin" ? accentColor : textPrimary
                                    onClicked: annotationTool = annotationTool === "pin" ? "" : "pin"
                                }

                                AppButton {
                                    text: annotationTool === "rect" ? "Draw Rect \u2713" : "Draw Rect"
                                    fillColor: annotationTool === "rect" ? accentSoft : panelSoft
                                    borderLine: annotationTool === "rect" ? accentBorder : borderColor
                                    labelColor: annotationTool === "rect" ? accentColor : textPrimary
                                    onClicked: annotationTool = annotationTool === "rect" ? "" : "rect"
                                }

                                AppButton {
                                    text: "Cancel"
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    enabled: annotationTool !== ""
                                    onClicked: clearAnnotationTool()
                                }
                            }

                            Label {
                                visible: isPdfFile() || isPresentationFile()
                                text: "Use the in-canvas toolbar to add pin/rect annotations."
                                color: textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: isMediaFile()
                                text: isMediaPlaybackReady()
                                      ? "Create timestamp and range notes directly from playback."
                                      : "Playback is not ready yet."
                                color: textSecondary
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Flow {
                                visible: isMediaFile()
                                width: parent.width
                                spacing: 8

                                AppButton {
                                    text: "Add Timestamp Note"
                                    fillColor: accentColor
                                    borderLine: accentColor
                                    labelColor: "#ffffff"
                                    enabled: isMediaPlaybackReady()
                                    onClicked: addMediaTimestampAnnotation()
                                }

                                AppButton {
                                    text: mediaRangeStartMs >= 0
                                          ? "Start " + formatDurationMs(mediaRangeStartMs)
                                          : "Mark Range Start"
                                    fillColor: mediaRangeStartMs >= 0 ? accentSoft : panelSoft
                                    borderLine: mediaRangeStartMs >= 0 ? accentBorder : borderColor
                                    labelColor: mediaRangeStartMs >= 0 ? accentColor : textPrimary
                                    enabled: isMediaPlaybackReady()
                                    onClicked: markMediaRangeStart()
                                }

                                AppButton {
                                    text: "Save Range Note"
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    enabled: isMediaPlaybackReady()
                                             && mediaRangeStartMs >= 0
                                             && currentMediaPositionMs() > mediaRangeStartMs
                                    onClicked: createMediaRangeAnnotationToCurrent()
                                }

                                AppButton {
                                    text: "Clear Range"
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    enabled: mediaRangeStartMs >= 0
                                    onClicked: mediaRangeStartMs = -1
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: annotationListColumn.implicitHeight + 16

                        ColumnLayout {
                            id: annotationListColumn
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            Label {
                                text: "Annotations (" + filteredAnnotationItems().length + ")"
                                color: textPrimary
                                font.bold: true
                                font.pixelSize: 18
                            }

                            AppTextField {
                                Layout.fillWidth: true
                                placeholderText: "Search annotations"
                                text: annotationSearchText
                                onTextChanged: annotationSearchText = text
                            }

                            Repeater {
                                model: filteredAnnotationItems()

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    width: parent.width
                                    radius: 10
                                    color: panelSoft
                                    border.color: borderColor
                                    implicitHeight: annotationCardColumn.implicitHeight + 14

                                    ColumnLayout {
                                        id: annotationCardColumn
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 4

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Label {
                                                text: annotationShortLabel(modelData)
                                                color: textPrimary
                                                font.bold: true
                                                Layout.fillWidth: true
                                                wrapMode: Text.Wrap
                                            }

                                            Label {
                                                text: formatDateTimeShort(modelData.updatedAt || modelData.createdAt)
                                                color: textSecondary
                                                font.pixelSize: 11
                                            }

                                            Rectangle {
                                                width: 16
                                                height: 16
                                                radius: 5
                                                color: modelData.color && modelData.color !== "" ? modelData.color : noteColorDefault
                                                border.color: borderColor
                                            }
                                        }

                                        Label {
                                            text: modelData.content && modelData.content !== "" ? modelData.content : "No note text"
                                            color: textSecondary
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true

                                            AppButton {
                                                visible: isTextFile()
                                                text: "Focus"
                                                implicitHeight: 30
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: focusTextAnnotation(modelData)
                                            }

                                            AppButton {
                                                visible: isMediaFile() && (modelData.targetType === "video" || modelData.targetType === "audio")
                                                text: "Jump"
                                                implicitHeight: 30
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                enabled: isMediaPlaybackReady()
                                                onClicked: jumpToMediaAnnotation(modelData)
                                            }

                                            Item { Layout.fillWidth: true }

                                            AppButton {
                                                text: "Edit"
                                                implicitHeight: 30
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: openEditAnnotationDialog(modelData)
                                            }

                                            AppButton {
                                                text: "\uE74D"
                                                implicitHeight: 30
                                                Layout.preferredWidth: 34
                                                leftPadding: 0
                                                rightPadding: 0
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                font.family: "Segoe MDL2 Assets"
                                                font.pixelSize: 12
                                                onClicked: removeAnnotation(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: filteredAnnotationItems().length === 0
                                text: annotationSearchText.trim() === "" ? "No annotations yet." : "No annotations match your search."
                                color: textSecondary
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: fileInfoSection.implicitHeight + 16

                        ColumnLayout {
                            id: fileInfoSection
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "File Information"
                                    color: textPrimary
                                    font.bold: true
                                    font.pixelSize: 18
                                }

                                Item { Layout.fillWidth: true }

                                AppButton {
                                    text: readerFileInfoExpanded ? "\u25B4" : "\u25BE"
                                    implicitHeight: 28
                                    Layout.preferredWidth: 30
                                    leftPadding: 0
                                    rightPadding: 0
                                    radiusValue: 6
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    onClicked: readerFileInfoExpanded = !readerFileInfoExpanded
                                }
                            }

                            GridLayout {
                                visible: readerFileInfoExpanded
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 6

                                Label { text: "Type"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: (fileDetails && fileDetails.extension ? String(fileDetails.extension).toUpperCase() : "-") + (fileDetails && fileDetails.mimeType ? " / " + fileDetails.mimeType : ""); color: textPrimary; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }

                                Label { text: "Size"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: formatFileSize(fileDetails && fileDetails.sizeBytes ? fileDetails.sizeBytes : 0); color: textPrimary }

                                Label { text: "Pages"; color: textSecondary; font.pixelSize: 12; visible: isPdfFile() || isPresentationFile() }
                                Label { text: pdfDocument.status === PdfDocument.Ready ? String(pdfDocument.pageCount) : "-"; color: textPrimary; visible: isPdfFile() || isPresentationFile() }

                                Label { text: "Indexed Date"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: formatDateShort(fileDetails && fileDetails.indexedAt ? fileDetails.indexedAt : ""); color: textPrimary }

                                Label { text: "Path"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.path ? fileDetails.path : "-"; color: textPrimary; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: metadataSection.implicitHeight + 16

                        ColumnLayout {
                            id: metadataSection
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "Metadata"
                                    color: textPrimary
                                    font.bold: true
                                    font.pixelSize: 18
                                }

                                Item { Layout.fillWidth: true }

                                AppButton {
                                    text: readerMetadataExpanded ? "\u25B4" : "\u25BE"
                                    implicitHeight: 28
                                    Layout.preferredWidth: 30
                                    leftPadding: 0
                                    rightPadding: 0
                                    radiusValue: 6
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    onClicked: readerMetadataExpanded = !readerMetadataExpanded
                                }
                            }

                            GridLayout {
                                visible: readerMetadataExpanded
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 6

                                Label { text: "Technical Domain"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.technicalDomain ? fileDetails.technicalDomain : "-"; color: textPrimary; wrapMode: Text.Wrap; Layout.fillWidth: true }

                                Label { text: "Subject"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.subject ? fileDetails.subject : "-"; color: textPrimary; wrapMode: Text.Wrap; Layout.fillWidth: true }

                                Label { text: "Subtopic"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.subtopic ? fileDetails.subtopic : "-"; color: textPrimary; wrapMode: Text.Wrap; Layout.fillWidth: true }

                                Label { text: "Author"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.author ? fileDetails.author : "-"; color: textPrimary; wrapMode: Text.Wrap; Layout.fillWidth: true }

                                Label { text: "Date Created"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: formatDateShort(fileDetails && fileDetails.createdAt ? fileDetails.createdAt : ""); color: textPrimary }

                                Label { text: "Location"; color: textSecondary; font.pixelSize: 12 }
                                Label { text: fileDetails && fileDetails.location ? fileDetails.location : "-"; color: textPrimary; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        color: panelColor
                        border.color: borderColor
                        implicitHeight: documentNotesSection.implicitHeight + 16

                        ColumnLayout {
                            id: documentNotesSection
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "Document Notes"
                                    color: textPrimary
                                    font.bold: true
                                    font.pixelSize: 18
                                }

                                Item { Layout.fillWidth: true }

                                AppButton {
                                    text: readerNotesExpanded ? "\u25B4" : "\u25BE"
                                    implicitHeight: 28
                                    Layout.preferredWidth: 30
                                    leftPadding: 0
                                    rightPadding: 0
                                    radiusValue: 6
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    onClicked: readerNotesExpanded = !readerNotesExpanded
                                }
                            }

                            RowLayout {
                                visible: readerNotesExpanded
                                Layout.fillWidth: true
                                spacing: 6

                                AppTextField {
                                    id: quickNoteField
                                    Layout.fillWidth: true
                                    placeholderText: "Add a text"
                                }

                                AppButton {
                                    text: "Add"
                                    fillColor: accentColor
                                    borderLine: accentColor
                                    labelColor: "#ffffff"
                                    enabled: quickNoteField.text.trim() !== ""
                                    onClicked: {
                                        const ok = fileListModel.addDocumentNote(fileId, "", quickNoteField.text.trim())
                                        if (ok) {
                                            quickNoteField.text = ""
                                            refreshAnnotations()
                                            readerStatusMessage = "Document note added"
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: readerNotesExpanded ? documentNotes : []

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    width: parent.width
                                    radius: 10
                                    color: panelSoft
                                    border.color: borderColor
                                    implicitHeight: noteCardColumn.implicitHeight + 14

                                    ColumnLayout {
                                        id: noteCardColumn
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 6

                                        Label {
                                            text: modelData.title && modelData.title !== "" ? modelData.title : "General document notes"
                                            color: textPrimary
                                            font.bold: true
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                        }

                                        Label {
                                            text: modelData.body
                                            color: textSecondary
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Label {
                                                text: formatDateTimeShort(modelData.updatedAt || modelData.createdAt)
                                                color: textSecondary
                                                font.pixelSize: 11
                                            }

                                            Item { Layout.fillWidth: true }

                                            AppButton {
                                                text: "Edit"
                                                implicitHeight: 30
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: openEditDocumentNoteDialog(modelData)
                                            }

                                            AppButton {
                                                text: "Delete"
                                                implicitHeight: 30
                                                radiusValue: 6
                                                fillColor: panelColor
                                                borderLine: borderColor
                                                onClicked: removeDocumentNote(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: readerNotesExpanded && documentNotes.length === 0
                                text: "No document notes yet."
                                color: textSecondary
                            }
                        }
                    }

                    Label {
                        visible: readerStatusMessage !== ""
                        text: readerStatusMessage
                        color: textSecondary
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    Component {
        id: imagePreviewComponent

        Rectangle {
            color: panelSoft
            radius: 16
            border.color: borderColor
            clip: true

            Flickable {
                id: imageFlick
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.AutoFlickDirection
                interactive: annotationTool === ""
                contentWidth: Math.max(width, imageCanvas.width + 24)
                contentHeight: Math.max(height, imageCanvas.height + 24)

                ScrollBar.vertical: ScrollBar { }
                ScrollBar.horizontal: ScrollBar { }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        if (event.modifiers & Qt.ControlModifier) {
                            const factor = event.angleDelta.y > 0 ? 1.1 : 0.9
                            imageZoom = clampZoom(imageZoom * factor)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                }

                Item {
                    id: imageCanvas
                    x: Math.max(12, (imageFlick.width - width) / 2)
                    y: Math.max(12, (imageFlick.height - height) / 2)
                    width: previewImage.status === Image.Ready ? previewImage.implicitWidth * imageZoom : Math.max(imageFlick.width - 24, 320)
                    height: previewImage.status === Image.Ready ? previewImage.implicitHeight * imageZoom : 240

                    Image {
                        id: previewImage
                        anchors.fill: parent
                        source: fileListModel.fileUrl(fileIndex)
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit
                    }

                    Item {
                        id: imageOverlay
                        anchors.fill: parent

                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 10
                            radius: 10
                            color: panelColor
                            border.color: borderColor
                            opacity: 0.96
                            z: 40
                            implicitHeight: imageToolbar.implicitHeight + 10
                            implicitWidth: imageToolbar.implicitWidth + 10

                            RowLayout {
                                id: imageToolbar
                                anchors.centerIn: parent
                                spacing: 6

                                AppButton {
                                    text: annotationTool === "pin" ? "Add Pin \u2713" : "Add Pin"
                                    implicitHeight: 30
                                    radiusValue: 6
                                    fillColor: annotationTool === "pin" ? accentSoft : panelSoft
                                    borderLine: annotationTool === "pin" ? accentBorder : borderColor
                                    labelColor: annotationTool === "pin" ? accentColor : textPrimary
                                    onClicked: annotationTool = annotationTool === "pin" ? "" : "pin"
                                }

                                AppButton {
                                    text: annotationTool === "rect" ? "Draw Rect \u2713" : "Draw Rect"
                                    implicitHeight: 30
                                    radiusValue: 6
                                    fillColor: annotationTool === "rect" ? accentSoft : panelSoft
                                    borderLine: annotationTool === "rect" ? accentBorder : borderColor
                                    labelColor: annotationTool === "rect" ? accentColor : textPrimary
                                    onClicked: annotationTool = annotationTool === "rect" ? "" : "rect"
                                }

                                AppButton {
                                    text: "Cancel"
                                    implicitHeight: 30
                                    radiusValue: 6
                                    fillColor: panelSoft
                                    borderLine: borderColor
                                    enabled: annotationTool !== ""
                                    onClicked: clearAnnotationTool()
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: annotationTool !== ""

                            property real startXNorm: 0
                            property real startYNorm: 0

                            onClicked: function(mouse) {
                                if (annotationTool !== "pin")
                                    return

                                const nx = mouse.x / width
                                const ny = mouse.y / height
                                            openNewAnnotationDialog("image", "pin-note", -1, -1, -1, "", nx, ny, 0, 0, -1, -1)
                            }

                            onPressed: function(mouse) {
                                if (annotationTool !== "rect")
                                    return

                                startXNorm = mouse.x / width
                                startYNorm = mouse.y / height
                                rectGuide.visible = true
                                rectGuide.x = mouse.x
                                rectGuide.y = mouse.y
                                rectGuide.width = 0
                                rectGuide.height = 0
                            }

                            onPositionChanged: function(mouse) {
                                if (annotationTool !== "rect" || !pressed)
                                    return

                                const left = Math.min(mouse.x, startXNorm * width)
                                const top = Math.min(mouse.y, startYNorm * height)
                                rectGuide.x = left
                                rectGuide.y = top
                                rectGuide.width = Math.abs(mouse.x - startXNorm * width)
                                rectGuide.height = Math.abs(mouse.y - startYNorm * height)
                            }

                            onReleased: function(mouse) {
                                if (annotationTool !== "rect")
                                    return

                                rectGuide.visible = false

                                const endXNorm = mouse.x / width
                                const endYNorm = mouse.y / height
                                const left = Math.min(startXNorm, endXNorm)
                                const top = Math.min(startYNorm, endYNorm)
                                const rectWidth = Math.abs(endXNorm - startXNorm)
                                const rectHeight = Math.abs(endYNorm - startYNorm)

                                if (rectWidth > 0.01 && rectHeight > 0.01) {
                                                openNewAnnotationDialog("image", "rect-note", -1, -1, -1, "", left, top, rectWidth, rectHeight, -1, -1)
                                }
                            }
                        }

                        Rectangle {
                            id: rectGuide
                            visible: false
                            color: "#330f766e"
                            border.color: accentColor
                            border.width: 2
                        }

                        Repeater {
                            model: visualAnnotations

                            delegate: Item {
                                x: Number(modelData.x) * imageOverlay.width
                                y: Number(modelData.y) * imageOverlay.height
                                width: isRectAnnotationType(modelData.annotationType) ? Number(modelData.width) * imageOverlay.width : 20
                                height: isRectAnnotationType(modelData.annotationType) ? Number(modelData.height) * imageOverlay.height : 20

                                Rectangle {
                                    anchors.fill: parent
                                    radius: isPinAnnotationType(modelData.annotationType) ? width / 2 : 6
                                    color: isPinAnnotationType(modelData.annotationType)
                                           ? (modelData.color && modelData.color !== "" ? modelData.color : pinColorDefault)
                                           : "transparent"
                                    border.color: modelData.color && modelData.color !== "" ? modelData.color : accentColor
                                    border.width: 2
                                    opacity: isPinAnnotationType(modelData.annotationType) ? 0.95 : 1.0
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        selectedVisualAnnotationId = Number(modelData.id)
                                        openEditAnnotationDialog(modelData)
                                    }
                                }

                                Rectangle {
                                    visible: selectedVisualAnnotationId === Number(modelData.id)
                                    anchors.top: parent.bottom
                                    anchors.topMargin: 8
                                    anchors.left: parent.left
                                    radius: 8
                                    color: panelColor
                                    border.color: borderColor
                                    z: 45
                                    implicitHeight: calloutText.implicitHeight + 10
                                    implicitWidth: Math.min(300, Math.max(120, calloutText.implicitWidth + 12))

                                    Label {
                                        id: calloutText
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        text: modelData.content && modelData.content !== "" ? modelData.content : "Annotation"
                                        color: textPrimary
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                Label {
                    visible: previewImage.status === Image.Error
                    anchors.centerIn: parent
                    text: "Unable to display image preview."
                    color: textSecondary
                }
            }
        }
    }

    Component {
        id: textPreviewComponent

        Rectangle {
            color: panelSoft
            radius: 16
            border.color: borderColor
            clip: true

            Flickable {
                id: textFlick
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, textDocumentCard.width + 48)
                contentHeight: Math.max(height, textDocumentCard.height + 48)

                ScrollBar.vertical: ScrollBar { }
                ScrollBar.horizontal: ScrollBar { }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        if (event.modifiers & Qt.ControlModifier) {
                            const factor = event.angleDelta.y > 0 ? 1.1 : 0.9
                            textZoom = clampZoom(textZoom * factor)
                            event.accepted = true
                        }
                    }
                }

                Rectangle {
                    id: textDocumentCard
                    x: Math.max(24, (textFlick.width - width) / 2)
                    y: 18
                    width: Math.max(760, textFlick.width - 120)
                    height: textContent.contentHeight + 56
                    radius: 12
                    color: panelColor
                    border.color: borderColor

                    TextEdit {
                        id: textContent
                        anchors.fill: parent
                        anchors.margins: 28
                        readOnly: true
                        selectByMouse: true
                        persistentSelection: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.RichText
                        text: renderedTextHtml
                        font.family: theme.fontFamily
                        font.pixelSize: Math.round(15 * textZoom)
                        color: textPrimary

                        selectionColor: accentBorder
                        selectedTextColor: textPrimary

                        Component.onCompleted: {
                            root.textContentRef = textContent
                            root.textFlickRef = textFlick
                            root.updateCachedTextSelection()
                        }

                        Component.onDestruction: {
                            if (root.textContentRef === textContent)
                                root.textContentRef = null
                            if (root.textFlickRef === textFlick)
                                root.textFlickRef = null
                            root.clearCachedTextSelection()
                        }

                        onSelectionStartChanged: root.updateCachedTextSelection()
                        onSelectionEndChanged: root.updateCachedTextSelection()
                        onSelectedTextChanged: root.updateCachedTextSelection()
                        onTextChanged: root.clearCachedTextSelection()
                    }
                }
            }
        }
    }

    Component {
        id: pdfPreviewComponent

        Rectangle {
            id: pdfFrame
            color: panelSoft
            radius: 16
            border.color: borderColor
            clip: true

            readonly property int pageCount: pdfDocument.status === PdfDocument.Ready ? pdfDocument.pageCount : 0

            Component.onCompleted: {
                if (pdfCurrentPage < 0)
                    pdfCurrentPage = 0
            }

            onPageCountChanged: {
                if (pageCount <= 0) {
                    pdfCurrentPage = 0
                    return
                }
                if (pdfCurrentPage >= pageCount)
                    pdfCurrentPage = pageCount - 1
                if (pdfCurrentPage < 0)
                    pdfCurrentPage = 0
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: "Page " + (pdfCurrentPage + 1) + " of " + Math.max(1, pdfFrame.pageCount)
                        color: textPrimary
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: "Prev"
                        implicitHeight: 30
                        radiusValue: 6
                        enabled: pdfCurrentPage > 0
                        onClicked: pdfCurrentPage = Math.max(0, pdfCurrentPage - 1)
                    }

                    AppButton {
                        text: "Next"
                        implicitHeight: 30
                        radiusValue: 6
                        enabled: pdfCurrentPage < pdfFrame.pageCount - 1
                        onClicked: pdfCurrentPage = Math.min(pdfFrame.pageCount - 1, pdfCurrentPage + 1)
                    }
                }

                Item {
                    id: pdfCanvasHost
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Flickable {
                        id: pdfFlick
                        anchors.fill: parent
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.AutoFlickDirection
                        interactive: annotationTool === ""
                        contentWidth: Math.max(width, pdfPageCard.visible ? pdfPageCard.width + 24 : width)
                        contentHeight: Math.max(height, pdfPageCard.visible ? pdfPageCard.height + 24 : height)

                        ScrollBar.vertical: ScrollBar { }
                        ScrollBar.horizontal: ScrollBar { }

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: function(event) {
                                if (event.modifiers & Qt.ControlModifier) {
                                    const factor = event.angleDelta.y > 0 ? 1.1 : 0.9
                                    pdfZoomFactor = clampZoom(pdfZoomFactor * factor)
                                    event.accepted = true
                                } else {
                                    event.accepted = false
                                }
                            }
                        }

                        Rectangle {
                            id: pdfPageCard
                            visible: pdfFrame.pageCount > 0 && pdfDocument.status === PdfDocument.Ready
                            x: Math.max(12, (pdfFlick.width - width) / 2)
                            y: Math.max(12, (pdfFlick.height - height) / 2)
                            property size pageSize: pdfDocument.pagePointSize(pdfCurrentPage)
                            property real aspectRatio: pageSize.width > 0 ? pageSize.height / pageSize.width : 1.414
                            property real fitWidth: {
                                const maxW = Math.max(320, pdfFlick.width - 24)
                                const maxH = Math.max(320, pdfFlick.height - 24)
                                if (aspectRatio <= 0)
                                    return maxW
                                return Math.min(maxW, maxH / aspectRatio)
                            }
                            width: Math.max(300, fitWidth * pdfZoomFactor)
                            height: Math.max(380, width * aspectRatio)
                            radius: 10
                            color: panelColor
                            border.color: borderColor

                            PdfPageImage {
                                anchors.fill: parent
                                document: pdfDocument
                                currentFrame: pdfCurrentPage
                                asynchronous: true
                                cache: false
                                fillMode: Image.Stretch
                                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                            }

                            Item {
                                id: currentPageOverlay
                                anchors.fill: parent

                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: 10
                                    radius: 9
                                    color: panelColor
                                    border.color: borderColor
                                    opacity: 0.96
                                    z: 30
                                    implicitHeight: pdfToolbar.implicitHeight + 8
                                    implicitWidth: pdfToolbar.implicitWidth + 8

                                    RowLayout {
                                        id: pdfToolbar
                                        anchors.centerIn: parent
                                        spacing: 6

                                        AppButton {
                                            text: annotationTool === "pin" ? "Add Pin \u2713" : "Add Pin"
                                            implicitHeight: 30
                                            radiusValue: 6
                                            fillColor: annotationTool === "pin" ? accentSoft : panelSoft
                                            borderLine: annotationTool === "pin" ? accentBorder : borderColor
                                            labelColor: annotationTool === "pin" ? accentColor : textPrimary
                                            onClicked: annotationTool = annotationTool === "pin" ? "" : "pin"
                                        }

                                        AppButton {
                                            text: annotationTool === "rect" ? "Draw Rect \u2713" : "Draw Rect"
                                            implicitHeight: 30
                                            radiusValue: 6
                                            fillColor: annotationTool === "rect" ? accentSoft : panelSoft
                                            borderLine: annotationTool === "rect" ? accentBorder : borderColor
                                            labelColor: annotationTool === "rect" ? accentColor : textPrimary
                                            onClicked: annotationTool = annotationTool === "rect" ? "" : "rect"
                                        }

                                        AppButton {
                                            text: "Cancel"
                                            implicitHeight: 30
                                            radiusValue: 6
                                            fillColor: panelSoft
                                            borderLine: borderColor
                                            enabled: annotationTool !== ""
                                            onClicked: clearAnnotationTool()
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: annotationTool !== ""

                                    property real startXNorm: 0
                                    property real startYNorm: 0

                                    onClicked: function(mouse) {
                                        if (annotationTool !== "pin")
                                            return
                                        openNewAnnotationDialog(
                                                    activeTargetType(),
                                                    "pin-note",
                                                    pdfCurrentPage,
                                                    -1,
                                                    -1,
                                                    "",
                                                    mouse.x / width,
                                                    mouse.y / height,
                                                    0,
                                                    0,
                                                    -1,
                                                    -1)
                                    }

                                    onPressed: function(mouse) {
                                        if (annotationTool !== "rect")
                                            return
                                        startXNorm = mouse.x / width
                                        startYNorm = mouse.y / height
                                        pdfRectGuide.visible = true
                                        pdfRectGuide.x = mouse.x
                                        pdfRectGuide.y = mouse.y
                                        pdfRectGuide.width = 0
                                        pdfRectGuide.height = 0
                                    }

                                    onPositionChanged: function(mouse) {
                                        if (annotationTool !== "rect" || !pressed)
                                            return
                                        const left = Math.min(mouse.x, startXNorm * width)
                                        const top = Math.min(mouse.y, startYNorm * height)
                                        pdfRectGuide.x = left
                                        pdfRectGuide.y = top
                                        pdfRectGuide.width = Math.abs(mouse.x - startXNorm * width)
                                        pdfRectGuide.height = Math.abs(mouse.y - startYNorm * height)
                                    }

                                    onReleased: function(mouse) {
                                        if (annotationTool !== "rect")
                                            return
                                        pdfRectGuide.visible = false
                                        const endXNorm = mouse.x / width
                                        const endYNorm = mouse.y / height
                                        const left = Math.min(startXNorm, endXNorm)
                                        const top = Math.min(startYNorm, endYNorm)
                                        const rectWidth = Math.abs(endXNorm - startXNorm)
                                        const rectHeight = Math.abs(endYNorm - startYNorm)
                                        if (rectWidth > 0.01 && rectHeight > 0.01) {
                                            openNewAnnotationDialog(
                                                        activeTargetType(),
                                                        "rect-note",
                                                        pdfCurrentPage,
                                                        -1,
                                                        -1,
                                                        "",
                                                        left,
                                                        top,
                                                        rectWidth,
                                                        rectHeight,
                                                        -1,
                                                        -1)
                                        }
                                    }
                                }

                                Rectangle {
                                    id: pdfRectGuide
                                    visible: false
                                    color: "#330f766e"
                                    border.color: accentColor
                                    border.width: 2
                                }

                                Repeater {
                                    model: annotationsForPage(pdfCurrentPage)

                                    delegate: Item {
                                        x: Number(modelData.x) * currentPageOverlay.width
                                        y: Number(modelData.y) * currentPageOverlay.height
                                        width: isRectAnnotationType(modelData.annotationType) ? Number(modelData.width) * currentPageOverlay.width : 22
                                        height: isRectAnnotationType(modelData.annotationType) ? Number(modelData.height) * currentPageOverlay.height : 22

                                        Rectangle {
                                            visible: isRectAnnotationType(modelData.annotationType)
                                            anchors.fill: parent
                                            radius: 6
                                            color: "transparent"
                                            border.color: modelData.color && modelData.color !== "" ? modelData.color : accentColor
                                            border.width: 2
                                        }

                                        Label {
                                            visible: isPinAnnotationType(modelData.annotationType)
                                            anchors.centerIn: parent
                                            text: "\u25CF"
                                            font.pixelSize: 18
                                            color: modelData.color && modelData.color !== "" ? modelData.color : accentColor
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                selectedVisualAnnotationId = Number(modelData.id)
                                            }
                                            onDoubleClicked: openEditAnnotationDialog(modelData)
                                        }

                                        Rectangle {
                                            visible: isPinAnnotationType(modelData.annotationType)
                                                     && modelData.content && modelData.content !== ""
                                            anchors.left: parent.right
                                            anchors.leftMargin: 8
                                            anchors.top: parent.top
                                            radius: 8
                                            color: panelColor
                                            border.color: selectedVisualAnnotationId === Number(modelData.id)
                                                          ? accentBorder
                                                          : borderColor
                                            z: 45
                                            implicitHeight: calloutText.implicitHeight + calloutMeta.implicitHeight + 16
                                            implicitWidth: Math.min(260, Math.max(140, calloutText.implicitWidth + 16))

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 4

                                                Label {
                                                    id: calloutText
                                                    text: modelData.content
                                                    color: textPrimary
                                                    wrapMode: Text.Wrap
                                                    maximumLineCount: 3
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Label {
                                                    id: calloutMeta
                                                    text: "You"
                                                    color: textSecondary
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: String(resolvedPdfPreviewUrl || "").trim() !== ""
                                 && !hasPdfPreviewUnavailableState()
                                 && pdfDocument.status !== PdfDocument.Ready
                        radius: 12
                        color: panelColor
                        border.color: borderColor

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 10

                            BusyIndicator {
                                running: true
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: "Loading page content..."
                                color: textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: hasPdfPreviewUnavailableState()
                        radius: 12
                        color: panelColor
                        border.color: borderColor

                        ColumnLayout {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 40, 480)
                            spacing: 10

                            Image {
                                source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/preview-unavailable.svg"
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: 220
                                sourceSize.height: 150
                                Layout.preferredWidth: 220
                                Layout.preferredHeight: 150
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: isPresentationFile() ? "Presentation preview unavailable" : "PDF preview unavailable"
                                color: textPrimary
                                font.pixelSize: 22
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            Label {
                                text: "Preview is unavailable for this document. Review externally."
                                color: textSecondary
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: readerStatusMessage !== ""
                                text: pdfPreviewErrorText !== "" ? pdfPreviewErrorText : readerStatusMessage
                                color: textSecondary
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            AppButton {
                                text: "Open Externally"
                                fillColor: accentColor
                                borderLine: accentColor
                                labelColor: "#ffffff"
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: fileListModel.openFile(fileIndex)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: panelColor
                    border.color: borderColor
                    visible: pdfFrame.pageCount > 0 && pdfDocument.status === PdfDocument.Ready
                    implicitHeight: 98

                    ListView {
                        id: filmstripView
                        anchors.fill: parent
                        anchors.margins: 8
                        orientation: ListView.Horizontal
                        spacing: 8
                        model: pdfFrame.pageCount
                        clip: true

                        delegate: Rectangle {
                            required property int index
                            width: 58
                            height: 78
                            radius: 6
                            color: panelSoft
                            border.color: index === pdfCurrentPage ? accentColor : borderColor
                            border.width: index === pdfCurrentPage ? 2 : 1

                            PdfPageImage {
                                anchors.fill: parent
                                anchors.margins: 3
                                document: pdfDocument
                                currentFrame: index
                                asynchronous: true
                                cache: false
                                fillMode: Image.PreserveAspectFit
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: pdfCurrentPage = index
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: mediaPreviewComponent

        Rectangle {
            color: panelSoft
            radius: 16
            border.color: borderColor

            Loader {
                id: mediaPreviewLoader
                anchors.fill: parent
                sourceComponent: mediaPreviewPanelComponent

                onStatusChanged: {
                    if (status === Loader.Ready && item) {
                        root.mediaControllerRef = item
                        item.mediaUrl = fileListModel.fileUrl(fileIndex)
                        item.videoMode = isVideoFile()
                        item.panelSoft = panelSoft
                        item.panelColor = panelColor
                        item.borderColor = borderColor
                        item.textPrimary = textPrimary
                        item.textSecondary = textSecondary
                        item.accentColor = accentColor
                        item.accentBorder = accentBorder
                        item.rangeStartMs = Qt.binding(function() { return root.mediaRangeStartMs })
                        item.rangeSaveEnabled = Qt.binding(function() {
                            return root.isMediaPlaybackReady()
                                    && root.mediaRangeStartMs >= 0
                                    && root.currentMediaPositionMs() > root.mediaRangeStartMs
                        })
                        item.onAddTimestamp = function() { root.addMediaTimestampAnnotation() }
                        item.onMarkRangeStart = function() { root.markMediaRangeStart() }
                        item.onSaveRange = function() { root.createMediaRangeAnnotationToCurrent() }
                        item.onClearRange = function() { root.mediaRangeStartMs = -1 }
                    } else if (status === Loader.Error) {
                        if (root.mediaControllerRef === item)
                            root.mediaControllerRef = null
                        if (root.mediaPreviewErrorText === "")
                            root.mediaPreviewErrorText = "Failed to create media preview item."
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: mediaPreviewErrorText !== "" || mediaPreviewLoader.status === Loader.Error
                color: panelSoft
                border.color: borderColor
                radius: 16

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 520)
                    spacing: 12

                    Image {
                        source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/preview-unavailable.svg"
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 260
                        sourceSize.height: 170
                        Layout.preferredWidth: 260
                        Layout.preferredHeight: 170
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: "Media preview unavailable"
                        font.pixelSize: 28
                        font.bold: true
                        color: textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "Multimedia runtime failed to initialize. You can still open this file externally."
                        wrapMode: Text.Wrap
                        color: textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    Label {
                        visible: mediaPreviewErrorText !== ""
                        text: mediaPreviewErrorText
                        wrapMode: Text.Wrap
                        color: "#9a3412"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    AppButton {
                        text: "Open Externally"
                        fillColor: accentColor
                        borderLine: accentColor
                        labelColor: "#ffffff"
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: fileListModel.openFile(fileIndex)
                    }
                }
            }

            Component.onDestruction: {
                if (root.mediaControllerRef === mediaPreviewLoader.item)
                    root.mediaControllerRef = null
            }
        }
    }

    Component {
        id: unsupportedPreviewComponent

        Rectangle {
            color: panelSoft
            radius: 16
            border.color: borderColor

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 520)
                spacing: 12

                Image {
                    source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/preview-unavailable.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 260
                    sourceSize.height: 170
                    Layout.preferredWidth: 260
                    Layout.preferredHeight: 170
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: "Unsupported file type"
                    font.pixelSize: 28
                    font.bold: true
                    color: textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                Label {
                    text: "Preview unavailable. File type not supported. Use Open Externally."
                    wrapMode: Text.Wrap
                    color: textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                AppButton {
                    text: "Open Externally"
                    fillColor: accentColor
                    borderLine: accentColor
                    labelColor: "#ffffff"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: fileListModel.openFile(fileIndex)
                }
            }
        }
    }
}
