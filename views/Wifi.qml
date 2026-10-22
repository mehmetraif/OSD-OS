import QtQuick
import Components

// Settings → Wi-Fi: joins a Wi-Fi network through wifiManager
// (NetworkManager). Laid out like Settings: WI-FI on or off, the COUNTRY whose
// rules the radio keeps to, SEARCH, NETWORK NOT LISTED for one that doesn't
// say its name, then the SAVED networks and the other NETWORKS heard, each with
// its lock and its signal at the line's end. Select on a new network asks for
// its password on the on-screen keyboard (none for an open one) and joins it;
// on a saved one it offers to connect, give it a new password or forget it, on
// the one joined to disconnect or forget it. Wi-Fi stays off until the country
// is set, so turning it on without one opens the country's chooser first.
FocusScope {
    id: wifiRoot

    signal goBack()

    property var navParams: ({})

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Update.qml).
    readonly property bool available: wifiManager ? wifiManager.available : false
    readonly property bool wifiOn: wifiManager ? wifiManager.enabled : false
    readonly property string radio: wifiManager ? wifiManager.state : ""
    readonly property var current: wifiManager ? wifiManager.current : ({})
    readonly property bool searching: wifiManager ? wifiManager.searching : false
    readonly property var networks: wifiManager ? wifiManager.networks : []
    readonly property string country: wifiManager ? wifiManager.country : ""
    readonly property string countryName: wifiManager ? wifiManager.countryName : ""
    readonly property bool countryAvailable: wifiManager ? wifiManager.countryAvailable : false
    readonly property bool countryBusy: wifiManager ? wifiManager.countryBusy : false
    readonly property string message: wifiManager ? wifiManager.message : ""

    // The lines: { type: "power" | "country" | "search" | "hidden" | "section" | "note" | "network", … }.
    property var rows: []
    // The network whose options are open.
    property var optionsNetwork: null
    // What the keyboard is typing: "password" (for typingFor), "hiddenName",
    // or "hiddenPassword" (for hiddenName); "" when it is closed.
    property string typing: ""
    property string typingFor: ""
    property string hiddenName: ""
    // The dots after SEARCHING, one more each half second.
    property int dots: 1

    onAvailableChanged: buildRows()
    onWifiOnChanged: buildRows()
    onRadioChanged: buildRows()
    onCurrentChanged: buildRows()
    onSearchingChanged: buildRows()
    onNetworksChanged: buildRows()
    onCountryChanged: buildRows()
    onCountryBusyChanged: buildRows()
    Component.onCompleted: buildRows()

    // Leaving the page ends its search: a scan keeps the radio busy.
    Component.onDestruction: {
        if (wifiManager) {
            wifiManager.stopSearch()
            wifiManager.clearMessage()
        }
    }

    function networkValue(n) {
        if (n.busy === "connecting")    return "Connecting"
        if (n.busy === "disconnecting") return "Disconnecting"
        if (n.active)                   return "Connected"
        if (n.known)                    return n.heard ? "Saved" : "Not Here"
        return ""
    }

    // Its lock, a password wanted, and its signal: one bar at least for a
    // network heard at all.
    function networkIcons(n) {
        if (!n.heard)
            return []
        var icons = n.secured ? ["lock"] : []
        icons.push("signal" + Math.max(1, Math.min(4, n.bars)))
        return icons
    }

    function networkDescription(n) {
        if (n.active)
            return (current.address ? "Connected, its address " + current.address + ". " : "")
                   + "Select to disconnect or forget it"
        if (n.known)
            return n.heard ? "Saved: select to connect, give it a new password or forget it"
                           : "Saved, but not heard here: select to connect, give it a new password or forget it"
        if (n.security === "enterprise")
            return "It wants a user name too (WPA Enterprise), which can't be typed here"
        return n.secured ? "Select to type its password and join it" : "Open: select to join it, no password wanted"
    }

    function networkRow(n) {
        return { type: "network", label: n.ssid, value: networkValue(n), icons: networkIcons(n), network: n,
                 busy: n.busy !== "", description: networkDescription(n) }
    }

    function buildRows() {
        // The cursor stays on its line as lines come and go: a network by its
        // name (it moves from NETWORKS to SAVED once joined), the others by
        // type.
        var at = list.currentIndex
        var current = rows[at]
        var items = []
        if (!available) {
            items.push({ type: "note", label: "No Wi-Fi",
                         description: "NetworkManager isn't running, or this computer has no Wi-Fi" })
        } else {
            var blocked = radio === "blocked"
            items.push({ type: "power", label: "Wi-Fi", value: blocked ? "Blocked" : wifiOn ? "On" : "Off",
                         description: blocked ? "A switch keeps the radio off"
                                    : country === "" && countryAvailable && !wifiOn
                                      ? "Turns Wi-Fi on, once you have chosen the country the Pi is used in"
                                      : "Turns Wi-Fi on or off" })
            if (countryAvailable || country !== "")
                items.push({ type: "country", label: "Country", busy: countryBusy,
                             value: countryBusy ? "Setting" : countryName !== "" ? countryName : "Not Set",
                             description: countryAvailable
                                 ? "The country the Pi is used in: Wi-Fi keeps to its rules, and stays off until it is set"
                                 : "The country the Pi is used in, which Wi-Fi keeps to the rules of" })
            if (wifiOn) {
                items.push({ type: "search", label: "Search", value: searching ? "Searching" : "", busy: searching,
                             description: searching
                                 ? "Looking for networks for a minute. Select stops it"
                                 : "Looks for networks for a minute, listing them below as they answer" })
                items.push({ type: "hidden", label: "Network Not Listed",
                             description: "One that doesn't say its name: type its name, then its password" })
                var saved = networks.filter(function(n) { return n.known || n.active })
                var others = networks.filter(function(n) { return !n.known && !n.active })
                if (saved.length > 0) {
                    items.push({ type: "section", label: "Saved" })
                    for (var i = 0; i < saved.length; i++)
                        items.push(networkRow(saved[i]))
                }
                if (others.length > 0 || searching) {
                    items.push({ type: "section", label: "Networks" })
                    for (var j = 0; j < others.length; j++)
                        items.push(networkRow(others[j]))
                    if (others.length === 0)
                        items.push({ type: "note", label: "Searching", busy: true,
                                     description: "Networks within reach answer in a few seconds" })
                }
            }
        }
        rows = items

        var index = -1
        if (current) {
            for (var k = 0; k < items.length && index < 0; k++) {
                if (current.type === "network" ? (items[k].type === "network" && items[k].network.ssid === current.network.ssid)
                                               : items[k].type === current.type && current.type !== "section" && current.type !== "note")
                    index = k
            }
        }
        // Its line gone (a network forgotten, Wi-Fi turned off): the one now
        // in its place.
        if (index < 0)
            index = selectableFrom(Math.min(Math.max(at, 0), items.length - 1), 1)
        list.currentIndex = index
        list.positionViewAtIndex(index, ListView.Contain)
    }

    function selectable(row) { return !!row && row.type !== "section" && row.type !== "note" }

    // The first line that can be chosen from index on, going by step, round.
    function selectableFrom(index, step) {
        for (var n = 0; n < rows.length; n++) {
            var i = ((index + n * step) % rows.length + rows.length) % rows.length
            if (selectable(rows[i]))
                return i
        }
        return 0
    }

    function askPassword(ssid) {
        typing = "password"
        typingFor = ssid
        keyboard.title = ssid + " Password"
        keyboard.masked = true
        keyboard.maxLength = 64
        keyboard.open("")
    }

    function activate(row) {
        if (!row || !wifiManager)
            return
        if (row.type === "power") {
            // Raspberry Pi OS turns it on itself once the country is set,
            // and while one is being set there is nothing to choose.
            if (!wifiOn && country === "" && countryAvailable) {
                if (!countryBusy)
                    countries.open()
            } else {
                wifiManager.setEnabled(!wifiOn)
            }
        } else if (row.type === "country") {
            if (countryAvailable && !countryBusy)
                countries.open()
        } else if (row.type === "search") {
            if (searching)
                wifiManager.stopSearch()
            else
                wifiManager.startSearch()
        } else if (row.type === "hidden") {
            typing = "hiddenName"
            keyboard.title = "Network Name"
            keyboard.masked = false
            keyboard.maxLength = 32
            keyboard.open("")
        } else if (row.type === "network") {
            var n = row.network
            if (n.busy !== "")
                return
            if (n.active || n.known) {
                optionsNetwork = n
                options.choices = n.active
                    ? [{ label: "Disconnect", action: "disconnect" }, { label: "Forget", action: "forget" }]
                    : [{ label: "Connect", action: "connect" }, { label: "New Password", action: "password" },
                       { label: "Forget", action: "forget" }]
                options.subtitleText = n.ssid
                options.open()
            } else if (n.secured && n.security !== "enterprise") {
                askPassword(n.ssid)
            } else {
                // Open, or WPA Enterprise, which the manager says it can't
                // join: a user name too can't be typed here.
                wifiManager.connectTo(n.ssid)
            }
        }
    }

    // A line's work under way (searching, connecting…) trails dots that come
    // and go.
    Timer {
        interval: 500
        repeat: true
        running: wifiRoot.rows.some(function(row) { return !!row.busy })
        onTriggered: wifiRoot.dots = wifiRoot.dots % 3 + 1
    }

    // Header: the address the Pi has on the network joined.
    AppBar {
        iconSource: "../../assets/images/wifi.svg"
        title: "Wi-Fi"
        subtitle: wifiRoot.current.address || ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: list
        visible: !keyboard.visible
        model: wifiRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        // Lines come and go as networks answer: no sliding.
        highlightMoveDuration: 0

        function move(step) {
            if (wifiRoot.rows.length === 0)
                return
            currentIndex = wifiRoot.selectableFrom(currentIndex + step, step)
            positionViewAtIndex(currentIndex, ListView.Contain)
            if (wifiManager)
                wifiManager.clearMessage()
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onLeftPressed: {
            var row = wifiRoot.rows[currentIndex]
            if (row && row.type === "power")
                wifiRoot.activate(row)
        }
        Keys.onRightPressed: {
            var row = wifiRoot.rows[currentIndex]
            if (row && row.type === "power")
                wifiRoot.activate(row)
        }
        Keys.onReturnPressed: wifiRoot.activate(wifiRoot.rows[currentIndex])
        Keys.onEnterPressed: wifiRoot.activate(wifiRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                wifiRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "HOME······SAVED", then its lock
        // and signal, and the groups' headings like Settings': "SAVED ─────".
        // A network's name gives way to its value and icons.
        delegate: MenuRow {
            readonly property string trail: modelData.busy ? ".".repeat(wifiRoot.dots) : ""
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: (modelData.label || "") + (modelData.type === "note" ? trail : "")
            value: modelData.value ? modelData.value + trail : ""
            icons: modelData.icons || []
            keepValue: modelData.type === "network"
            selected: list.currentIndex === index && wifiRoot.selectable(modelData)
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        visible: list.visible
        anchors.fill: list
        list: list
    }

    // What the line is for, or how the last connection went.
    HelpLine {
        property var currentRow: wifiRoot.rows[list.currentIndex]
        text: wifiRoot.message !== "" ? wifiRoot.message
              : ((currentRow && currentRow.description) || "")
        visible: text !== "" && !keyboard.visible
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !keyboard.visible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A saved network's options, the one joined's, or whether one not
    // listed wants a password.
    ChoiceOverlay {
        id: options
        anchors.fill: parent
        promptText: "Wi-Fi"
        onActivated: function(action) {
            if (!wifiManager)
                return
            if (action === "hiddenPassword") {
                wifiRoot.typing = "hiddenPassword"
                keyboard.title = wifiRoot.hiddenName + " Password"
                keyboard.masked = true
                keyboard.maxLength = 64
                keyboard.open("")
                return
            }
            if (action === "hiddenOpen") {
                wifiManager.connectHidden(wifiRoot.hiddenName, "")
                return
            }
            var n = wifiRoot.optionsNetwork
            if (!n)
                return
            if (action === "connect")
                wifiManager.connectTo(n.ssid)
            else if (action === "password")
                wifiRoot.askPassword(n.ssid)
            else if (action === "disconnect")
                wifiManager.disconnectNetwork()
            else if (action === "forget")
                wifiManager.forget(n.ssid)
        }
        onClosed: list.forceActiveFocus()
    }

    // A network's password, or one not listed's name and then, unless it is
    // open, its password.
    OnScreenKeyboard {
        id: keyboard
        anchors.fill: parent
        visible: false
        caseSensitive: true
        onAccepted: function(text) {
            var typed = wifiRoot.typing
            wifiRoot.typing = ""
            // A password typed goes no further than NetworkManager.
            keyboard.text = ""
            if (!wifiManager)
                return
            if (typed === "password") {
                wifiManager.connectTo(wifiRoot.typingFor, text)
            } else if (typed === "hiddenName") {
                // Never blank: the keyboard's OK takes none.
                var name = text.trim()
                wifiRoot.hiddenName = name
                options.choices = [{ label: "Its Password", action: "hiddenPassword" },
                                   { label: "Open, None", action: "hiddenOpen" }]
                options.subtitleText = name
                options.open()
                return
            } else if (typed === "hiddenPassword") {
                wifiManager.connectHidden(wifiRoot.hiddenName, text)
            }
            list.forceActiveFocus()
        }
        onCanceled: {
            wifiRoot.typing = ""
            keyboard.text = ""
            list.forceActiveFocus()
        }
    }

    // The country the Pi is used in, from the time zone database's list by
    // name, starting on the one set, else the system time zone's: ▲ ▼ one at a
    // time, ◄ ► from letter to letter.
    FocusScope {
        id: countries
        anchors.fill: parent
        visible: false
        // Hidden, it lets go of the keys: an item only hidden would keep them.
        enabled: visible

        readonly property var entries: wifiManager ? wifiManager.countries : []
        property int index: 0

        function open() {
            var code = wifiRoot.country !== "" ? wifiRoot.country : (wifiManager ? wifiManager.suggestedCountry : "")
            index = 0
            for (var i = 0; i < entries.length; i++) {
                if (entries[i].code === code)
                    index = i
            }
            visible = true
            forceActiveFocus()
        }
        function close() {
            visible = false
            list.forceActiveFocus()
        }
        // To the first country of the next letter (step 1), or of the one
        // before (step -1), round.
        function jump(step) {
            var n = entries.length
            if (n === 0)
                return
            var letter = entries[index].letter
            var i = index
            if (step > 0) {
                while (i < n && entries[i].letter === letter)
                    i++
                index = i < n ? i : 0
            } else {
                while (i >= 0 && entries[i].letter === letter)
                    i--
                if (i < 0)
                    i = n - 1
                while (i > 0 && entries[i - 1].letter === entries[i].letter)
                    i--
                index = i
            }
        }

        Keys.onPressed: function(event) {
            // Ctrl+Q and the rest go on to Main.qml.
            if (event.modifiers & Qt.ControlModifier)
                return
            event.accepted = true
            var n = entries.length
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                close()
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && n > 0) {
                var code = entries[index].code
                close()
                if (wifiManager)
                    wifiManager.setCountry(code)
            } else if (event.key === Qt.Key_Up && n > 0) {
                index = (index - 1 + n) % n
            } else if (event.key === Qt.Key_Down && n > 0) {
                index = (index + 1) % n
            } else if (event.key === Qt.Key_Left) {
                jump(-1)
            } else if (event.key === Qt.Key_Right) {
                jump(1)
            }
        }

        PromptScreen {
            title: "Wi-Fi Country"
            message: "The country the Pi is used in: Wi-Fi keeps to its rules"
            choices: countries.entries.map(function(c) { return { label: c.name, action: c.code } })
            currentIndex: countries.index
            hint: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":LETTER "
                  + root.hints.select + ":SELECT"
        }
    }
}
