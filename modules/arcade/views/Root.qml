import QtQuick

FocusScope {
    id: moduleRoot

    // Exit signal — emitted to leave the module entirely
    signal goBack()

    property var navParams: ({})

    // The module's manifest id — the single place it appears in this module's QML.
    // Child views reference it via moduleRoot.moduleId.
    property string moduleId: "com.osdos.arcade"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    // Settings → Arcade's, as the module opens. Screen: "Original", the tree
    // (Games.qml), or "Arcade", the selection screen (ArcadeScreen.qml),
    // whose own are its Hint Bar and Help Line, the filter it opens on, how
    // its list lies, its column's frames and ground. Start Info Screen is
    // both's: how to come back, before a game.
    property string screen: "Original"
    readonly property bool arcadeScreen: screen === "Arcade"
    property bool hintBar: true
    property bool helpLine: true
    property bool startInfo: true
    property string startFilter: "all"
    // List Style: "Straight", "Curved" or "3D Curved".
    property string listStyle: "Curved"
    // Border: the frames round the Arcade screen's picture and the box under
    // it.
    property bool border: true
    // Info Background: how solid the Arcade screen's column's ground is, 0
    // (transparent) to 100 (solid).
    property int infoBackground: 100
    // Screen Adjustment: how far each side of the Arcade screen's area is
    // moved out from where the design has it (in, below 0), in the design's
    // units, a 960th of the width and a 720th of the height.
    property var screenAdjustment: ({ top: 0, bottom: 0, left: 0, right: 0 })
    // The Arcade screen's Hint Bar and Help Line, which Main.qml's hintBar and
    // helpLine go by while the module is open, its dialogs and all; the
    // original screen goes by Settings' own.
    readonly property var hintBarShown: arcadeScreen ? hintBar : undefined
    readonly property var helpLineShown: arcadeScreen ? helpLine : undefined

    // The credits bought on the Arcade screen with COIN: they go in the next
    // game started (startGame()), as many as ArcadeBackend puts in
    // (kMaxCredits).
    property int credits: 0
    readonly property int maxCredits: 9
    function addCredit() { credits = Math.min(maxCredits, credits + 1) }

    // Starts a game in MAME with the credits bought, which are then spent,
    // and puts it on Recently Played (the newest 30). False, with
    // arcadeBackend.lastError(), when it couldn't be started: a game that
    // never started, or was canceled before, isn't put there.
    function startGame(set, title) {
        if (!arcadeBackend || !arcadeBackend.play(set, credits))
            return false
        credits = 0
        appCore.add_to_list(moduleId, "recent", { name: title || set, path: "game:" + set, isFolder: false }, 30)
        return true
    }

    // A game a view asks for (navigateTo Play.qml with its set and title);
    // coming back lands on it as listState says. With Start Info Screen on,
    // Play.qml says how to come back first, then starts it. With it off the
    // game starts from the list itself, which stays on the screen while MAME
    // starts and has every key kept from it till the game ends (keyCatcher):
    // Play.qml only says why a game didn't play.
    function playGame(params, listState) {
        if (startInfo) {
            navigateTo("Play.qml", params, listState)
            return
        }
        playing = { game: params.game, title: params.title, listState: listState || {} }
        keyCatcher.forceActiveFocus()
        listLaunch.restart()
    }
    // How long what is on the screen stays before MAME takes it, so that a
    // headless Pi hands that over, not a frame of what was there before.
    readonly property int handOverDelay: 150

    // The list on the screen first, without the window a game may have been
    // chosen in (its options), as a headless Pi hands it over to MAME.
    Timer {
        id: listLaunch
        interval: moduleRoot.handOverDelay
        onTriggered: {
            var game = moduleRoot.playing
            if (!game || moduleRoot.startGame(game.game, game.title))
                return
            moduleRoot.playing = null
            moduleRoot.navigateTo("Play.qml", { game: game.game, title: game.title,
                                                error: arcadeBackend ? arcadeBackend.lastError() : "" },
                                  game.listState)
        }
    }
    // The game playGame() started from the list, while it runs: { game,
    // title, listState }; null for none. Nothing of the menus moves behind
    // it: their effects rest, as for a game's own screen (Play.qml).
    property var playing: null
    onPlayingChanged: root.restFor(moduleRoot, playing !== null)
    Component.onDestruction: root.restFor(moduleRoot, false)

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onFinished(exitCode, reason, notice) {
            var ended = moduleRoot.playing
            if (!ended)
                return
            moduleRoot.playing = null
            if (internalLoader.item)
                internalLoader.item.forceActiveFocus()
            // Back on the list, as after a video; else why it didn't play.
            if (reason !== "ok")
                moduleRoot.navigateTo("Play.qml", { game: ended.game, title: ended.title, failed: true,
                                                    message: notice, output: arcadeBackend.output() },
                                      ended.listState)
        }
    }

    // A toggle of the module's, ON unless it is turned off.
    function isOn(value) {
        return value === undefined || value === null || value === true || String(value).toUpperCase() === "ON"
    }

    // Internal navigation state
    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        root.changeWindow(internalLoader, resolved, { "navParams": params || {} })
    }

    function replaceWith(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
        currentParams = params || {}
        root.changeWindow(internalLoader, resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        root.changeWindow(internalLoader, prev.source, { "navParams": restored })
    }

    // While a game from the list runs, every key (both read the controllers)
    // is kept from the list.
    FocusScope {
        id: keyCatcher
        anchors.fill: parent
        Keys.onPressed: function(event) { event.accepted = true }
        Keys.onReleased: function(event) { event.accepted = true }
    }
    // A game played counts as activity: no screen saver comes up behind it.
    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: arcadeBackend ? arcadeBackend.running : false
        onTriggered: if (idleTracker) idleTracker.resetActivity()
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) {
                // A game asked for, played as Start Info Screen says.
                if (path === "Play.qml" && params && params.game)
                    moduleRoot.playGame(params, listState)
                else
                    moduleRoot.navigateTo(path, params, listState)
            }
            function onReplaceWith(path, params) { moduleRoot.replaceWith(path, params) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    Component.onCompleted: {
        var settings = appCore ? appCore.get_settings() : {}
        var own = (settings.modules && settings.modules[moduleId]) || {}
        screen = own.screen === "Arcade" ? "Arcade" : "Original"
        hintBar = isOn(own.hint_bar)
        helpLine = isOn(own.help_line)
        startInfo = isOn(own.start_info)
        startFilter = own.start_filter || "all"
        listStyle = own.list_style || "Curved"
        border = isOn(own.border)
        var ground = parseInt(own.info_background)
        infoBackground = isNaN(ground) ? 100 : Math.max(0, Math.min(100, ground))
        var saved = own.screen_adjustment || {}
        var sides = {}
        for (var side in screenAdjustment)
            sides[side] = parseInt(saved[side]) || 0
        screenAdjustment = sides
        // The folder looked at again, and MAME asked about any game new in
        // it, before the list asks for what is in it.
        if (arcadeBackend)
            arcadeBackend.refresh()
        // The startup favourite, when the app opens on one (Main.qml); or
        // one of Settings → Arcade's pages: the Arcade screen to adjust
        // (Screen Adjustment), FinalBurn Neo's, or the RetroAchievements
        // account's.
        if (navParams.adjustScreen)
            navigateTo("ArcadeScreen.qml", { adjust: true })
        else if (navParams.fbneo)
            navigateTo("Fbneo.qml", {})
        else if (navParams.achievements)
            navigateTo("Account.qml", {})
        else
            navigateTo(arcadeScreen ? "ArcadeScreen.qml" : "Games.qml", { startupPlay: navParams.startupPlay })
    }
}
