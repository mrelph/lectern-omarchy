import QtQuick.Controls

TextField {
    color: theme.colors.text
    selectionColor: theme.colors.selection
    selectedTextColor: theme.colors.onSelection
    placeholderTextColor: activeFocus ? theme.colors.focus : theme.colors.muted
}
