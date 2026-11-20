import QtQuick
import QtQuick.Controls

Popup {
    id: panel
    required property Item trigger
    parent: trigger
    x: Math.min(0, trigger.width - width)
    y: trigger.height + 5
    margins: 8
    padding: 15
    focus: true
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Item {}
    background: Rectangle {
        radius: theme.radius
        color: theme.colors.panel
        border.color: theme.colors.line
    }
}
