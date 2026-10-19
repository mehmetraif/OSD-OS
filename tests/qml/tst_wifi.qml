import QtQuick
import QtTest

// Settings → Wi-Fi, views/Wifi.qml, against a stand-in for WifiManager: no
// Wi-Fi; Wi-Fi off with no country, which select on WI-FI asks for first; the
// country's chooser, ▲ ▼ and ◄ ► from letter to letter; the lines (Search,
// Network Not Listed, the saved networks and the others, each with its lock and
// signal, a saved one out of reach marked so); a new network's password on the
// keyboard, an open one joined at once; a saved one's and the joined one's
// options; one not listed by its name, then its password or none; and the
// cursor kept on its network as the list changes.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    readonly property real helpLineMargin: sh * 0.1583333
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }

    // WifiManager as the page uses it, its calls kept in order.
    QtObject {
        id: wifiManager
        property bool supported: true
        property bool available: true
        property bool enabled: true
        property string state: "connected"
        property var current: ({ ssid: "Home", bars: 4, address: "192.168.1.20" })
        property bool searching: false
        property var networks: []
        property string country: "TR"
        property string countryName: "Türkiye"
        property bool countryAvailable: true
        property bool countryBusy: false
        property string suggestedCountry: ""
        property var countries: [
            { code: "AF", name: "Afghanistan", letter: "A" },
            { code: "AX", name: "Åland Islands", letter: "A" },
            { code: "AL", name: "Albania", letter: "A" },
            { code: "BE", name: "Belgium", letter: "B" },
            { code: "DE", name: "Germany", letter: "G" },
            { code: "GR", name: "Greece", letter: "G" },
            { code: "TR", name: "Türkiye", letter: "T" },
            { code: "GB", name: "United Kingdom", letter: "U" },
            { code: "US", name: "United States", letter: "U" }
        ]
        property string message: ""
        property var calls: []
        function record(call) { calls = calls.concat([call]) }
        function setEnabled(on) { record("setEnabled " + on) }
        function startSearch() { record("startSearch") }
        function stopSearch() { record("stopSearch") }
        function connectTo(ssid, password) { record("connectTo " + ssid + (password ? " " + password : "")) }
        function connectHidden(ssid, password) { record("connectHidden " + ssid + " " + password) }
        function disconnectNetwork() { record("disconnectNetwork") }
        function forget(ssid) { record("forget " + ssid) }
        function setCountry(code) { record("setCountry " + code); return true }
        function clearMessage() { message = "" }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    function network(ssid, extra) {
        return Object.assign({ ssid: ssid, bars: 3, security: "wpa", secured: true, known: false, heard: true,
                               active: false, busy: "" }, extra || {})
    }

    TestCase {
        name: "Wifi"
        when: windowShown

        function init() {
            wifiManager.available = true
            wifiManager.enabled = true
            wifiManager.state = "connected"
            wifiManager.current = { ssid: "Home", bars: 4, address: "192.168.1.20" }
            wifiManager.searching = false
            wifiManager.country = "TR"
            wifiManager.countryName = "Türkiye"
            wifiManager.countryAvailable = true
            wifiManager.countryBusy = false
            wifiManager.suggestedCountry = ""
            wifiManager.message = ""
            wifiManager.calls = []
            wifiManager.networks = [
                root.network("Home", { bars: 4, known: true, active: true }),
                root.network("Cafe", { bars: 2, security: "open", secured: false, known: true }),
                root.network("Office", { bars: 0, security: "", secured: false, known: true, heard: false }),
                root.network("Neighbour", { bars: 3 }),
                root.network("Free", { bars: 0, security: "open", secured: false }),
                root.network("Corp", { bars: 2, security: "enterprise" })
            ]
        }
        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../views/Wifi.qml"))
            verify(loader.item !== null, "loaded Wifi.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        // The page's lines as their labels, a heading's marked "# ".
        function lines(page) {
            return page.rows.map(function(row) { return (row.type === "section" ? "# " : "") + (row.label || "") })
        }
        function row(page, label) {
            return page.rows.filter(function(r) { return r.label === label })[0]
        }
        function listOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                var item = page.children[i]
                if (item.currentIndex !== undefined && item.model !== undefined && item.highlightMoveDuration !== undefined)
                    return item
            }
            return null
        }
        function cursor(page) {
            var list = listOf(page)
            return list ? page.rows[list.currentIndex].label : ""
        }
        function moveTo(page, label) {
            for (var i = 0; i < page.rows.length && cursor(page) !== label; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), label)
        }
        function keyboardOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].masked !== undefined)
                    return page.children[i]
            }
            return null
        }
        function overlayOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].promptText !== undefined)
                    return page.children[i]
            }
            return null
        }
        function chooserOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].entries !== undefined)
                    return page.children[i]
            }
            return null
        }
        function type(text) {
            for (var i = 0; i < text.length; i++)
                keyClick(text.charAt(i))
        }

        function test_unavailable() {
            wifiManager.available = false
            var page = open()
            compare(lines(page), ["No Wi-Fi"])
        }

        // The lines in their groups, a network's value and its icons.
        function test_lines() {
            var page = open()
            compare(lines(page), ["Wi-Fi", "Country", "Search", "Network Not Listed", "# Saved", "Home", "Cafe",
                                  "Office", "# Networks", "Neighbour", "Free", "Corp"])
            compare(row(page, "Wi-Fi").value, "On")
            compare(row(page, "Country").value, "Türkiye")
            compare(row(page, "Home").value, "Connected")
            compare(row(page, "Home").icons, ["lock", "signal4"])
            compare(row(page, "Cafe").value, "Saved")
            compare(row(page, "Cafe").icons, ["signal2"])
            compare(row(page, "Office").value, "Not Here")
            compare(row(page, "Office").icons, [])
            compare(row(page, "Neighbour").value, "")
            compare(row(page, "Neighbour").icons, ["lock", "signal3"])
            // Heard at all, a bar.
            compare(row(page, "Free").icons, ["signal1"])
            verify(row(page, "Home").description.indexOf("192.168.1.20") >= 0)
            verify(row(page, "Corp").description.indexOf("user name") >= 0)
            compare(cursor(page), "Wi-Fi")
        }

        // Off: no search and no networks; on again with a select or ◄ ►.
        function test_power() {
            wifiManager.enabled = false
            wifiManager.state = "off"
            var page = open()
            compare(lines(page), ["Wi-Fi", "Country"])
            compare(row(page, "Wi-Fi").value, "Off")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Left)
            compare(wifiManager.calls, ["setEnabled true", "setEnabled true"])
            wifiManager.state = "blocked"
            compare(row(page, "Wi-Fi").value, "Blocked")
        }

        // No country: WI-FI opens the chooser first, on the time zone's
        // country, and the one chosen is set (Raspberry Pi OS turns Wi-Fi on
        // with it).
        function test_noCountry() {
            wifiManager.enabled = false
            wifiManager.state = "off"
            wifiManager.country = ""
            wifiManager.countryName = ""
            wifiManager.suggestedCountry = "DE"
            var page = open()
            compare(row(page, "Country").value, "Not Set")
            keyClick(Qt.Key_Return)
            var chooser = chooserOf(page)
            verify(chooser.visible)
            compare(chooser.entries[chooser.index].code, "DE")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!chooser.visible)
            compare(wifiManager.calls, ["setCountry GR"])
            wifiManager.countryBusy = true
            compare(row(page, "Country").value, "Setting")
        }

        // The chooser: ▲ ▼ one at a time, round; ◄ ► to the first of the
        // letter before or after, round; back leaves it as it was.
        function test_countryChooser() {
            var page = open()
            moveTo(page, "Country")
            keyClick(Qt.Key_Return)
            var chooser = chooserOf(page)
            verify(chooser.visible)
            compare(chooser.entries[chooser.index].code, "TR")
            keyClick(Qt.Key_Right)
            compare(chooser.entries[chooser.index].code, "GB")
            keyClick(Qt.Key_Right)
            compare(chooser.entries[chooser.index].code, "AF")
            keyClick(Qt.Key_Left)
            compare(chooser.entries[chooser.index].code, "GB")
            keyClick(Qt.Key_Left)
            compare(chooser.entries[chooser.index].code, "TR")
            keyClick(Qt.Key_Left)
            compare(chooser.entries[chooser.index].code, "DE")
            keyClick(Qt.Key_Up)
            compare(chooser.entries[chooser.index].code, "BE")
            keyClick(Qt.Key_Left)
            compare(chooser.entries[chooser.index].code, "AF")
            keyClick(Qt.Key_Up)
            compare(chooser.entries[chooser.index].code, "US")
            keyClick(Qt.Key_Escape)
            verify(!chooser.visible)
            compare(wifiManager.calls, [])
            compare(cursor(page), "Country")
        }

        // A new network's password on the keyboard; an open one at once; one
        // that wants a user name says so (the manager's message).
        function test_join() {
            var page = open()
            moveTo(page, "Neighbour")
            keyClick(Qt.Key_Return)
            var keyboard = keyboardOf(page)
            verify(keyboard.visible)
            verify(keyboard.masked)
            compare(keyboard.title, "Neighbour Password")
            type("Secret12")
            keyClick(Qt.Key_Return)
            verify(!keyboard.visible)
            compare(wifiManager.calls, ["connectTo Neighbour Secret12"])
            moveTo(page, "Free")
            keyClick(Qt.Key_Return)
            verify(!keyboard.visible)
            moveTo(page, "Corp")
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["connectTo Neighbour Secret12", "connectTo Free", "connectTo Corp"])
            // Back from the keyboard joins nothing.
            moveTo(page, "Neighbour")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Escape)
            verify(!keyboard.visible)
            compare(wifiManager.calls.length, 3)
            compare(cursor(page), "Neighbour")
        }

        // A saved network: connect, a new password or forget it; the one
        // joined: disconnect or forget it.
        function test_options() {
            var page = open()
            var overlay = overlayOf(page)
            moveTo(page, "Cafe")
            keyClick(Qt.Key_Return)
            verify(overlay.visible)
            compare(overlay.subtitleText, "Cafe")
            compare(overlay.choices.map(function(c) { return c.label }), ["Connect", "New Password", "Forget"])
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            var keyboard = keyboardOf(page)
            verify(keyboard.visible)
            type("Another1")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["connectTo Cafe", "connectTo Cafe Another1", "forget Cafe"])
            moveTo(page, "Home")
            keyClick(Qt.Key_Return)
            compare(overlay.choices.map(function(c) { return c.label }), ["Disconnect", "Forget"])
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls[3], "disconnectNetwork")
        }

        // One not listed: its name, then its password or none.
        function test_hidden() {
            var page = open()
            moveTo(page, "Network Not Listed")
            keyClick(Qt.Key_Return)
            var keyboard = keyboardOf(page)
            verify(keyboard.visible)
            verify(!keyboard.masked)
            type("Attic")
            keyClick(Qt.Key_Return)
            var overlay = overlayOf(page)
            verify(overlay.visible)
            compare(overlay.subtitleText, "Attic")
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            verify(keyboard.masked)
            type("Hidden99")
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["connectHidden Attic Hidden99"])
            keyClick(Qt.Key_Return)
            type("Barn")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["connectHidden Attic Hidden99", "connectHidden Barn "])
        }

        // Search starts and stops it, SEARCHING trailing dots; leaving the
        // page stops it.
        function test_search() {
            var page = open()
            moveTo(page, "Search")
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["startSearch"])
            wifiManager.searching = true
            compare(row(page, "Search").value, "Searching")
            verify(row(page, "Search").busy)
            keyClick(Qt.Key_Return)
            compare(wifiManager.calls, ["startSearch", "stopSearch"])
            loader.source = ""
            compare(wifiManager.calls, ["startSearch", "stopSearch", "stopSearch"])
        }

        // The cursor keeps to its network as the list changes: joined, it
        // moves from NETWORKS to SAVED, the cursor with it.
        function test_cursorKept() {
            var page = open()
            moveTo(page, "Neighbour")
            wifiManager.networks = [
                root.network("Neighbour", { bars: 3, known: true, active: true }),
                root.network("Home", { bars: 4, known: true }),
                root.network("Free", { bars: 1, security: "open", secured: false })
            ]
            compare(lines(page), ["Wi-Fi", "Country", "Search", "Network Not Listed", "# Saved", "Neighbour", "Home",
                                  "# Networks", "Free"])
            compare(cursor(page), "Neighbour")
            // A message over the line's help until the cursor moves.
            wifiManager.message = "Connected to Neighbour"
            keyClick(Qt.Key_Down)
            compare(wifiManager.message, "")
        }
    }
}
