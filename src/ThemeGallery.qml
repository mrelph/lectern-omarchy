import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lectern

LecternDialog {
    id: gallery
    readonly property var names: ["auto", "light", "dark", "high-contrast", "warm-paper", "bold-color", "minimal-mono", "blocks-light", "blocks-dark"]
    readonly property var labels: [qsTr("System"), qsTr("Light"), qsTr("Dark"), qsTr("High contrast"), qsTr("Warm paper"), qsTr("Bold color"), qsTr("Minimal mono"), qsTr("Blocks light"), qsTr("Blocks dark")]
    signal chosen(string name, bool currentOnly)
    title: qsTr("Choose a theme")
    anchors.centerIn: parent
    modal: true
    width: Math.min(760, parent.width - 48)
    height: Math.min(620, parent.height - 48)
    contentItem: ColumnLayout {
        RowLayout {
            ComboBox { id: scope; implicitContentWidthPolicy: ComboBox.WidestText; model: [qsTr("Whole deck"), qsTr("Current slide")] }
            Button {
                visible: scope.currentIndex === 1
                text: qsTr("Use deck theme")
                onClicked: gallery.chosen("deck", true)
            }
        }
        GridView {
            id: grid
            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true
            cellWidth: width / 3
            cellHeight: Math.min(height / 3, cellWidth * .56 + 38)
            model: gallery.visible ? gallery.names : []
            delegate: ItemDelegate {
                required property int index
                required property string modelData
                width: grid.cellWidth - 4
                height: grid.cellHeight - 4
                Accessible.name: gallery.labels[index]
                contentItem: ColumnLayout {
                    SlideView { Layout.fillWidth: true; Layout.fillHeight: true; deck: backend; previewTheme: modelData }
                    Label { text: gallery.labels[index]; color: modelData === backend.slideTheme ? theme.accent : "#edf0f5" }
                }
                onClicked: gallery.chosen(modelData, scope.currentIndex === 1)
            }
        }
    }
}
