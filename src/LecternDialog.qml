import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Dialog {
    id: dialog
    focus: true
    header: Label {
        visible: dialog.title.length > 0
        text: dialog.title
        color: "#edf0f5"
        font.pixelSize: 20
        font.weight: Font.DemiBold
        leftPadding: 18
        rightPadding: 18
        topPadding: 16
        bottomPadding: 8
        background: Rectangle { color: "transparent" }
    }
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.foreground: "#edf0f5"
    Material.roundedScale: theme.radius
    Overlay.modal: Rectangle { color: "#a0000000" }
    background: Rectangle {
        color: "#1b1e25"
        radius: theme.radius
        border.color: "#3c4350"
    }
}
