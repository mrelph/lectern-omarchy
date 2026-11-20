import QtQuick
import QtCore
import QtMultimedia
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

    property int workspaceMode: viewSettings.workspaceMode
    property bool notesVisible: viewSettings.notesVisible
    property int editorZoom: viewSettings.editorZoom
    Settings {
        id: viewSettings
        category: "workspace"
        property int workspaceMode: 1
        property bool hideEditorNotes: false
        property bool notesVisible: true
        property int editorZoom: 14
    }
    onWorkspaceModeChanged: viewSettings.workspaceMode = workspaceMode
    onNotesVisibleChanged: viewSettings.notesVisible = notesVisible
    onEditorZoomChanged: viewSettings.editorZoom = editorZoom
    readonly property bool dialogOpen: commands.visible || library.visible || gallery.visible || reference.visible
                                       || help.visible || checkDialog.visible || updates.visible || discard.visible || conflict.visible
    readonly property bool editing: !presenting && !dialogOpen
    property bool presenting: false
    onPresentingChanged: backend.presentationActive(presenting, stage)
    property bool presenterMode: false
    property bool singleAudience: false
    property string audienceScreenName: ""
    readonly property bool dualPresentation: presenting && audienceScreenName !== ""
    property int previousVisibility: Window.Windowed
    property bool quitting: false
    property var pendingAction
    readonly property color panel: "#171a21"
    readonly property color line: "#2b303b"
    readonly property color muted: "#aab2c2"

    component EditButton: ToolButton {
        required property string hint
        font.family: "monospace"
        implicitWidth: 40
        implicitHeight: 34
        Accessible.name: hint
        ToolTip.text: hint
        ToolTip.visible: hovered || visualFocus
        ToolTip.delay: 400
        palette.buttonText: theme.accent
        background: Rectangle {
            radius: theme.radius
            color: parent.down || parent.hovered ? win.line : "transparent"
            border.color: parent.visualFocus ? theme.accent : win.line
        }
    }

    function changeAppearance(key, value) {
        if (notesEditor.applyChanges())
            backend.setAppearance(key, value, editor.textDocument)
    }

    function guard(action) {
        if (!notesEditor.applyChanges())
            return
        if (backend.dirty) {
            pendingAction = action
            discard.open()
        } else {
            action()
        }
    }

    function navigate(index) {
        if (!notesEditor.applyChanges())
            return
        const position = backend.select(index)
        editor.cursorPosition = Math.min(position, editor.length)
        if (!presenting)
            editor.forceActiveFocus()
    }

    function startPresentation(forcePresenter) {
        if (presenting || !notesEditor.applyChanges())
            return
        previousVisibility = visibility
        audienceScreenName = backend.prepareAudienceWindow(audience, win)
        presenterMode = forcePresenter || audienceScreenName !== ""
        presenter.resetTimer()
        presenting = true
        if (dualPresentation)
            audience.showFullScreen()
        if (presenterMode) {
            presenter.forceActiveFocus()
        } else {
            showFullScreen()
            stage.forceActiveFocus()
        }
    }

    function stopPresentation() {
        if (!presenting)
            return
        presenting = false
        presenterMode = false
        singleAudience = false
        audience.hide()
        audienceScreenName = ""
        stage.resetControls()
        visibility = previousVisibility
        editor.forceActiveFocus()
    }

    function togglePresent() {
        if (presenting)
            stopPresentation()
        else
            startPresentation(false)
    }

    function togglePresenter() {
        if (!presenting) {
            startPresentation(true)
        } else if (!dualPresentation) {
            if (presenterMode)
                singleAudience = !singleAudience
            else
                presenterMode = true
            if (singleAudience) {
                showFullScreen()
                stage.forceActiveFocus()
            } else {
                visibility = previousVisibility
                presenter.forceActiveFocus()
            }
        }
    }

    Connections {
        target: Qt.application
        function onScreensChanged() {
            if (win.dualPresentation
                    && !Qt.application.screens.some(candidate => candidate.name === win.audienceScreenName)) {
                audience.hide()
                win.audienceScreenName = ""
                presenter.forceActiveFocus()
            }
        }
    }

    onClosing: close => {
        if (presenting) {
            close.accepted = false
            stopPresentation()
            return
        }
        if (!notesEditor.applyChanges()) {
            close.accepted = false
            return
        }
        if (backend.dirty && !quitting) {
            close.accepted = false
            guard(() => {
                quitting = true
                win.close()
            })
        }
    }

    Shortcut { sequence: "Ctrl+N"; enabled: win.editing; onActivated: win.guard(() => backend.newDeck()) }
    Shortcut { sequence: "Ctrl+O"; enabled: win.editing; onActivated: win.guard(() => backend.choose("open")) }
    Shortcut { sequence: "Ctrl+S"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) backend.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) backend.choose("save") }
    Shortcut { sequence: "Ctrl+Shift+E"; enabled: win.editing; onActivated: backend.choose("pdf") }
    Shortcut { sequence: "Ctrl+Shift+Return"; enabled: win.editing; onActivated: { win.navigate(0); win.startPresentation(false) } }
    Shortcut { sequence: "Ctrl+W"; enabled: win.presenting; context: Qt.ApplicationShortcut; onActivated: win.stopPresentation() }
    Shortcut { sequence: "Ctrl+Alt+Shift+E"; enabled: win.editing; onActivated: backend.choose("png") }
    Shortcut { sequence: "Ctrl+1"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) win.workspaceMode = 0 }
    Shortcut { sequence: "Ctrl+2"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) win.workspaceMode = 1 }
    Shortcut { sequence: "Ctrl+3"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) win.workspaceMode = 2 }
    Shortcut { sequence: "Ctrl+Return"; enabled: !win.dialogOpen; onActivated: win.togglePresent() }
    Shortcut { sequence: "Ctrl+Alt+Return"; enabled: !win.dialogOpen; onActivated: win.togglePresenter() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: win.close() }
    Shortcut { sequence: "Q"; enabled: win.presenting; onActivated: win.close() }
    Shortcut { sequence: "F1"; enabled: win.editing; onActivated: help.open() }
    Shortcut { sequence: "?"; enabled: win.editing && !editor.activeFocus && !notesEditor.activeFocus; onActivated: help.open() }
    Shortcut { sequence: "Space"; enabled: win.editing && !editor.activeFocus && !notesEditor.activeFocus; onActivated: win.togglePresent() }

    readonly property var formats: [
        [qsTr("Bold"), "Ctrl+B", "**", "**", qsTr("bold text")],
        [qsTr("Italic"), "Ctrl+I", "*", "*", qsTr("italic text")],
        [qsTr("Inline code"), "Ctrl+Alt+C", "`", "`", "code"],
        [qsTr("Link"), "Ctrl+K", "[", "](https://)", qsTr("link text")],
        [qsTr("Heading 1"), "Ctrl+Alt+1", "# "],
        [qsTr("Heading 2"), "Ctrl+Alt+2", "## "],
        [qsTr("Heading 3"), "Ctrl+Alt+3", "### "],
        [qsTr("Bulleted list"), "Ctrl+Shift+8", "- "],
        [qsTr("Numbered list"), "Ctrl+Shift+7", "1. "],
        [qsTr("Quote"), "Ctrl+Shift+9", "> "],
        [qsTr("Code block"), "Ctrl+Alt+Shift+C", "\n```\n", "\n```\n", "code"],
        [qsTr("Table"), "Ctrl+Alt+T", "\n| A | B |\n| --- | --- |\n| 1 | 2 |\n", "", ""],
        [qsTr("Slide break"), "Ctrl+Shift+Space", "\n\n---\n\n", "", ""]
    ]
    function format(entry) {
        if (!notesEditor.applyChanges()) return
        if (workspaceMode === 2) workspaceMode = 1
        let a = editor.selectionStart, b = editor.selectionEnd
        const source = editor.text, before = entry[2], after = entry[3]
        let replacement, selectAt, length
        if (entry.length === 3) {
            a = a ? source.lastIndexOf("\n", a - 1) + 1 : 0
            const next = source.indexOf("\n", Math.max(a, b - 1))
            b = next < 0 ? source.length : next
            const lines = source.slice(a,b).split("\n")
            const remove = lines.every(line => line.startsWith(before))
            const oldPrefix = before.startsWith("#") ? /^#{1,6}\s+/ : before === "> " ? /^>\s?/ : /^(?:[-*+]\s+|\d+\.\s+)/
            replacement = lines.map(line => remove ? line.slice(before.length) : before + line.replace(oldPrefix, "")).join("\n")
            selectAt = a + replacement.length; length = 0
        } else {
            const selected = source.slice(a,b)
            if (after && selected.length >= before.length + after.length && selected.startsWith(before) && selected.endsWith(after)) {
                replacement = selected.slice(before.length, selected.length - after.length)
                selectAt = a
                length = replacement.length
            } else if (after && source.slice(0,a).endsWith(before) && source.slice(b).startsWith(after)) {
                a -= before.length; b += after.length
                replacement = selected; selectAt = a; length = selected.length
            } else {
                const inner = selected || entry[4]
                replacement = before + inner + after; selectAt = a + before.length; length = inner.length
            }
        }
        editor.forceActiveFocus()
        if (!backend.editorProjection.allowsReplacement(a, b, replacement)) return
        backend.replaceSelection(editor.textDocument, a,b,replacement)
        editor.select(selectAt, selectAt + length)
    }
    Instantiator {
        model: win.formats
        delegate: Shortcut {
            required property var modelData
            sequence: modelData[1]
            enabled: win.editing && editor.activeFocus
            onActivated: win.format(modelData)
        }
    }
    Shortcut { sequence: "Ctrl+Shift+P"; enabled: win.editing; onActivated: commands.open() }
    Shortcut { sequence: "Ctrl+/"; enabled: win.editing; onActivated: { helpTabs.currentIndex = 1; help.open() } }
    Shortcut { sequence: "Ctrl+Alt+/"; enabled: win.editing; onActivated: reference.open() }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: win.editing; onActivated: backend.reveal() }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: win.editing; onActivated: backend.choose("folder") }
    Shortcut { sequence: "Ctrl+Shift+L"; enabled: win.editing; onActivated: library.open() }
    function toggleEditorNotes() {
        if (notesEditor.applyChanges()) viewSettings.hideEditorNotes = !viewSettings.hideEditorNotes
    }
    Shortcut { sequence: "Ctrl+Alt+Shift+N"; enabled: win.editing; onActivated: win.toggleEditorNotes() }
    Shortcut { sequence: "Ctrl+Alt+N"; enabled: win.editing; onActivated: if (notesEditor.applyChanges()) win.notesVisible = !win.notesVisible }
    Shortcut { sequence: "Ctrl+="; enabled: win.editing; onActivated: win.editorZoom = Math.min(32,win.editorZoom + 2) }
    Shortcut { sequence: "Ctrl+-"; enabled: win.editing; onActivated: win.editorZoom = Math.max(10,win.editorZoom - 2) }
    Shortcut { sequence: "Ctrl+0"; enabled: win.editing; onActivated: win.editorZoom = 14 }
    CommandPalette {
        id: commands
        objectName: "commandPalette"
        commands: [
            [viewSettings.hideEditorNotes ? qsTr("Show notes in editor") : qsTr("Hide notes in editor"), "Ctrl+Alt+Shift+N", () => win.toggleEditorNotes()],
            ...win.formats.map(f => [f[0], f[1], () => win.format(f)]),
            [qsTr("Insert image or video…"), "Ctrl+Shift+I", () => { if (notesEditor.applyChanges() && editor.allowsImport()) backend.choose("media") }],
            [qsTr("Move slide up"), "Ctrl+Alt+↑", () => win.moveCurrent(-1)],
            [qsTr("Move slide down"), "Ctrl+Alt+↓", () => win.moveCurrent(1)],
            [qsTr("Choose a slide theme"), "", () => gallery.open()],
            [qsTr("Open library"), "Ctrl+Shift+L", () => library.open()],
            [qsTr("Add folder…"), "Ctrl+Shift+O", () => backend.choose("folder")],
            [qsTr("New"), "Ctrl+N", () => win.guard(() => backend.newDeck())],
            [qsTr("Open"), "Ctrl+O", () => win.guard(() => backend.choose("open"))],
            [qsTr("Save"), "Ctrl+S", () => { if (notesEditor.applyChanges()) backend.save() }],
            [qsTr("Save As"), "Ctrl+Shift+S", () => { if (notesEditor.applyChanges()) backend.choose("save") }],
            [qsTr("New slide"), "Ctrl+Shift+N", () => win.addSlide("## " + qsTr("New slide"))],
            [qsTr("Present"), "Ctrl+Enter", () => win.startPresentation(false)],
            [qsTr("Present from start"), "Ctrl+Shift+Enter", () => { win.navigate(0); win.startPresentation(false) }],
            [qsTr("Export PDF with notes"), "", () => { if (notesEditor.applyChanges()) backend.choose("pdf-notes") }],
            [qsTr("Show or hide speaker notes"), "Ctrl+Alt+N", () => { if (notesEditor.applyChanges()) win.notesVisible = !win.notesVisible }],
            [qsTr("Reveal deck in file manager"), "Ctrl+Shift+R", () => backend.reveal()],
            [qsTr("Check deck"), "", () => { checkDialog.problems = backend.validate(); checkDialog.open() }],
            [qsTr("Markdown reference"), "Ctrl+Alt+/", () => reference.open()],
            [qsTr("Export PDF"), "Ctrl+Shift+E", () => backend.choose("pdf")],
            [qsTr("Export slide images"), "Ctrl+Alt+Shift+E", () => backend.choose("png")],
            [qsTr("Start Presenter View"), "Ctrl+Alt+Enter", () => win.startPresentation(true)]
        ]
    }
    LecternDialog {
        id: library
        modal: true
        title: qsTr("Deck library")
        anchors.centerIn: parent
        width: Math.min(580, win.width - 48)
        height: Math.min(520, win.height - 48)
        property var rows: []
        onAboutToShow: { rows = backend.library(); libraryQuery.clear(); libraryQuery.forceActiveFocus() }
        Connections { target: backend; function onLibraryChanged() { if (library.visible) library.rows = backend.library() } }
        contentItem: ColumnLayout {
            RowLayout {
                Button { text: qsTr("Add folder…"); onClicked: backend.choose("folder") }
                Button { text: qsTr("Open file…"); onClicked: { library.close(); win.guard(() => backend.choose("open")) } }
            }
            TextField { id: libraryQuery; Layout.fillWidth: true; placeholderText: qsTr("Find decks…") }
            ListView {
                id: files
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: library.rows.filter(r => !libraryQuery.text || !r.folder && (r.name + " " + r.path).toLocaleLowerCase().includes(libraryQuery.text.toLocaleLowerCase()))
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: modelData.folder ? 42 : 60
                    contentItem: RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label { Layout.fillWidth: true; text: modelData.name; font.bold: modelData.folder; elide: Text.ElideRight; textFormat: Text.PlainText }
                            Label { visible: !modelData.folder; Layout.fillWidth: true; font.pixelSize: 12; color: win.muted; text: modelData.group === qsTr("Recent") ? modelData.group + " · " + modelData.path : modelData.path; elide: Text.ElideMiddle; textFormat: Text.PlainText }
                        }
                        ToolButton { visible: modelData.folder; text: qsTr("Remove"); onClicked: backend.forgetFolder(modelData.path) }
                    }
                    onClicked: if (!modelData.folder) {
                        const path = modelData.path
                        library.close()
                        win.guard(() => backend.open(path))
                    }
                }
                Label { anchors.centerIn: parent; visible: !files.count; text: libraryQuery.text ? qsTr("No matching decks") : qsTr("Open a deck or add a folder to begin.") }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
    ThemeGallery {
        id: gallery
        objectName: "themeGallery"
        onChosen: (name, currentOnly) => {
            if (!notesEditor.applyChanges()) return
            if (currentOnly) backend.setSlideTheme(name, editor.textDocument)
            else win.changeAppearance("theme", name)
            close()
        }
    }
    function moveCurrent(direction) {
        if (!notesEditor.applyChanges()) return
        backend.moveSlide(direction, editor.textDocument)
        win.navigate(backend.index)
    }
    Shortcut { sequence: "Ctrl+Alt+Up"; enabled: win.editing; onActivated: win.moveCurrent(-1) }
    Shortcut { sequence: "Ctrl+Alt+Down"; enabled: win.editing; onActivated: win.moveCurrent(1) }
    LecternDialog {
        id: reference
        title: qsTr("Markdown reference")
        modal: true
        anchors.centerIn: parent
        width: Math.min(600, win.width - 48)
        height: Math.min(480, win.height - 48)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            ComboBox {
                id: examples
                Layout.fillWidth: true
                model: [qsTr("Title and subtitle"), qsTr("Quote"), qsTr("Code"), qsTr("Image"), qsTr("Speaker notes"), qsTr("Slide theme"), qsTr("Deck settings"), qsTr("Lists"), qsTr("Table"), qsTr("Video"), qsTr("Slide break")]
                readonly property var snippets: ["# A new idea\n\n## A short subtitle", "> A sentence worth remembering.\n>\n> — Author", "```sh\nlectern-omarchy check talk.md\n```", "![Description](assets/photo.jpg)", "## The next step\n\nNote:\nWhat the audience should remember.", "<!-- theme: warm-paper -->\n# A change of pace", "---\ntitle: My talk\naspect: '16:9'\ntheme: blocks-dark\ntransition: slide\nfont: roboto\nfont-size: 36\ncode-font: geist-mono\n---\n# My talk", "## Priorities\n\n- First decision\n- Next step", "## Results\n\n| Item | Total |\n| --- | --- |\n| A | 12 |", "![Demo](assets/demo.mp4 \"50% right\")", "# First\n\n---\n\n## Second"]
            }
            SlideView {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                visible: ![3,9].includes(examples.currentIndex)
                example: visible ? examples.snippets[examples.currentIndex] : ""
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: exampleSource
                    readOnly: true
                    selectByMouse: true
                    text: examples.snippets[examples.currentIndex]
                    font.family: "monospace"
                    wrapMode: TextEdit.Wrap
                }
            }
            RowLayout {
                Button { text: qsTr("Copy Markdown"); onClicked: { exampleSource.selectAll(); exampleSource.copy(); exampleSource.deselect() } }
                Button { visible: ![6,10].includes(examples.currentIndex); text: qsTr("Insert example slide"); onClicked: { reference.close(); win.addSlide(exampleSource.text) } }
            }
        }
    }
    function addSlide(markdown) {
        if (!notesEditor.applyChanges()) return
        const current = backend.index
        const last = current + 1 === backend.count
        const position = last ? editor.length : backend.select(current + 1)
        const addition = last ? "\n\n---\n\n" + markdown + "\n" : markdown + "\n\n---\n\n"
        editor.forceActiveFocus()
        backend.replaceSelection(editor.textDocument, position, position, addition)
        win.navigate(current + 1)
    }
    Shortcut { sequence: "Ctrl+Shift+I"; enabled: win.editing; onActivated: if (notesEditor.applyChanges() && editor.allowsImport()) backend.choose("media") }
    Connections {
        target: backend
        function onInsertMediaLink(link) {
            if (!backend.editorProjection.allowsReplacement(editor.selectionStart, editor.selectionEnd, link)) return
            if (win.workspaceMode === 2) win.workspaceMode = 1
            editor.forceActiveFocus()
            editor.cursorPosition = backend.replaceSelection(editor.textDocument, editor.selectionStart, editor.selectionEnd, link)
        }
    }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: win.editing; onActivated: win.addSlide("## " + qsTr("New slide")) }
    Shortcut { sequence: "Ctrl+]"; enabled: win.editing; onActivated: win.navigate(backend.index + 1) }
    Shortcut { sequence: "Ctrl+["; enabled: win.editing; onActivated: win.navigate(backend.index - 1) }

    Connections {
        target: backend
        function onExternalConflict() { if (!win.presenting) conflict.open() }
    }
    LecternDialog {
        id: conflict
        width: Math.min(win.width - 48, 490)
        title: qsTr("This deck changed on disk")
        modal: true
        anchors.centerIn: parent
        contentItem: ColumnLayout {
            Label { text: qsTr("Reload the file to discard your edits, or keep editing this copy."); wrapMode: Text.Wrap; Layout.maximumWidth: 420 }
            RowLayout {
                Button { text: qsTr("Reload file"); onClicked: { conflict.close(); backend.reloadFromDisk() } }
                Button { text: qsTr("Keep editing"); onClicked: conflict.close() }
                Button { text: qsTr("Save As"); onClicked: { conflict.close(); if (notesEditor.applyChanges()) backend.choose("save") } }
            }
        }
    }
    LecternDialog {
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

    LecternDialog {
        id: help
        width: Math.min(win.width - 48, 650)
        height: Math.min(win.height - 48, 500)
        anchors.centerIn: parent
        title: qsTr("Lectern help")
        modal: true
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            TabBar {
                id: helpTabs
                Layout.fillWidth: true
                TabButton { text: qsTr("Getting started") }
                TabButton { text: qsTr("Keyboard shortcuts") }
            }
            ScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    text: helpTabs.currentIndex === 0
                        ? qsTr("Open a Markdown file or start a new deck. Separate slides with a line containing three hyphens (---). Choose a thumbnail to edit that slide. Use Write, Split view, or Preview to make room for the task at hand.") + "\n\n" +
                          qsTr("Add folders to the deck library to find talks again. Choose Theme to preview your current slide in each preset. Save the deck before dropping images or video into the editor so Lectern can copy them beside the Markdown file.") + "\n\n" +
                          qsTr("Edit speaker notes below the preview. Present sends slides to a second display when available. Presenter View keeps your notes, the next slide, and a timer close at hand. Export a PDF handout with notes for rehearsal or slide images for sharing.") + "\n\n" +
                          qsTr("For automation, run lectern-omarchy help for the editing commands, mcp for the stdio server, or skill install to install the bundled deck-authoring skill.")
                        : win.shortcutHelp
                }
            }
            Button { text: qsTr("Markdown reference…"); onClicked: { help.close(); reference.open() } }
        }
    }
    readonly property string shortcutHelp: [
        qsTr("Find a command") + "   Ctrl+Shift+P",
        qsTr("Write / Split / Preview") + "   Ctrl+1 / 2 / 3",
        qsTr("Previous / next slide") + "   Ctrl+[ / Ctrl+]",
        qsTr("Editor zoom") + "   Ctrl++ / Ctrl+- / Ctrl+0",
        ...commands.commands.filter(e => e[1]).map(e => e[0] + "   " + e[1]),
        stage.keysDescription
    ].join("\n")
    LecternDialog {
        id: checkDialog
        property var problems: []
        title: problems.length ? qsTr("Deck checks") : qsTr("Deck ready")
        modal: true
        anchors.centerIn: parent
        width: Math.min(win.width - 48, 540)
        height: Math.min(win.height - 48, 160 + problems.length * 42)
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent
            TextArea { readOnly: true; wrapMode: TextEdit.Wrap; text: checkDialog.problems.length ? checkDialog.problems.join("\n") : qsTr("All slides passed the checks.") }
        }
    }

    LecternDialog {
        id: updates
        objectName: "updates"
        anchors.centerIn: parent
        width: Math.min(win.width - 40, 520)
        title: qsTr("Lectern updates")
        modal: true
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: updater.status
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            RowLayout {
                Button {
                    text: qsTr("Install update")
                    visible: updater.canInstall
                    enabled: !updater.busy
                    onClicked: updater.install()
                }
                Button {
                    text: qsTr("View release")
                    visible: updater.available
                    enabled: !updater.busy
                    onClicked: updater.openRelease()
                }
            }
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
            Layout.preferredHeight: 52
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
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: backend.title || qsTr("Untitled deck")
                    textFormat: Text.PlainText
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Button { flat: true; text: qsTr("New"); onClicked: win.guard(() => backend.newDeck()) }
                Button { flat: true; text: qsTr("Open"); onClicked: win.guard(() => backend.choose("open")) }
                Button {
                    id: fileButton
                    text: qsTr("File")
                    onClicked: fileMenu.open()
                    Menu {
                        id: fileMenu
                        MenuItem { text: qsTr("Deck library…"); onTriggered: library.open() }
                        MenuItem { text: qsTr("Markdown reference…"); onTriggered: reference.open() }
                        MenuItem { text: qsTr("Open example"); onTriggered: win.guard(() => backend.sample()) }
                        MenuItem {
                            objectName: "checkUpdates"
                            text: qsTr("Updates")
                            onTriggered: { updates.open(); updater.check() }
                        }
                        y: fileButton.height
                        MenuItem { text: qsTr("Save As"); onTriggered: if (notesEditor.applyChanges()) backend.choose("save") }
                        MenuItem { text: qsTr("Export slide images…"); onTriggered: backend.choose("png") }
                        MenuItem { text: qsTr("Check deck…"); onTriggered: { checkDialog.problems = backend.validate(); checkDialog.open() } }
                        MenuItem { text: qsTr("Keep screen awake when presenting"); checkable: true; checked: backend.keepAwake; onTriggered: backend.keepAwake = checked }
                        MenuItem { text: qsTr("Export PDF"); onTriggered: backend.choose("pdf") }
                        MenuItem {
                            text: qsTr("Export PDF with notes")
                            onTriggered: if (notesEditor.applyChanges()) backend.choose("pdf-notes")
                        }
                    }
                }
                Button { text: qsTr("Save"); onClicked: if (notesEditor.applyChanges()) backend.save() }
                Button {
                    text: qsTr("Present")
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: theme.accentForeground
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitWidth: 90
                        implicitHeight: 32
                        radius: theme.radius
                        color: theme.accent
                        opacity: parent.down ? 0.8 : 1
                        border.width: parent.visualFocus ? 2 : 0
                        border.color: theme.accentForeground
                    }
                    highlighted: true
                    Material.background: theme.accent
                    Material.foreground: theme.accentForeground
                    onClicked: win.togglePresent()
                }
                Button { flat: true; text: qsTr("Presenter View"); onClicked: win.startPresentation(true) }
                Button { text: "?"; Accessible.name: qsTr("Keyboard shortcuts"); onClicked: help.open() }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            spacing: 10
            Label { text: qsTr("Slide font"); color: win.muted }
            ComboBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 220
                Layout.preferredHeight: 32
                objectName: "slideFontPicker"
                model: [qsTr("Default"), ...Qt.fontFamilies()]
                displayText: backend.slideFont || qsTr("Default")
                currentIndex: Math.max(0, model.indexOf(backend.slideFont))
                Accessible.name: qsTr("Slide font")
                onActivated: index => win.changeAppearance("font", index ? model[index] : "")
            }
            Label { text: qsTr("Size"); color: win.muted }
            SpinBox {
                Layout.preferredHeight: 32
                from: 12
                to: 96
                value: backend.slideSize
                Accessible.name: qsTr("Slide font size")
                onValueModified: win.changeAppearance("font-size", String(value))
            }
            Button {
                text: qsTr("Theme") + " · " + gallery.labels[gallery.names.indexOf(backend.slideTheme)]
                Accessible.name: qsTr("Choose a slide theme")
                onClicked: gallery.open()
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                Layout.preferredHeight: 32
                Accessible.name: qsTr("Workspace view")
                model: [qsTr("Write"), qsTr("Split view"), qsTr("Preview")]
                currentIndex: win.workspaceMode
                displayText: model[win.workspaceMode]
                onActivated: index => {
                    if (notesEditor.applyChanges())
                        win.workspaceMode = index
                }
            }
        }

        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 0

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                handle: Rectangle {
                    implicitWidth: 6
                    color: SplitHandle.hovered || SplitHandle.pressed ? theme.accent : win.line
                }
                ColumnLayout {
                    visible: win.workspaceMode !== 2
                    SplitView.fillWidth: win.workspaceMode === 0
                    SplitView.preferredWidth: win.width * 0.41
                    SplitView.minimumWidth: 200
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        Layout.leftMargin: 15
                        Layout.rightMargin: 10
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("Markdown")
                            font.weight: Font.DemiBold
                            color: win.muted
                        }
                        EditButton {
                            text: qsTr("Commands")
                            implicitWidth: Math.max(72, implicitContentWidth + 16)
                            hint: qsTr("Find a command (Ctrl+Shift+P)")
                            onClicked: commands.open()
                        }
                        EditButton {
                            text: "↶"
                            hint: qsTr("Undo")
                            enabled: editor.canUndo
                            onClicked: { editor.forceActiveFocus(); editor.undo() }
                        }
                        EditButton {
                            text: "↷"
                            hint: qsTr("Redo")
                            enabled: editor.canRedo
                            onClicked: { editor.forceActiveFocus(); editor.redo() }
                        }
                        EditButton {
                            text: "H1"
                            hint: qsTr("Heading")
                            onClicked: win.format(win.formats[4])
                        }
                        EditButton {
                            text: "•"
                            hint: qsTr("Bullet list")
                            onClicked: win.format(win.formats[7])
                        }
                    }
                    SourceEditor {
                        id: editor
                        objectName: "editor"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        hideNotes: viewSettings.hideEditorNotes
                        applyingNotes: notesEditor.applying
                        zoom: win.editorZoom
                    }
                }

                ColumnLayout {
                    visible: win.workspaceMode !== 0
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
                        visible: win.notesVisible
                        Layout.preferredHeight: 88
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
                                    id: notesEditor
                                    objectName: "notesEditor"
                                    property int slideIndex: 0
                                    property string storedNotes: ""
                                    property bool applying: false

                                    function applyChanges() {
                                        if (applying || text === storedNotes)
                                            return true
                                        applying = true
                                        const saved = backend.editNotes(slideIndex, text, editor.textDocument, editor.cursorPosition)
                                        applying = false
                                        if (saved)
                                            refresh()
                                        return saved
                                    }

                                    function refresh() {
                                        slideIndex = backend.index
                                        storedNotes = backend.notes
                                        if (text !== storedNotes)
                                            text = storedNotes
                                        backend.setNotesPending(false)
                                    }

                                    Component.onCompleted: refresh()
                                    onTextChanged: backend.setNotesPending(text !== storedNotes)
                                    onActiveFocusChanged: {
                                        if (!activeFocus && !applyChanges())
                                            Qt.callLater(() => notesEditor.forceActiveFocus())
                                    }
                                    Connections {
                                        target: backend
                                        function onChanged() {
                                            if (!notesEditor.applying && notesEditor.text === notesEditor.storedNotes)
                                                notesEditor.refresh()
                                        }
                                    }

                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    padding: 0
                                    background: null
                                    textFormat: TextEdit.PlainText
                                    placeholderText: qsTr("Add speaker notes for this slide.")
                                    Keys.onEscapePressed: event => {
                                        refresh()
                                        editor.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                    ListView {
                        id: rail
                        Layout.fillWidth: true
                        Layout.preferredHeight: 101
                        orientation: ListView.Horizontal
                        leftMargin: 12
                        rightMargin: 12
                        model: backend.headings
                        currentIndex: backend.index
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        clip: true
                        spacing: 4
                        delegate: ItemDelegate {
                            required property int index
                            required property string modelData
                            width: 70 * backend.slideWidth / 720 + 12
                            height: rail.height
                            Accessible.name: modelData || qsTr("Empty slide")
                            highlighted: backend.index === index
                            onClicked: win.navigate(index)
                            background: Rectangle {
                                radius: theme.radius
                                color: parent.highlighted ? "#2a3040" : parent.hovered ? "#222631" : "transparent"
                            }
                            contentItem: ColumnLayout {
                                spacing: 3
                                SlideView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    deck: backend
                                    slideIndex: index
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: (index + 1).toLocaleString(Qt.locale(translator.language), 'f', 0) + "   " + (modelData || qsTr("Empty slide"))
                                    color: backend.index === index ? theme.accent : win.muted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
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
            visible: backend.status !== "" || backend.dirty
            padding: 6
            text: backend.status || (backend.dirty ? qsTr("Unsaved changes") : backend.title || qsTr("Untitled deck"))
            elide: Text.ElideRight
            color: win.muted
            textFormat: Text.PlainText
        }
    }

    Window {
        id: audience
        objectName: "audienceWindow"
        title: qsTr("Audience Slides")
        transientParent: null
        color: "black"
        onClosing: win.stopPresentation()
        onActiveChanged: if (active && win.dualPresentation) stage.forceActiveFocus()
    }

    PresenterView {
        id: presenter
        objectName: "presenterView"
        anchors.fill: parent
        visible: win.presenting && win.presenterMode && !win.singleAudience
        hasAudienceDisplay: win.dualPresentation
        blankColor: stage.blankColor
        jumpDigits: stage.jumpDigits
        showKeys: stage.showKeys
        keysDescription: stage.keysDescription
        onNavigate: index => {
            stage.jumpDigits = ""
            win.navigate(index)
        }
        onExitRequested: win.stopPresentation()
        onShowSlidesRequested: win.togglePresenter()
        Keys.onPressed: event => stage.handleKey(event)
    }

    Rectangle {
        id: stage
        objectName: "presentationStage"
        parent: win.dualPresentation ? audience.contentItem : win.contentItem
        anchors.fill: parent
        color: "black"
        visible: win.presenting && (!win.presenterMode || win.dualPresentation || win.singleAudience)
        Material.theme: Material.Dark
        Material.accent: theme.accent
        property string jumpDigits: ""
        property string blankColor: ""
        property bool showKeys: false
        readonly property string keysDescription: qsTr("Next   → / ↓ / Space / Enter\nPrevious   ← / ↑\nFirst / last   Home / End\nJump to slide   Number + Enter\nBlank screen   B / W\nShortcuts   ?\nExit presentation   Esc")

        function resetControls() {
            jumpDigits = ""
            blankColor = ""
            showKeys = false
        }

        function advance() {
            jumpDigits = ""
            win.navigate(backend.index + 1)
        }

        SlideView {
            id: projected
            width: parent.width
            height: parent.height
            deck: backend
            visible: stage.blankColor === ""
            property int current: backend.index
            property int previous: 0
            onCurrentChanged: {
                entrance.stop()
                if (stage.visible && visible && backend.transition !== "none") {
                    x = backend.transition === "slide" ? width * (current < previous ? -1 : 1) : 0
                    opacity = backend.transition === "fade" ? 0 : 1
                    entrance.start()
                } else { x = 0; opacity = 1 }
                previous = current
            }
            ParallelAnimation {
                id: entrance
                NumberAnimation { target: projected; property: "opacity"; to: 1; duration: 200 }
                NumberAnimation { target: projected; property: "x"; to: 0; duration: 280; easing.type: Easing.OutCubic }
            }
        }
        Item {
            id: playback
            objectName: "videoLayer"
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * backend.slideWidth / 720)
            height: width * 720 / backend.slideWidth
            clip: true
            visible: stage.visible && stage.blankColor === ""
            readonly property bool activeStage: stage.visible
            property var clips: []
            property int loadedIndex: -1
            function refresh() {
                const next = activeStage ? backend.videos() : []
                if (loadedIndex !== backend.index || JSON.stringify(next) !== JSON.stringify(clips)) {
                    loadedIndex = backend.index
                    clips = next
                }
            }
            onActiveStageChanged: refresh()
            Connections {
                target: backend
                function onChanged() { if (playback.activeStage) playback.refresh() }
            }
            Repeater {
                model: playback.clips
                delegate: Video {
                    objectName: "audienceVideo"
                    readonly property bool shouldPlay: playback.visible
                    onShouldPlayChanged: { if (shouldPlay) play(); else pause() }
                    required property var modelData
                    x: modelData.left * playback.width / backend.slideWidth
                    y: modelData.top * playback.height / 720
                    width: modelData.width * playback.width / backend.slideWidth
                    height: modelData.height * playback.height / 720
                    source: modelData.url
                    Component.onCompleted: if (shouldPlay) play()
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            height: 3
            width: parent.width * (backend.index + 1) / Math.max(1, backend.count)
            color: theme.accent
        }
        Rectangle {
            anchors.fill: parent
            visible: stage.blankColor !== ""
            color: stage.blankColor || "black"
            Accessible.name: stage.blankColor === "white" ? qsTr("White screen") : qsTr("Black screen")
        }

        Rectangle {
            anchors.centerIn: parent
            visible: stage.showKeys
            radius: theme.radius
            color: win.panel
            width: Math.min(stage.width - 24, presentationKeys.implicitWidth + 44)
            height: presentationKeys.implicitHeight + 36
            Label {
                id: presentationKeys
                anchors.fill: parent
                anchors.margins: 18
                wrapMode: Text.Wrap
                text: stage.keysDescription
                lineHeight: 1.5
            }
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            padding: 12
            visible: stage.jumpDigits !== "" && stage.blankColor === ""
            text: qsTr("Slide %1").arg(stage.jumpDigits)
            background: Rectangle { color: win.panel; radius: theme.radius }
        }

        TapHandler {
            onTapped: {
                if (stage.showKeys)
                    stage.showKeys = false
                else
                    stage.advance()
            }
        }
        Keys.onPressed: event => handleKey(event)

        function handleKey(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (event.modifiers === Qt.ControlModifier) {
                    win.stopPresentation()
                    event.accepted = true
                    return
                }
                if (event.modifiers === (Qt.ControlModifier | Qt.AltModifier)) {
                    win.togglePresenter()
                    event.accepted = true
                    return
                }
            }
            if (showKeys) {
                if (!event.isAutoRepeat && (event.key === Qt.Key_Escape || event.key === Qt.Key_Question))
                    showKeys = false
                event.accepted = true
                return
            }
            if (!(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                    && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                if (jumpDigits.length < 9 && !event.isAutoRepeat)
                    jumpDigits += String(event.key - Qt.Key_0)
                event.accepted = true
                return
            }

            switch (event.key) {
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (jumpDigits !== "") {
                    const target = Number(jumpDigits)
                    jumpDigits = ""
                    if (target >= 1 && target <= backend.count)
                        win.navigate(target - 1)
                    break
                }
                stage.advance()
                break
            case Qt.Key_Right:
            case Qt.Key_Down:
            case Qt.Key_Space:
                stage.advance()
                break
            case Qt.Key_Left:
            case Qt.Key_Up:
                jumpDigits = ""
                win.navigate(backend.index - 1)
                break
            case Qt.Key_Home:
            case Qt.Key_End:
                jumpDigits = ""
                win.navigate(event.key === Qt.Key_Home ? 0 : backend.count - 1)
                break
            case Qt.Key_B:
            case Qt.Key_W:
                if (!event.isAutoRepeat) {
                    const nextColor = event.key === Qt.Key_B ? "black" : "white"
                    blankColor = blankColor === nextColor ? "" : nextColor
                    showKeys = false
                }
                break
            case Qt.Key_Question:
                if (!event.isAutoRepeat && blankColor === "")
                    showKeys = !showKeys
                break
            case Qt.Key_Backspace:
                if (jumpDigits === "")
                    return
                jumpDigits = jumpDigits.slice(0, -1)
                break
            case Qt.Key_Escape:
                if (jumpDigits !== "")
                    jumpDigits = ""
                else
                    win.stopPresentation()
                break
            default:
                return
            }
            event.accepted = true
        }
    }
}
