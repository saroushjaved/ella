import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Item {
    id: root

    EllaTokens { id: tokens }

    property url mediaUrl: ""
    property bool videoMode: false

    property color panelSoft: tokens.panelSoft
    property color panelColor: tokens.panel
    property color borderColor: tokens.border
    property color textPrimary: tokens.textPrimary
    property color textSecondary: tokens.textSecondary
    property color accentColor: tokens.accent
    property color accentBorder: tokens.accentBorder

    property int rangeStartMs: -1
    property bool rangeSaveEnabled: false
    property var onAddTimestamp: null
    property var onMarkRangeStart: null
    property var onSaveRange: null
    property var onClearRange: null

    readonly property bool previewUnavailable: !mediaUrl || String(mediaUrl).trim() === ""
    readonly property bool playbackReady: !previewUnavailable && player.mediaStatus !== MediaPlayer.NoMedia

    function currentPositionMs() {
        return Math.max(0, Math.round(Number(player.position || 0)))
    }

    function durationMs() {
        return Math.max(0, Math.round(Number(player.duration || 0)))
    }

    function seekToMs(ms) {
        const target = Math.max(0, Math.min(durationMs(), Math.round(Number(ms || 0))))
        player.position = target
    }

    function seekRelativeMs(deltaMs) {
        seekToMs(currentPositionMs() + Number(deltaMs || 0))
    }

    function play() {
        player.play()
    }

    function pause() {
        player.pause()
    }

    function togglePlayback() {
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else
            player.play()
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

    function invokeCallback(fn) {
        if (fn)
            fn()
    }

    AudioOutput {
        id: output
        volume: 1.0
    }

    MediaPlayer {
        id: player
        audioOutput: output
        videoOutput: videoMode ? videoOutput : null
        source: root.mediaUrl
    }

    component MediaButton: EllaButton {
        tokens: tokens
        implicitHeight: 32
        font.pixelSize: 12
        radiusValue: tokens.radiusSm
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 360
            radius: tokens.radiusLg
            color: videoMode ? "#0b1220" : panelColor
            border.color: borderColor
            border.width: 1
            clip: true

            VideoOutput {
                id: videoOutput
                anchors.fill: parent
                visible: videoMode && !previewUnavailable
                fillMode: VideoOutput.PreserveAspectFit
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                visible: !videoMode || previewUnavailable

                Image {
                    visible: previewUnavailable
                    source: "qrc:/qt/qml/SecondBrain/src/ui/assets/vendor/preview-unavailable.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 240
                    sourceSize.height: 160
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: 160
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    visible: !previewUnavailable
                    text: "\u266B"
                    color: textSecondary
                    font.pixelSize: 52
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: previewUnavailable ? "Media preview unavailable" : "Audio playback"
                    color: textSecondary
                    font.pixelSize: 18
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }

                EllaButton {
                    visible: previewUnavailable
                    tokens: tokens
                    text: "Open Externally"
                    tone: "primary"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: Qt.openUrlExternally(String(mediaUrl))
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: tokens.radiusLg
            color: panelColor
            border.color: borderColor
            implicitHeight: mediaControlColumn.implicitHeight + 14

            ColumnLayout {
                id: mediaControlColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    MediaButton {
                        text: "-10s"
                        tone: "ghost"
                        enabled: playbackReady
                        onClicked: root.seekRelativeMs(-10000)
                    }

                    Rectangle {
                        implicitWidth: 48
                        implicitHeight: 48
                        radius: 24
                        color: accentColor
                        border.color: accentColor
                        opacity: playbackReady ? 1.0 : 0.6

                        Label {
                            anchors.centerIn: parent
                            text: player.playbackState === MediaPlayer.PlayingState ? "\u23F8" : "\u25B6"
                            color: "#ffffff"
                            font.bold: true
                            font.pixelSize: 16
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: playbackReady
                            onClicked: root.togglePlayback()
                            cursorShape: Qt.PointingHandCursor
                        }
                    }

                    MediaButton {
                        text: "+10s"
                        tone: "ghost"
                        enabled: playbackReady
                        onClicked: root.seekRelativeMs(10000)
                    }

                    Item { Layout.fillWidth: true }

                    Repeater {
                        model: ["0.75x", "1.0x", "1.25x", "1.5x", "2.0x"]

                        delegate: MediaButton {
                            text: modelData
                            tone: Math.abs(Number(player.playbackRate || 1) - Number(String(modelData).replace("x", ""))) < 0.001
                                  ? "primary"
                                  : "ghost"
                            enabled: playbackReady
                            onClicked: {
                                const raw = String(modelData).replace("x", "")
                                player.playbackRate = Number(raw)
                            }
                        }
                    }
                }

                Slider {
                    id: timelineSlider
                    Layout.fillWidth: true
                    implicitHeight: 22
                    from: 0
                    to: Math.max(1, Number(player.duration || 1))
                    value: Number(player.position || 0)
                    enabled: playbackReady

                    onMoved: player.position = value
                    onPressedChanged: {
                        if (!pressed)
                            player.position = value
                    }

                    background: Rectangle {
                        x: 0
                        y: (parent.height - height) / 2
                        width: parent.width
                        height: 6
                        radius: 3
                        color: borderColor

                        Rectangle {
                            width: parent.width * ((root.durationMs() > 0) ? (player.position / player.duration) : 0)
                            height: parent.height
                            radius: parent.radius
                            color: accentColor
                        }
                    }

                    handle: Rectangle {
                        x: timelineSlider.leftPadding + timelineSlider.visualPosition * (timelineSlider.availableWidth - width)
                        y: (timelineSlider.height - height) / 2
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 7
                        color: "#ffffff"
                        border.color: accentColor
                        border.width: 2
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: root.formatDurationMs(player.position) + " / " + root.formatDurationMs(player.duration)
                        color: textSecondary
                        font.pixelSize: 13
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: player.errorString !== "" ? player.errorString : ""
                        visible: player.errorString !== ""
                        color: tokens.dangerText
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: panelSoft
                    border.color: borderColor
                    implicitHeight: timelineToolsColumn.implicitHeight + 12

                    ColumnLayout {
                        id: timelineToolsColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6

                        Label {
                            text: "ANNOTATION TOOLS (SECONDARY)"
                            color: textSecondary
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Flow {
                            Layout.fillWidth: true
                            width: parent.width
                            spacing: 6

                            MediaButton {
                                text: "Add Timestamp Note"
                                tone: "ghost"
                                enabled: playbackReady
                                onClicked: root.invokeCallback(root.onAddTimestamp)
                            }

                            MediaButton {
                                text: rangeStartMs >= 0 ? "Start " + formatDurationMs(rangeStartMs) : "Mark Range Start"
                                tone: "ghost"
                                enabled: playbackReady
                                onClicked: root.invokeCallback(root.onMarkRangeStart)
                            }

                            MediaButton {
                                text: "Save Range Note"
                                tone: "ghost"
                                enabled: rangeSaveEnabled
                                onClicked: root.invokeCallback(root.onSaveRange)
                            }

                            MediaButton {
                                text: "Clear Range"
                                tone: "ghost"
                                enabled: rangeStartMs >= 0
                                onClicked: root.invokeCallback(root.onClearRange)
                            }
                        }
                    }
                }
            }
        }
    }
}
