import QtQuick
import Components

// Settings → Network → Wi-Fi: joins a Wi-Fi network through wifiManager
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
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

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
    // The list is given them anew only as lines come, go or change places
    // (MenuList's take()): one whose value, icons or help change (a signal,
    // a join under way, a search) is changed where it is, from rows, so a
    // scan's answers don't make every line again each time.
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
        list.take(items, lineId)
    }

    // Which line a row is, whatever it says now: the cursor stays on it as
    // lines come and go, a network by its name (it moves from NETWORKS to
    // SAVED once joined), else the one now in its place (a network
    // forgotten, Wi-Fi turned off).
    function lineId(row) { return row.type + ":" + (row.type === "network" ? row.network.ssid : row.label) }

    function askPassword(ssid) {
        typing = "password"
        typingFor = ssid
        keyboard.title = ssid + " Password"
        keyboard.masked = true
        keyboard.maxLength = 64
        keyboard.open("")
    }

    // The network not listed is asked for by the name typed, which is kept
    // until now: back from its password, from the question before it or
    // from the name itself comes to a keyboard that opens on the name
    // again, not on nothing.
    function joinHidden(password) {
        var name = hiddenName
        hiddenName = ""
        wifiManager.connectHidden(name, password)
    }

    function activate(row) {
        if (!row || !wifiManager)
            return
        if (row.type === "power") {
            // Raspberry Pi OS turns it on itself once the country is set,
            // and while one is being set there is nothing to choose.
            if (!wifiOn && country === "" && countryAvailable) {
                if (!countryBusy)
                    countries.openOnCountry()
            } else {
                wifiManager.setEnabled(!wifiOn)
            }
        } else if (row.type === "country") {
            if (countryAvailable && !countryBusy)
                countries.openOnCountry()
        } else if (row.type === "search") {
            if (searching)
                wifiManager.stopSearch()
            else
                wifiManager.startSearch()
        } else if (row.type === "hidden") {
            // On the name typed before, where back left it before its join
            // was asked (hiddenName).
            typing = "hiddenName"
            keyboard.title = "Network Name"
            keyboard.masked = false
            keyboard.maxLength = 32
            keyboard.open(hiddenName)
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

    // What select does on a line, for the hint bar: "" where it does
    // nothing (a note, a network busy joining).
    function selectHint(row) {
        if (!row)
            return ""
        if (row.type === "power")
            return !wifiOn && country === "" && countryAvailable
                   ? (countryBusy ? "" : "select:SET COUNTRY")
                   : "select:" + (wifiOn ? "TURN OFF" : "TURN ON")
        if (row.type === "country")
            return countryAvailable && !countryBusy ? "select:CHOOSE" : ""
        if (row.type === "search")
            return "select:" + (searching ? "STOP" : "SEARCH")
        if (row.type === "hidden")
            return "select:TYPE NAME"
        if (row.type === "network") {
            var n = row.network
            if (n.busy !== "")
                return ""
            return "select:" + (n.active || n.known ? "OPTIONS" : "JOIN")
        }
        return ""
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
        // The address is no step of the way here.
        path: ["Wi-Fi"]
    }

    MenuList {
        id: list
        visible: !keyboard.visible
        focus: true
        onMoved: if (wifiManager) wifiManager.clearMessage()

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
            if (root.isBack(event)) {
                wifiRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "HOME······SAVED", then its lock
        // and signal, and the groups' headings like Settings': "SAVED ─────".
        // A network's name gives way to its value and icons.
        delegate: MenuRow {
            // The line as it is now (rows), which the list's own (its model)
            // may be behind.
            readonly property var row: wifiRoot.rows[index] || modelData
            readonly property string trail: row.busy ? ".".repeat(wifiRoot.dots) : ""
            width: list.width
            height: root.menuRowHeight
            heading: row.type === "section"
            label: (row.label || "") + (row.type === "note" ? trail : "")
            value: row.value ? row.value + trail : ""
            icons: row.icons || []
            keepValue: row.type === "network"
            selected: list.currentIndex === index && list.selectable(row)
        }
    }

    // What the line is for, or how the last connection went.
    HelpLine {
        property var currentRow: wifiRoot.rows[list.currentIndex]
        text: wifiRoot.message !== "" ? wifiRoot.message
              : ((currentRow && currentRow.description) || "")
        // How a join went is said with Help Line off too: without it a join
        // that failed showed nothing at all.
        always: wifiRoot.message !== ""
        visible: text !== "" && !keyboard.visible
    }

    // The keys that do something on the line under the cursor: select's
    // word is what it does there, and nothing but back with no line to go
    // to (no Wi-Fi).
    HintBar {
        visible: !keyboard.visible
        text: root.hintLine(["back:BACK",
                             wifiRoot.rows.filter(function(r) { return list.selectable(r) }).length > 1 ? "navigate:NAVIGATE" : "",
                             wifiRoot.selectHint(wifiRoot.rows[list.currentIndex])])
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
                wifiRoot.joinHidden("")
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
                forgetQuestion.ask(n)
        }
        onClosed: list.forceActiveFocus()
    }

    // Forget is asked first, the cursor on Cancel: a password forgotten
    // has to be typed again with a remote to join the network.
    ChoiceOverlay {
        id: forgetQuestion
        objectName: "forgetQuestion"
        anchors.fill: parent
        property string ssid: ""
        promptText: "Forget " + ssid + "?"
        subtitleText: "Its password has to be typed again to join it"
        choices: [{ label: "Cancel", action: "cancel" }, { label: "Forget", action: "forget" }]
        function ask(network) {
            ssid = network.ssid
            open()
        }
        onActivated: function(action) {
            if (action === "forget" && wifiManager)
                wifiManager.forget(ssid)
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
                wifiRoot.joinHidden(text)
            }
            list.forceActiveFocus()
        }
        onCanceled: {
            // A name left half typed is kept as far as it got; a password
            // never is.
            if (wifiRoot.typing === "hiddenName")
                wifiRoot.hiddenName = keyboard.text
            wifiRoot.typing = ""
            keyboard.text = ""
            list.forceActiveFocus()
        }
    }

    // The country the Pi is used in, from the time zone database's list by
    // name, starting on the one set, else the system time zone's: ▲ ▼ one at a
    // time, ◄ ► from letter to letter. Its lines are made as it first opens:
    // most visits to the page never choose a country.
    ListPicker {
        id: countries
        title: "Wi-Fi Country"
        message: "The country the Pi is used in: Wi-Fi keeps to its rules"
        groupName: "LETTER"
        entries: made && wifiManager ? wifiManager.countries.map(function(c) {
            return { label: c.name, value: c.code, group: c.letter }
        }) : []

        function openOnCountry() {
            open(wifiRoot.country !== "" ? wifiRoot.country : (wifiManager ? wifiManager.suggestedCountry : ""))
        }
        onClosed: list.forceActiveFocus()
        onPicked: function(code) {
            if (wifiManager)
                wifiManager.setCountry(code)
        }
    }
}
