import QtQuick
import Components

// A game's options (EntryOptions: its favourite, Play at Startup), for the
// game they were opened for (offer()), whatever the list under them does
// meanwhile: the host's own first (firstChoices: Play, Game Details), then
// what is set for it, each chosen over the options (GameChoice): a vertical
// game's monitor (while Settings → Screen → Rotation isn't turned), which it
// is played and hosted with, its difficulty where MAME plays it with a DIP
// switch for it, and its engine where MAME and FinalBurn Neo both have it;
// with Online Play on, hosting it, and with LAN Play on, hosting it on the
// local network. Those that start the game (starts) come first, a line
// under them. The host acts on its own and on hosting (activated(action):
// "details", "host", "hostLan", or one of firstChoices'), with set and
// title; closed() once the options and what was chosen over them have
// gone, for the host to take the keys back.
//     GameOptions {
//         id: options
//         anchors.fill: parent
//         firstChoices: [{ label: "Game Details", action: "details" }]
//         onActivated: function(action) { … options.set, options.title }
//         onFavoritesEdited: tree.refresh("favorites")
//         onClosed: tree.forceActiveFocus()
//     }
//     … options.offer({ name, path: "game:" + set, isFolder: false })
Item {
    id: gameOptions

    property alias firstChoices: options.firstChoices
    // Counted up by the host as what it knows of its games changes: the
    // choices are read again.
    property int revision: 0
    // The game the options are for, and its title.
    readonly property var entry: options.entry
    readonly property string set: entry && String(entry.path).indexOf("game:") === 0
                                  ? String(entry.path).substring(5) : ""
    readonly property string title: entry ? entry.name || "" : ""
    // The options themselves are up; they, or a setting chosen over them.
    readonly property bool menuUp: options.visible
    readonly property bool up: menuUp || monitor.visible || difficulty.visible || engines.visible

    signal activated(string action)
    signal favoritesEdited()
    // A setting kept for the game.
    signal chosen()
    signal closed()

    function offer(item) { options.offer(item) }

    // What Host LAN Game says under the options while the cursor is on it:
    // the warning of this machine's link to the network (2.4 GHz Wi-Fi with
    // Bluetooth pads), then the advice on which machine should host, each
    // a line of its own; "" with neither.
    function hostingNote() {
        if (!arcadeBackend)
            return ""
        return [arcadeBackend.lanLinkWarning(-1), arcadeBackend.hostAdvice()].filter(Boolean).join("\n")
    }

    // Read again as a setting is kept.
    property int kept: 0
    function settingKept() {
        kept++
        chosen()
    }
    function optionsClosed() {
        if (!monitor.visible && !difficulty.visible && !engines.visible)
            closed()
    }

    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        // Made as the options open, not for every game the cursor passes.
        moreChoices: !options.visible ? []
                     : (gameOptions.revision, gameOptions.kept,
                        (monitor.offered(gameOptions.set)
                         ? [{ label: monitor.lineFor(gameOptions.set), action: "monitor" }] : [])
                        .concat(difficulty.offered(gameOptions.set)
                         ? [{ label: difficulty.lineFor(gameOptions.set), action: "difficulty" }] : [])
                        .concat(engines.offered(gameOptions.set)
                         ? [{ label: engines.lineFor(gameOptions.set), action: "engine" }] : [])
                        .concat(arcadeBackend && arcadeBackend.canHost(gameOptions.set)
                                ? [{ label: "Host Online Game", action: "host", starts: true }] : [])
                        .concat(arcadeBackend && arcadeBackend.canHostLan(gameOptions.set)
                                ? [{ label: "Host LAN Game", action: "hostLan", starts: true,
                                     description: gameOptions.hostingNote() }] : []))
        onActivated: function(action) {
            if (action === "monitor")
                monitor.offer(gameOptions.set, gameOptions.title)
            else if (action === "difficulty")
                difficulty.offer(gameOptions.set, gameOptions.title)
            else if (action === "engine")
                engines.offer(gameOptions.set, gameOptions.title)
            else
                gameOptions.activated(action)
        }
        onFavoritesEdited: gameOptions.favoritesEdited()
        onClosed: gameOptions.optionsClosed()
    }
    GameChoice {
        id: monitor
        anchors.fill: parent
        // Settings → Arcade → Emulator Video's name for the same three
        // turns, its value this option's Default.
        name: "Vertical Games"
        // "Vertical Games: Default" on the options, which fits the line.
        shortLine: true
        choicesFor: function(game) { return arcadeBackend ? arcadeBackend.monitorChoices(game) : [] }
        keep: function(game, id) { if (arcadeBackend) arcadeBackend.setMonitor(game, id) }
        onChosen: gameOptions.settingKept()
        onClosed: gameOptions.closed()
    }
    GameChoice {
        id: difficulty
        anchors.fill: parent
        name: "Difficulty"
        choicesFor: function(game) { return arcadeBackend ? arcadeBackend.difficultyChoices(game) : [] }
        keep: function(game, id) { if (arcadeBackend) arcadeBackend.setDifficulty(game, id) }
        onChosen: gameOptions.settingKept()
        onClosed: gameOptions.closed()
    }
    GameChoice {
        id: engines
        anchors.fill: parent
        name: "Engine"
        least: 3
        choicesFor: function(game) { return arcadeBackend ? arcadeBackend.engineChoices(game) : [] }
        keep: function(game, id) { if (arcadeBackend) arcadeBackend.setEngine(game, id) }
        onChosen: gameOptions.settingKept()
        onClosed: gameOptions.closed()
    }
}
