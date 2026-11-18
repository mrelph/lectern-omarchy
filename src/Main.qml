import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Lectern

ApplicationWindow {
    id: win
    visible: true
    width: 1240
    height: 800
    minimumWidth: 760
    minimumHeight: 540
    title: (backend.title || qsTr("Untitled deck")) + (backend.dirty ? " • " : " — ") + "Lectern"
    color: "#111318"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool presenting: false
    property bool previousFullscreen: false
    property bool quitting: false
    property var pendingAction
    readonly property color panel: "#171a21"
    readonly property color line: "#2b303b"
    readonly property color muted: "#aab2c2"

    function guard(action) {
        if (backend.dirty) {
            pendingAction = action
            discard.open()
        } else {
            action()
        }
    }

    function navigate(index) {
        const position = backend.select(index)
        editor.cursorPosition = Math.min(position, editor.length)
    }

    function togglePresent() {
        if (presenting) {
            presenting = false
            if (!previousFullscreen)
                visibility = Window.Windowed
        } else {
            previousFullscreen = visibility === Window.FullScreen
            presenting = true
            showFullScreen()
            stage.forceActiveFocus()
        }
    }

    onClosing: close => {
        if (backend.dirty && !quitting) {
            close.accepted = false
            guard(() => {
                quitting = true
                win.close()
            })
        }
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !win.presenting; onActivated: win.guard(() => backend.newDeck()) }
    Shortcut { sequence: "Ctrl+O"; enabled: !win.presenting; onActivated: win.guard(() => backend.choose("open")) }
    Shortcut { sequence: "Ctrl+S"; enabled: !win.presenting; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !win.presenting; onActivated: backend.choose("save") }
    Shortcut { sequence: "Ctrl+Shift+E"; enabled: !win.presenting; onActivated: backend.choose("pdf") }
    Shortcut { sequence: "Ctrl+Return"; onActivated: win.togglePresent() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: win.close() }
    Shortcut { sequence: "Q"; enabled: win.presenting || !editor.activeFocus; onActivated: win.close() }
    Shortcut { sequence: "F1"; enabled: !win.presenting; onActivated: help.open() }
    Shortcut { sequence: "?"; enabled: !win.presenting && !editor.activeFocus; onActivated: help.open() }
    Shortcut { sequence: "Space"; enabled: !win.presenting && !editor.activeFocus; onActivated: win.togglePresent() }

    Dialog {
        id: discard
        anchors.centerIn: parent
        title: qsTr("Discard unsaved changes?")
        modal: true
        standardButtons: Dialog.Discard | Dialog.Cancel
        onDiscarded: {
            const action = win.pendingAction
            win.pendingAction = null
            action()
        }
        onRejected: win.pendingAction = null
    }

    Dialog {
        id: help
        width: Math.min(win.width - 40, 520)
        anchors.centerIn: parent
        title: qsTr("Keyboard shortcuts")
        modal: true
        standardButtons: Dialog.Close
        Label {
            text: qsTr("New deck   Ctrl+N\nOpen   Ctrl+O\nSave   Ctrl+S\nSave As   Ctrl+Shift+S\nExport PDF   Ctrl+Shift+E\nPresent   Ctrl+Enter\nNext slide   → / ↓ / Space / Enter\nPrevious slide   ← / ↑\nFirst / last slide   Home / End\nLeave fullscreen   Esc\nHelp   F1\nQuit   Ctrl+Q")
            lineHeight: 1.5
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !win.presenting
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: win.panel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8
                Rectangle {
                    width: 5
                    height: 28
                    color: theme.accent
                    radius: Math.min(theme.radius, 2)
                }
                Label {
                    Layout.leftMargin: 5
                    text: "Lectern"
                    font.pixelSize: 23
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("New"); onClicked: win.guard(() => backend.newDeck()) }
                Button { text: qsTr("Open"); onClicked: win.guard(() => backend.choose("open")) }
                Button { text: qsTr("Save"); onClicked: backend.save() }
                Button { text: qsTr("Save As"); onClicked: backend.choose("save") }
                Button { text: qsTr("Export PDF"); onClicked: backend.choose("pdf") }
                Button {
                    text: qsTr("Present")
                    highlighted: true
                    Material.background: theme.accent
                    Material.foreground: theme.accentForeground
                    onClicked: win.togglePresent()
                }
                Button { text: "?"; Accessible.name: qsTr("Keyboard shortcuts"); onClicked: help.open() }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: win.width > 980 ? 204 : 156
                Layout.fillHeight: true
                color: win.panel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Label {
                        Layout.leftMargin: 7
                        text: qsTr("Slides")
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: win.muted
                    }
                    ListView {
                        id: rail
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: backend.headings
                        currentIndex: backend.index
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        clip: true
                        spacing: 4
                        delegate: ItemDelegate {
                            required property int index
                            required property string modelData
                            width: rail.width
                            height: 44
                            highlighted: backend.index === index
                            onClicked: win.navigate(index)
                            background: Rectangle {
                                radius: theme.radius
                                color: parent.highlighted ? "#2a3040" : parent.hovered ? "#222631" : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Label {
                                    text: (index + 1).toLocaleString(Qt.locale(translator.language), 'f', 0)
                                    color: backend.index === index ? theme.accent : win.muted
                                    font.family: "monospace"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData || qsTr("Empty slide")
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                }
                            }
                        }
                    }
                    Button { text: qsTr("Open example"); onClicked: win.guard(() => backend.sample()) }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: win.line }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                handle: Rectangle { implicitWidth: 5; color: win.line }
                ColumnLayout {
                    SplitView.preferredWidth: win.width * 0.33
                    SplitView.minimumWidth: 200
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        padding: 15
                        text: qsTr("Markdown")
                        font.weight: Font.DemiBold
                        color: win.muted
                        background: Rectangle { color: win.panel }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            id: editor
                            objectName: "editor"
                            text: backend.source.replace(/\r\n/g, "\n")
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font.family: "monospace"
                            font.pixelSize: 14
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 14
                            background: null
                            onTextChanged: if (activeFocus) backend.edit(text, cursorPosition)
                            onCursorPositionChanged: if (activeFocus) backend.followCursor(cursorPosition)
                            Connections {
                                target: backend
                                function onSourceLoaded() {
                                    editor.text = backend.source.replace(/\r\n/g, "\n")
                                    editor.cursorPosition = Math.min(backend.select(backend.index), editor.length)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 14
                        Layout.preferredHeight: 48
                        Label { text: qsTr("Preview"); font.weight: Font.DemiBold; color: win.muted }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("%1 of %2")
                                .arg((backend.index + 1).toLocaleString(Qt.locale(translator.language), 'f', 0))
                                .arg(backend.count.toLocaleString(Qt.locale(translator.language), 'f', 0))
                            color: win.muted
                        }
                    }
                    SlideView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 16
                        deck: backend
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        color: win.panel
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            Label { text: qsTr("Speaker notes"); font.weight: Font.DemiBold; color: win.muted }
                            ScrollView {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                clip: true
                                TextArea {
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    padding: 0
                                    background: null
                                    textFormat: TextEdit.PlainText
                                    text: backend.notes
                                    placeholderText: qsTr("Add notes below a Note: line in the Markdown.")
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        Label {
            Layout.fillWidth: true
            padding: 10
            text: backend.status || (backend.dirty ? qsTr("Unsaved changes") : backend.title || qsTr("Untitled deck"))
            elide: Text.ElideRight
            color: win.muted
            textFormat: Text.PlainText
        }
    }

    Rectangle {
        id: stage
        anchors.fill: parent
        color: "black"
        visible: win.presenting
        SlideView { anchors.fill: parent; deck: backend }
        TapHandler { onTapped: win.navigate(backend.index + 1) }
        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Right:
            case Qt.Key_Down:
            case Qt.Key_Space:
            case Qt.Key_Return:
            case Qt.Key_Enter:
                win.navigate(backend.index + 1)
                break
            case Qt.Key_Left:
            case Qt.Key_Up:
                win.navigate(backend.index - 1)
                break
            case Qt.Key_Home:
                win.navigate(0)
                break
            case Qt.Key_End:
                win.navigate(backend.count - 1)
                break
            case Qt.Key_Escape:
                win.togglePresent()
                break
            default:
                return
            }
            event.accepted = true
        }
    }
}
