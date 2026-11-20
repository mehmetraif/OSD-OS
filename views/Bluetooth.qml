import QtQuick
import Components

// Settings → Network → Bluetooth: pairs a Bluetooth keyboard, gamepad or remote with the
// Pi, through bluetoothManager (BlueZ). Laid out like Settings: BLUETOOTH on
// or off, SEARCH, then PAIRED and FOUND devices. SEARCH looks for devices for a
// minute, listing what answers as it does; select on one found pairs it, and a
// keyboard then has a code to type on it, which comes up over the list.
// Select on a paired one offers to connect, disconnect or forget it.
FocusScope {
    id: bluetoothRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal goBack()

    property var navParams: ({})

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Update.qml).
    readonly property bool available: bluetoothManager ? bluetoothManager.available : false
    readonly property bool powered: bluetoothManager ? bluetoothManager.powered : false
    readonly property bool searching: bluetoothManager ? bluetoothManager.searching : false
    readonly property var devices: bluetoothManager ? bluetoothManager.devices : []
    readonly property var prompt: bluetoothManager ? bluetoothManager.prompt : ({})
    readonly property string message: bluetoothManager ? bluetoothManager.message : ""
    readonly property bool powerFailed: bluetoothManager ? bluetoothManager.powerFailed : false

    // The lines: { type: "power" | "details" | "search" | "section" | "note" | "device", … }.
    property var rows: []
    // The lines the list is made of, given it anew only as lines come, go or
    // change places: one whose name or value changes (a device connecting,
    // a search) is changed where it is, from rows, so a device's every
    // property doesn't make every line again.
    property var shownRows: []
    // A device whose options are open.
    property var optionsDevice: null
    // The dots after SEARCHING, one more each half second.
    property int dots: 1

    onAvailableChanged: buildRows()
    onPoweredChanged: buildRows()
    onSearchingChanged: buildRows()
    onPowerFailedChanged: buildRows()
    onDevicesChanged: buildRows()
    Component.onCompleted: buildRows()

    // Leaving the page ends what it started: nothing would show a pairing's
    // code any more, and a search keeps the radio busy.
    Component.onDestruction: {
        if (bluetoothManager) {
            bluetoothManager.stopSearch()
            bluetoothManager.cancelPairing()
            bluetoothManager.clearMessage()
        }
    }

    function deviceValue(d) {
        if (d.busy === "pairing")       return "Pairing"
        if (d.busy === "connecting")    return "Connecting"
        if (d.busy === "disconnecting") return "Disconnecting"
        if (d.paired)                   return d.connected ? "Connected" : "Paired"
        return d.kind || "New"
    }

    function buildRows() {
        // The cursor stays on its line as lines come and go: a device by its
        // path (it moves from FOUND to PAIRED once paired), the others by type.
        var at = list.currentIndex
        var current = rows[at]
        var items = []
        if (!available) {
            items.push({ type: "note", label: "No Bluetooth",
                         description: "Bluetooth isn't running, or this computer has none" })
        } else {
            items.push({ type: "power", label: "Bluetooth", value: powered ? "On" : "Off",
                         description: "Turns Bluetooth on or off" })
            // It wouldn't turn on: what the system says about it.
            if (powerFailed && !powered)
                items.push({ type: "details", label: "Details",
                             description: "What the system says about Bluetooth. A photo of it helps find what is wrong" })
            items.push({ type: "search", label: "Search",
                         value: searching ? "Searching" : "", busy: searching,
                         description: searching
                             ? "Looking for devices for a minute. Select stops it"
                             : "Put the device in pairing mode (see its manual), then select SEARCH. Devices found are listed below" })
            var paired = devices.filter(function(d) { return d.paired })
            var found = devices.filter(function(d) { return !d.paired })
            if (paired.length > 0) {
                items.push({ type: "section", label: "Paired" })
                for (var i = 0; i < paired.length; i++)
                    items.push({ type: "device", label: paired[i].name, value: deviceValue(paired[i]),
                                 device: paired[i], busy: paired[i].busy !== "",
                                 description: "Select to connect, disconnect or forget it" })
            }
            if (found.length > 0 || searching) {
                items.push({ type: "section", label: "Found" })
                for (var j = 0; j < found.length; j++)
                    items.push({ type: "device", label: found[j].name, value: deviceValue(found[j]),
                                 device: found[j], busy: found[j].busy !== "",
                                 description: "Select to pair and connect it" })
                if (found.length === 0)
                    items.push({ type: "note", label: "Searching", busy: true,
                                 description: "Put the device in pairing mode, close by" })
            }
        }
        rows = items

        // The same lines in the same places, a device by its path (two may
        // share a name): the list and its cursor are left as they are.
        var same = items.length === shownRows.length
        for (var s = 0; s < items.length && same; s++)
            same = lineId(items[s]) === lineId(shownRows[s])
        if (same)
            return
        shownRows = items

        var index = -1
        if (current) {
            for (var k = 0; k < items.length && index < 0; k++) {
                if (current.type === "device" ? (items[k].type === "device" && items[k].device.path === current.device.path)
                                              : items[k].type === current.type && current.type !== "section" && current.type !== "note")
                    index = k
            }
        }
        // Its line gone (a device forgotten): the one now in its place.
        if (index < 0)
            index = list.selectableFrom(Math.min(Math.max(at, 0), items.length - 1), 1)
        list.currentIndex = index
        list.show(index)
    }

    // Which line a row is, whatever it says now.
    function lineId(row) { return row.type + ":" + (row.type === "device" ? row.device.path : row.label) }

    function activate(row) {
        if (!row || !bluetoothManager)
            return
        if (row.type === "power") {
            bluetoothManager.setPowered(!powered)
        } else if (row.type === "details") {
            bluetoothManager.collectDetails()
            details.open()
        } else if (row.type === "search") {
            if (searching)
                bluetoothManager.stopSearch()
            else
                bluetoothManager.startSearch()
        } else if (row.type === "device") {
            if (row.device.busy !== "")
                return
            if (!row.device.paired) {
                bluetoothManager.pair(row.device.path)
                return
            }
            optionsDevice = row.device
            options.choices = [
                row.device.connected ? { label: "Disconnect", action: "disconnect" }
                                     : { label: "Connect", action: "connect" },
                { label: "Forget", action: "forget" }
            ]
            options.subtitleText = row.device.name
            options.open()
        }
    }

    // A line's work under way (searching, pairing…) trails dots that come
    // and go.
    Timer {
        interval: 500
        repeat: true
        running: bluetoothRoot.rows.some(function(row) { return !!row.busy })
        onTriggered: bluetoothRoot.dots = bluetoothRoot.dots % 3 + 1
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/bluetooth.svg"
        title: "Bluetooth"
        subtitle: bluetoothManager ? bluetoothManager.adapterName : ""
        // The adapter's name is no step of the way here.
        path: ["Bluetooth"]
    }

    MenuList {
        id: list
        model: bluetoothRoot.shownRows
        focus: true
        onMoved: if (bluetoothManager) bluetoothManager.clearMessage()

        Keys.onLeftPressed: {
            var row = bluetoothRoot.rows[currentIndex]
            if (row && row.type === "power")
                bluetoothRoot.activate(row)
        }
        Keys.onRightPressed: {
            var row = bluetoothRoot.rows[currentIndex]
            if (row && row.type === "power")
                bluetoothRoot.activate(row)
        }
        Keys.onReturnPressed: bluetoothRoot.activate(bluetoothRoot.rows[currentIndex])
        Keys.onEnterPressed: bluetoothRoot.activate(bluetoothRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                bluetoothRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "KEYBOARD······CONNECTED", and
        // the groups' headings like Settings': "PAIRED ─────".
        delegate: MenuRow {
            // The line as it is now (rows), which the list's own (shownRows)
            // may be behind.
            readonly property var row: bluetoothRoot.rows[index] || modelData
            readonly property string trail: row.busy ? ".".repeat(bluetoothRoot.dots) : ""
            width: list.width
            height: root.menuRowHeight
            heading: row.type === "section"
            label: (row.label || "") + (row.type === "note" ? trail : "")
            value: row.value ? row.value + trail : ""
            // A long name is cut, not CONNECTED or PAIRED after it.
            keepValue: row.type === "device"
            selected: list.currentIndex === index && list.selectable(row)
        }
    }

    // What the line is for, or how the last pairing went.
    HelpLine {
        property var currentRow: bluetoothRoot.rows[list.currentIndex]
        text: bluetoothRoot.message !== "" ? bluetoothRoot.message
              : ((currentRow && currentRow.description) || "")
        // How a pairing went is said with Help Line off too.
        always: bluetoothRoot.message !== ""
        visible: text !== ""
    }

    HintBar {
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
    }

    // A paired device's options.
    ChoiceOverlay {
        id: options
        anchors.fill: parent
        promptText: "Bluetooth"
        onActivated: function(action) {
            var device = bluetoothRoot.optionsDevice
            if (!device || !bluetoothManager)
                return
            if (action === "connect")
                bluetoothManager.connectDevice(device.path)
            else if (action === "disconnect")
                bluetoothManager.disconnectDevice(device.path)
            else if (action === "forget")
                bluetoothManager.forget(device.path)
        }
        onClosed: list.forceActiveFocus()
    }

    // What the system says about Bluetooth, for a photo: ▲ ▼ scroll it.
    FocusScope {
        id: details
        anchors.fill: parent
        visible: false
        // Hidden, it lets go of the keys: an item only hidden would keep them.
        enabled: visible

        function open() {
            visible = true
            detailsText.contentY = 0
            forceActiveFocus()
        }
        function close() {
            visible = false
            list.forceActiveFocus()
        }

        Keys.onPressed: function(event) {
            // Ctrl+Q and the rest go on to Main.qml.
            if (event.modifiers & Qt.ControlModifier)
                return
            event.accepted = true
            if (root.isBack(event)
                    || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                close()
            else if (event.key === Qt.Key_Up)
                detailsText.scrollBy(-1)
            else if (event.key === Qt.Key_Down)
                detailsText.scrollBy(1)
        }

        // In a monitor layout's frame the details are read in the layout,
        // in its pane.
        readonly property bool monitor: root.monitorFramed === true
        OsdGround {
            anchors.fill: parent
            inLayout: true
        }

        AppBar {
            iconSource: "../../assets/images/bluetooth.svg"
            title: "Bluetooth"
            subtitle: "Details"
        }

        Flickable {
            id: detailsText
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: parent.monitor ? root.listY : root.sh * 0.2166667 //104
            anchors.leftMargin: parent.monitor ? root.paneX + root.sw * 0.009375 : root.sw * 0.125 //80
            width: parent.monitor ? root.paneWidth - root.sw * 0.01875 : root.sw * 0.75 //480
            // In the frame, down to its pane's foot, the ▼'s room left.
            height: parent.monitor ? root.paneBottom - root.listY - root.sh * 0.0291667
                                   : root.sh * 0.5 + root.hintRoom //240
            clip: true
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            // A line's worth at a time.
            function scrollBy(step) {
                var line = body.font.pixelSize * 1.2
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + step * line * 3))
            }

            // As the system wrote it, devices' names and all.
            Text {
                id: body
                width: detailsText.width
                textFormat: Text.PlainText
                text: (bluetoothManager && bluetoothManager.details) || "…"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667 //14
                wrapMode: Text.WrapAnywhere
                lineHeight: 1.2
            }
        }

        ScrollMarks {
            anchors.fill: detailsText
            list: detailsText
        }

        HintBar {
            text: root.hintLine(["back:BACK", "navigate:SCROLL"])
        }
    }

    // What pairing needs: a code to type on the device, or to compare.
    BluetoothPrompt {
        anchors.fill: parent
        prompt: bluetoothRoot.prompt
        onAnswered: function(accept) {
            if (bluetoothManager)
                bluetoothManager.answerPrompt(accept)
        }
        onCanceled: {
            if (bluetoothManager)
                bluetoothManager.cancelPairing()
        }
        // The keys back to what it came over: a device's options, if open.
        onVisibleChanged: if (!visible) (options.visible ? options : list).forceActiveFocus()
    }
}
