import QtQuick
import QtQuick.Controls

Row {
    id: bar
    required property var formats
    signal formatRequested(int index)
    signal mediaRequested()

    spacing: 2
    topPadding: 4
    bottomPadding: 4
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Formatting")

    component FormatAction: GlyphButton {
        required property int commandIndex
        objectName: "formatAction" + commandIndex
        hint: bar.formats[commandIndex][0]
        chord: bar.formats[commandIndex][1]
        // Mouse formatting keeps the editor selection. Tab still reaches every action.
        focusPolicy: Qt.TabFocus
        onClicked: bar.formatRequested(commandIndex)
    }

    component Divider: Item {
        implicitWidth: 11
        implicitHeight: 30
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 16
            color: theme.colors.line
        }
    }

    FormatAction { commandIndex: 0; symbol: "qrc:/icons/format/bold.svg" }
    FormatAction { commandIndex: 1; symbol: "qrc:/icons/format/italic.svg" }
    FormatAction { commandIndex: 2; symbol: "qrc:/icons/format/inline-code.svg" }
    FormatAction { commandIndex: 3; symbol: "qrc:/icons/format/link.svg" }
    Divider {}
    FormatAction { commandIndex: 4; mark: "H1" }
    FormatAction { commandIndex: 5; mark: "H2" }
    FormatAction { commandIndex: 6; mark: "H3" }
    Divider {}
    FormatAction { commandIndex: 7; symbol: "qrc:/icons/format/bulleted-list.svg" }
    FormatAction { commandIndex: 8; symbol: "qrc:/icons/format/numbered-list.svg" }
    FormatAction { commandIndex: 9; symbol: "qrc:/icons/format/quote.svg" }
    Divider {}
    FormatAction { commandIndex: 10; symbol: "qrc:/icons/format/code-block.svg" }
    FormatAction { commandIndex: 11; symbol: "qrc:/icons/format/table.svg" }
    Divider {}
    FormatAction { commandIndex: 12; symbol: "qrc:/icons/format/slide-break.svg" }
    Divider {}
    GlyphButton {
        symbol: "qrc:/icons/format/media.svg"
        hint: qsTr("Insert image or video…")
        chord: "Ctrl+Shift+I"
        focusPolicy: Qt.TabFocus
        onClicked: bar.mediaRequested()
    }
}
