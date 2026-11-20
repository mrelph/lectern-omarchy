import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lectern

FocusScope {
    id: presenter
    required property bool hasAudienceDisplay
    required property string blankColor
    required property string jumpDigits
    required property bool showKeys
    required property string keysDescription
    signal navigate(int index)
    signal exitRequested()
    signal showSlidesRequested()

    property int elapsed: 0
    property date now: new Date()
    readonly property color panel: theme.colors.panel
    readonly property color line: theme.colors.line
    readonly property color muted: theme.colors.muted

    function refreshClock() {
        now = new Date()
        elapsed = backend.presentationSeconds()
    }

    function resetTimer() {
        backend.resetPresentationTimer()
        refreshClock()
    }

    function elapsedLabel(seconds, locale) {
        function digits(number) {
            const value = number.toLocaleString(locale, "f", 0)
            return number < 10 ? (0).toLocaleString(locale, "f", 0) + value : value
        }
        const minutes = Math.floor(seconds / 60)
        const remainder = digits(seconds % 60)
        if (minutes >= 60)
            return digits(Math.floor(minutes / 60)) + ":" + digits(minutes % 60) + ":" + remainder
        return digits(minutes) + ":" + remainder
    }

    Timer {
        interval: 250
        running: presenter.visible
        triggeredOnStart: true
        repeat: true
        onTriggered: presenter.refreshClock()
    }

    component SlideTile: ColumnLayout {
        required property string heading
        required property int index
        spacing: 8

        Label {
            text: parent.heading
            color: presenter.muted
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: frame
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius
            color: presenter.panel
            border.color: presenter.line
            SlideView {
                anchors.fill: parent
                anchors.margins: 2
                deck: backend
                slideIndex: frame.parent.index
                visible: slideIndex >= 0
            }
            Label {
                anchors.centerIn: parent
                visible: frame.parent.index < 0
                color: presenter.muted
                text: qsTr("End of deck")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: presenter.elapsedLabel(presenter.elapsed, Qt.locale(translator.language))
                font.family: "monospace"
                font.pixelSize: 28
                color: theme.colors.focus
                Accessible.name: qsTr("Elapsed time: %1").arg(text)
            }
            Button { text: qsTr("Reset timer"); flat: true; onClicked: presenter.resetTimer() }
            Label {
                text: presenter.now.toLocaleTimeString(Qt.locale(translator.language), Locale.ShortFormat)
                color: presenter.muted
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Previous"); enabled: backend.index > 0; onClicked: presenter.navigate(backend.index - 1) }
            Button { text: qsTr("Next"); enabled: backend.index + 1 < backend.count; onClicked: presenter.navigate(backend.index + 1) }
            Button { text: qsTr("Show slides"); visible: !presenter.hasAudienceDisplay; onClicked: presenter.showSlidesRequested() }
            Button { text: qsTr("End"); onClicked: presenter.exitRequested() }
        }

        Label {
            Layout.fillWidth: true
            visible: presenter.blankColor !== "" || presenter.jumpDigits !== ""
            padding: 8
            text: presenter.blankColor === "black" ? qsTr("Audience screen is black") :
                  presenter.blankColor === "white" ? qsTr("Audience screen is white") :
                  qsTr("Slide %1").arg(presenter.jumpDigits)
            color: theme.colors.focus
            background: Rectangle { color: presenter.panel; radius: theme.radius }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(150, presenter.height * 0.44)
            Layout.maximumHeight: Layout.preferredHeight
            spacing: 12
            SlideTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                objectName: "currentPresenterSlide"
                index: backend.index
                heading: qsTr("Current · %1 of %2")
                    .arg((backend.index + 1).toLocaleString(Qt.locale(translator.language), "f", 0))
                    .arg(backend.count.toLocaleString(Qt.locale(translator.language), "f", 0))
            }
            SlideTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                objectName: "nextPresenterSlide"
                index: backend.index + 1 < backend.count ? backend.index + 1 : -1
                heading: qsTr("Next")
            }
        }

        Label {
            text: qsTr("Speaker notes")
            color: presenter.muted
            font.weight: Font.DemiBold
        }

        ScrollView {
            id: noteScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            background: Rectangle { color: presenter.panel; radius: theme.radius }
            Label {
                objectName: "presenterNotes"
                width: noteScroll.availableWidth
                padding: 12
                font.pixelSize: 23
                lineHeight: 1.22
                text: backend.notes || qsTr("No notes for this slide.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Connections {
                target: backend
                function onChanged() {
                    if (noteScroll.contentItem.contentY > 0)
                        noteScroll.contentItem.contentY = 0
                }
            }
        }
    }

    Rectangle {
        visible: presenter.showKeys
        anchors.centerIn: parent
        width: Math.min(presenter.width - 24, shortcutText.implicitWidth + 40)
        height: shortcutText.implicitHeight + 36
        radius: theme.radius
        color: presenter.panel
        border.color: theme.colors.focus
        Label {
            id: shortcutText
            anchors.fill: parent
            anchors.margins: 18
            text: presenter.keysDescription
            lineHeight: 1.5
        }
    }
}
