import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    required property bool hideNotes
    required property bool applyingNotes
    required property int zoom
    property alias textDocument: source.textDocument
    property alias cursorPosition: source.cursorPosition
    readonly property alias length: source.length
    readonly property alias text: source.text
    readonly property alias selectionStart: source.selectionStart
    readonly property alias selectionEnd: source.selectionEnd
    readonly property alias canUndo: source.canUndo
    readonly property alias canRedo: source.canRedo
    readonly property var projection: backend.editorProjection
    property bool syncing: false
    property bool ready: false

    function allowsImport() {
        return projection.allowsReplacement(selectionStart, selectionEnd, "![media](assets/media)")
    }
    function pasteMedia(event) {
        if (event.matches(StandardKey.Paste))
            event.accepted = !allowsImport() || backend.pasteMedia()
    }
    function select(a, b) {
        source.select(a, b)
        syncFromSource()
    }
    function syncFromSource() {
        if (!ready || !hideNotes || syncing) return
        syncing = true
        const anchor = source.cursorPosition === source.selectionStart ? source.selectionEnd : source.selectionStart
        compact.select(projection.toView(anchor), projection.toView(source.cursorPosition))
        syncing = false
    }
    function syncFromView() {
        if (!ready || !hideNotes || syncing || applyingNotes) return
        syncing = true
        const anchor = compact.cursorPosition === compact.selectionStart ? compact.selectionEnd : compact.selectionStart
        const pos = projection.toSource(compact.cursorPosition)
        source.select(projection.toSource(anchor), pos)
        if (compact.selectionStart === compact.selectionEnd && projection.toView(pos) !== compact.cursorPosition)
            compact.cursorPosition = projection.toView(pos)
        if (compact.activeFocus) backend.followCursor(pos)
        syncing = false
    }
    function undo() { source.undo(); syncFromSource() }
    function redo() { source.redo(); syncFromSource() }
    function modeChanged() {
        if (!ready) return
        const focused = activeFocus
        projection.setEnabled(hideNotes)
        syncFromSource()
        if (focused) (hideNotes ? compact : source).forceActiveFocus()
    }
    onHideNotesChanged: modeChanged()
    Component.onCompleted: {
        projection.attach(source.textDocument, compact.textDocument)
        ready = true
        modeChanged()
    }
    Connections {
        target: backend
        function onSourceLoaded() {
            root.syncing = true
            source.text = backend.source.replace(/\r\n/g, "\n")
            source.cursorPosition = Math.min(backend.select(backend.index), source.length)
            root.syncing = false
            root.syncFromSource()
        }
    }
    Connections {
        target: root.projection
        function onRefused() { feedback.show(qsTr("Show notes in the editor to edit this selection."), 3000) }
    }
    ToolTip { id: feedback; parent: root; y: 8 }

    component MarkdownArea: AppTextArea {
        id: area
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: drop => {
                if (!drop.hasUrls || !root.allowsImport()) return
                area.forceActiveFocus()
                if (area.selectionStart === area.selectionEnd)
                    area.cursorPosition = area.positionAt(drop.x, drop.y)
                if (!root.allowsImport()) return
                for (const url of drop.urls) backend.importMedia(url)
                drop.acceptProposedAction()
            }
        }
        textFormat: TextEdit.PlainText
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        font.family: "monospace"
        font.pixelSize: root.zoom
        leftPadding: 24
        rightPadding: 24
        topPadding: 20
        background: Rectangle {
            color: theme.colors.reader
            border.width: area.activeFocus ? 1 : 0
            border.color: theme.colors.focus
        }
    }
    ScrollView {
        anchors.fill: parent
        visible: !root.hideNotes
        focus: visible
        MarkdownArea {
            id: source
            objectName: "fullSourceEditor"
            Keys.onPressed: event => root.pasteMedia(event)
            focus: true
            text: backend.source.replace(/\r\n/g, "\n")
            onTextChanged: if (root.ready && !root.applyingNotes && !root.syncing && root.activeFocus)
                               backend.edit(text, cursorPosition)
            onCursorPositionChanged: {
                if (root.hideNotes) root.syncFromSource()
                else if (activeFocus && !root.applyingNotes) backend.followCursor(cursorPosition)
            }
        }
    }
    ScrollView {
        anchors.fill: parent
        visible: root.hideNotes
        focus: visible
        MarkdownArea {
            id: compact
            objectName: "collapsedNotesEditor"
            focus: true
            onCursorPositionChanged: root.syncFromView()
            onSelectionStartChanged: root.syncFromView()
            onSelectionEndChanged: root.syncFromView()
            Keys.onShortcutOverride: event => {
                if (event.matches(StandardKey.Undo) || event.matches(StandardKey.Redo)) event.accepted = true
            }
            Keys.onPressed: event => {
                if (event.matches(StandardKey.Paste)) root.pasteMedia(event)
                else if (event.matches(StandardKey.Undo)) { root.undo(); event.accepted = true }
                else if (event.matches(StandardKey.Redo)) { root.redo(); event.accepted = true }
            }
        }
    }
}
