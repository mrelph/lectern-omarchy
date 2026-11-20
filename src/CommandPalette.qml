import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

LecternDialog {
    id: palette
    property var commands: []
    readonly property var matches: commands.filter(entry => entry[0].toLocaleLowerCase().includes(search.text.trim().toLocaleLowerCase()))
    modal: true
    padding: 16
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 540)
    height: Math.min(parent.height - 60, 430)
    onOpened: { search.clear(); search.forceActiveFocus(); results.currentIndex = 0 }
    function execute(index) {
        const entry = matches[index]
        if (!entry) return
        close()
        entry[2]()
    }
    contentItem: ColumnLayout {
        spacing: 8
        AppTextField {
            id: search
            objectName: "commandSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Find a command…")
            onTextChanged: results.currentIndex = 0
            onAccepted: palette.execute(Math.max(0, results.currentIndex))
            Keys.onDownPressed: results.incrementCurrentIndex()
            Keys.onUpPressed: results.decrementCurrentIndex()
        }
        ListView {
            id: results
            objectName: "commandResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: palette.matches
            delegate: ItemDelegate {
                id: resultRow
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                Accessible.name: modelData[0]
                background: Rectangle {
                    color: parent.highlighted ? theme.colors.selection : parent.hovered ? theme.colors.canvas : "transparent"
                    border.width: parent.visualFocus ? 2 : 0
                    border.color: parent.highlighted ? theme.colors.onSelection : theme.colors.focus
                }
                contentItem: RowLayout {
                    Label { Layout.fillWidth: true; color: resultRow.highlighted ? theme.colors.onSelection : theme.colors.text; text: modelData[0]; elide: Text.ElideRight }
                    Label { text: modelData[1]; color: resultRow.highlighted ? theme.colors.onSelection : theme.colors.muted }
                }
                onClicked: palette.execute(index)
            }
            Label { anchors.centerIn: parent; visible: !results.count; text: qsTr("No matching commands") }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
