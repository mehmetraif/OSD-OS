import QtQuick

// One row of a settings page, Settings' or a module's: a MenuRow, "DISPLAY······ON",
// and for a slider the deck's tape bar under it, as the deck's shows how far
// the tape is: ◄ ► move the ▼ between the two ends, and stand beyond them.
// The bar takes whole lines under its own, so every line keeps to the list's
// rule as it scrolls; while the bar isn't shown (a slider that is off, or
// dimmed) its lines stay, empty. In a monitor layout's frame (Settings → Look
// → Menu Layout) a slider is its line alone, its value a number ("60%", the
// host's), and the line is drawn the layout's way (MenuRow's MonitorRow):
// every row one line.
//
//     delegate: SettingRow {
//         width: list.width
//         label: …; value: …; selected: list.currentIndex === index
//         slider: modelData.type === "slider"; barValue: modelData.value / 100
//     }
Item {
    id: settingRow

    // The line's, as MenuRow has them.
    property string label: ""
    property string value: ""
    property bool selected: false
    property bool heading: false
    property bool opens: false
    property bool changes: false
    property bool dimmed: false
    // Its value being changed in two steps (Broadcast's, Flat Panel's).
    property bool editing: false

    // A slider: its bar at barValue (0 to 1) between startText and
    // endText, shown while barShown.
    property bool slider: false
    property real barValue: 0
    property string startText: ""
    property string endText: ""
    property bool barShown: true

    // In a monitor layout's frame, where a slider is its line alone.
    readonly property bool monitor: root.monitorFramed === true
    readonly property real lineHeight: root.sh * 0.0583333 //28
    readonly property int barLines: slider && !monitor ? Math.ceil((tape.height + 2 * root.px) / lineHeight) : 0
    height: lineHeight * (1 + barLines)

    MenuRow {
        width: parent.width
        height: settingRow.lineHeight
        label: settingRow.label
        value: settingRow.value
        selected: settingRow.selected
        heading: settingRow.heading
        opens: settingRow.opens
        changes: settingRow.changes
        dimmed: settingRow.dimmed
        editing: settingRow.editing
    }

    Loader {
        id: tape
        active: settingRow.slider && !settingRow.monitor
        visible: settingRow.barShown
        x: root.sw * 0.009375 //6
        y: settingRow.lineHeight + Math.round((settingRow.barLines * settingRow.lineHeight - height) / 2)
        width: parent.width - 2 * x
        sourceComponent: OsdTapeBar {
            changes: true
            fontSize: root.sh * 0.0333333 //16
            value: settingRow.barValue
            startText: settingRow.startText
            endText: settingRow.endText
        }
    }
}
