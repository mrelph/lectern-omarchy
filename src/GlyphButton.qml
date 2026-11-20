import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolButton {
    id: button
    required property string hint
    property string chord
    property url symbol
    property string mark
    property bool selectable: false
    property bool selected: false
    property bool primary: false
    property color ink: primary ? theme.colors.onAccent
                                : selected || hovered || visualFocus ? theme.colors.focus : theme.colors.text
    implicitWidth: 30
    implicitHeight: 30
    padding: 0
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    focusPolicy: Qt.TabFocus
    text: mark
    display: mark ? AbstractButton.TextOnly : AbstractButton.IconOnly
    font.pixelSize: 13
    font.weight: Font.DemiBold
    icon.source: symbol
    icon.width: 17
    icon.height: 17
    icon.color: ink
    Material.foreground: ink
    opacity: enabled ? 1 : 0.38
    Accessible.name: hint
    Accessible.description: chord
    Accessible.checkable: selectable || checkable
    Accessible.checked: selected || checked
    ToolTip.text: chord ? hint + "  ·  " + chord : hint
    ToolTip.visible: enabled && (hovered || visualFocus)
    ToolTip.delay: 450
    background: Rectangle {
        radius: Math.min(6, theme.radius)
        color: button.primary ? theme.accent : button.selected ? theme.colors.reader
             : button.down || button.hovered ? theme.colors.canvas : "transparent"
        border.width: button.visualFocus || button.selected ? 1 : 0
        border.color: button.visualFocus ? theme.colors.focus : theme.colors.line
    }
}
