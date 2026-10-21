import QtQuick

// What is on, the way a phone's status bar shows it (Settings → Status
// Icons): the deck's pixel symbols (PixelIcon) in a row, as Main.qml's
// statusIcons lists them: the modules' (the Arcade's HI for Save High
// Scores, a trophy for RetroAchievements, a globe for Online Play, a network
// for LAN Play), then Bluetooth's rune while it is on, a dot either side
// while a device is connected, and Wi-Fi's bars while it is on, their feet
// alone while it has joined no network. The title bar has them at its right
// end (AppBar), the Arcade screen at the right of its help line (with none,
// of its hint bar).
Row {
    id: iconRow

    property color color: root.surfaceColor

    visible: root.statusIcons.length > 0
    spacing: 3 * root.px

    // By their count, so a list made again as it was draws nothing anew.
    Repeater {
        model: root.statusIcons.length
        PixelIcon {
            required property int index
            name: root.statusIcons[index] || ""
            color: iconRow.color
        }
    }
}
