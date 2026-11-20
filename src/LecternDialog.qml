import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Dialog {
    id: dialog
    focus: true
    header: Label {
        visible: dialog.title.length > 0
        text: dialog.title
        color: theme.colors.text
        font.pixelSize: 20
        font.weight: Font.DemiBold
        leftPadding: 18
        rightPadding: 18
        topPadding: 16
        bottomPadding: 8
        background: Rectangle { color: "transparent" }
    }
    Material.theme: theme.colors.dark ? Material.Dark : Material.Light
    Material.accent: theme.colors.focus
    Material.background: theme.colors.panel
    Material.foreground: theme.colors.text
    Material.roundedScale: theme.radius
    Overlay.modal: Rectangle { color: "#a0000000" }
    background: Rectangle {
        color: theme.colors.panel
        radius: theme.radius
        border.color: theme.colors.line
    }
}
