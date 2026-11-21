// Settings' pages, the rows each is made of (views/Settings.qml's showRows()):
// the first screen's, STATUS's and each heading's page's, from what the page
// read (readAll(): appSettings, installedModules) and what the app has
// (appCore, root, displayOutput, audioOutput and the rest). Not a library:
// imported into Settings.qml as Pages, it runs in the page's own scope, its
// properties and ids, as the page's own functions do.

// The first screen: a heading for each page, every row under one of them,
// then Quit. TV / Movie's page is the videos' (this view again, group
// "tv"), with the video modules'; Arcade's the arcade modules' own
// settings on one page, the first module's with the others' under their
// names (ModuleSettings, alsoModules). A module's group is its
// manifest's "settings_group"; the modules of no group are on System's.
function firstRows(items) {
    items.push(groupRow("tv", "The videos' settings: how a picture fills the screen, Transparent Background, the info screen, each video module's own, and the channel logo"))
    var arcade = groupModules("arcade")
    if (arcade.length > 0) {
        var also = arcade.slice(1).map(function(m) { return m.id })
        items.push({
            type: "submenu",
            key: "group_arcade",
            label: "Arcade",
            description: "The games' settings on one page: " + arcade.map(function(m) { return m.name }).join(", "),
            moduleId: arcade[0].id,
            alsoModules: also,
            page: "views/ModuleSettings.qml",
            params: { moduleId: arcade[0].id, alsoModules: also }
        })
    }
    items.push(groupRow("look", "The whole look: the theme, the color scheme, the skin, the effects, what the menus are drawn on, the loading and boot screens and the bars around them"))
    items.push(groupRow("screen", "Where the picture goes and setting the screen up: Display Output, Test Patterns, the screen saver, the mouse pointer and Window Mode"))
    items.push(groupRow("sound", "Which sound card plays, and the music under the menus"))
    items.push({
        type: "submenu",
        key: "remap_controls",
        page: "views/Controllers.qml",
        label: "Controls",
        description: "Every controller connected, each with its own buttons (and a pad's button labels), and extra keys for every keyboard and remote",
        moduleId: ""
    })
    // Dimmed where neither of its rows can be used (a Mac, Windows), with
    // why, as its rows are.
    var network = groupRow("network", "Wi-Fi's networks, and pairing Bluetooth keyboards, gamepads and remotes")
    network.unavailable = networkOff()
    items.push(network)
    items.push(groupRow("startup", "What OSD/OS opens on: Start On, Play at Startup and Cabinet Lock"))
    items.push(groupRow("system", "Update, System Information, the time zone and the keyboard layout, OSD/OS Setup, About, and the modules of no group, such as Scripts"))
    // On the OSD/OS image (autostart) a power menu, and named so.
    items.push({ type: "quit", label: settingsRoot.autostartSession ? "Power" : "Quit",
                 description: settingsRoot.autostartSession ? "Power off or restart the Pi, or leave OSD/OS for a terminal"
                                                            : "Leave OSD/OS" })
    // Broadcast's and Flat Panel's: Cabinet Lock as a monitor's Key
    // Protect, at the foot after a gap (also on Startup's page).
    if (root.twoStep) {
        items.push({ type: "section", label: "" })
        items.push(Object.assign(cabinetLockRow(), { label: "Key Protect" }))
    }
}

// STATUS (Studio Monitor): what is set, a line each, read alone; each
// value as its own row has it, from what that row's value is made of, not
// the whole page it is on (read again as Wi-Fi, Bluetooth or the sound card
// change: followStatus(), followAudio()). A row OSD/OS doesn't offer here
// (Display Output off a Pi, Audio Output where the system picks the card)
// is the system's to say: System, not None, as if there were no output.
function statusRows(items) {
    var output = displayOutput && displayOutput.available ? (displayOutputValue() || "None") : "System"
    var audio = audioOutput && audioOutput.available ? (audioSetting().value || "None") : "System"
    var wifi = "None"
    if (wifiManager && wifiManager.supported) {
        wifi = wifiManager.state === "connected" ? ((wifiManager.current || {}).ssid || "Joined")
             : wifiManager.state === "off" || wifiManager.state === "blocked" || !wifiManager.enabled ? "Off"
             : "Not Joined"
    }
    var bluetooth = "None"
    if (bluetoothManager && bluetoothManager.supported) {
        var linked = (bluetoothManager.devices || []).filter(function(d) { return !!d.connected }).length
        bluetooth = !bluetoothManager.powered ? "Off" : linked > 0 ? linked + " Connected" : "On"
    }
    var lines = [["Output", output],
                 ["Audio", audio],
                 ["Wi-Fi", wifi],
                 ["Bluetooth", bluetooth],
                 ["Theme", themeRow().value],
                 ["Start On", startModuleName()],
                 ["Cabinet Lock", cabinetLockRow().value],
                 ["Version", root.devBuild ? "Dev" : root.appVersion],
                 ["IP Address", ipAddress || "None"]]
    for (var i = 0; i < lines.length; i++)
        items.push({ type: "info", label: lines[i][0], value: lines[i][1] })
}

// A heading's row: its page, this view again; its label the page's
// title unless given.
function groupRow(group, description, label) {
    return { type: "submenu", key: "group_" + group, label: label || groupTitles[group],
             description: description, moduleId: "", page: "views/Settings.qml", params: { group: group } }
}

// THEME's row, Look's first, whose value STATUS shows too: the themes as
// the page listed them (themes(), AppCore::themes()).
function themeRow() {
    return lookRow("osd_theme", "Theme", "None", themes(),
        "The whole look in one: the color scheme, the skin, the effects and the menu music, each row below set back to the theme's as one is chosen (a new one is a folder in the data folder's themes: see the wiki)\n[NONE] OSD/OS's own")
}

// LOOK: the theme and its parts, the effects on a page of their own, and
// what the menus are drawn on.
function lookRows(items) {
    // Theme — the whole look in one (AppCore::themes(), the app's own and
    // the data folder's themes folder; read in Main.qml as root.theme):
    // the color scheme, the skin, the effects and the menu music. Each of
    // those has its row below, THEME (as the theme has it) unless set
    // otherwise; choosing a theme sets them all back to THEME
    // (themeChosen()). Without a theme they have no THEME: "" is what
    // each is without one (Video 1, OSD/OS's own window, off).
    items.push(themeRow())

    // Main.qml's schemes, the custom ones among them (root.allSchemes).
    items.push(partRow("color_scheme", "Color Scheme", Object.keys(root.allSchemes), false,
        "The two colors everything is drawn in (a custom one: see the wiki)\n[THEME] The theme's, else Video 1"))

    // Skin — how the window is dressed, apart from the colours: the
    // shapes of its frame, bars and selected line, and its icons, in the
    // scheme's colours (AppCore::skins(); read in Main.qml as root.skin).
    // Saved by folder name, shown by the skin's name.
    // A skin chosen before skins had their name was saved as app.theme.
    var skinSaved = appSettings["skin"] !== undefined ? appSettings["skin"] : (appSettings["theme"] || "")
    var skinRow = lookRow("skin", "Skin", "Theme", skins(),
        "How the window is dressed: its frame, the title and hint bars, the selected line and the icons, in the color scheme's colors (a new one is a folder in the data folder's skins: see the wiki)\n[THEME] The theme's  [NONE] OSD/OS's own",
        skinSaved)
    skinRow.options.splice(1, 0, "None")
    skinRow.values.splice(1, 0, "Off")
    // A skin saved and gone from its folder since is drawn as none
    // (Main.qml's skinChosen): None, not the theme's.
    var skinGone = skinRow.value === "Theme" && skinSaved !== "" && skinSaved !== "Off"
    if (appSettings["skin"] === "Off" || skinGone || (!appSettings["osd_theme"] && skinRow.value === "Theme"))
        skinRow.value = "None"
    if (!appSettings["osd_theme"]) {
        skinRow.options.shift()
        skinRow.values.shift()
    } else {
        skinRow.themeValue = themeOwn("skin")
    }
    items.push(skinRow)

    items.push(groupRow("effects", "What the text, the background, the selected line and the screen do, and how one window gives way to the next", "Effects"))

    // MENU LAYOUT — how Settings and the main menu are laid out (Main.qml's
    // menuLayout): a list a page at a time, or a camcorder's menu.
    items.push({
        type: "list_single",
        key: "menu_layout",
        label: "Menu Layout",
        options: root.menuLayouts,
        value: root.menuLayoutOf(appSettings["menu_layout"]),
        description: "How Settings and the main menu are laid out\n[STANDARD] A list, a page at a time  [CAMCORDER] As a camcorder's menu: the pages, or the main menu's groups, as icons in a column that stays in place, the rows of the one chosen beside it  [STUDIO MONITOR] As a studio monitor's window over the picture, the pages as icons in a strip, a Status page first  [BROADCAST] As a broadcast monitor's menu, plain blue, a value changed in two steps: select, then ▲ ▼, then select  [FLAT PANEL] As a flat-panel monitor's menu, black, a page of rows at a time, a value changed in two steps",
        moduleId: ""
    })

    // OSD Background — what the menus are drawn on (Components/OsdGround,
    // read in Main.qml): the colour scheme's background over the whole
    // screen, none (black), or a framed window of it behind the menus.
    items.push({
        type: "list_single",
        key: "osd_background",
        label: "OSD Background",
        // Window shown as Box, saved as it was.
        options: ["Full", "Off", "Box"],
        values: ["Full", "Off", "Window"],
        value: ({ Full: "Full", Off: "Off", Window: "Box" })[root.osdBackgroundOf(appSettings["osd_background"])],
        description: "What the menus are drawn on\n[FULL] The color scheme's background, all over  [OFF] None, the menus on black like a deck's on-screen display  [BOX] A framed box of it behind the menus, black around it",
        moduleId: ""
    })

    // OSD Frame — the frame of OSD Background's box, for Box
    // alone (shownWith). Read in Main.qml, drawn by OsdGround.
    items.push({
        type: "list_single",
        key: "osd_frame",
        label: "OSD Frame",
        options: ["On", "Off", "Shadow"],
        value: root.osdFrameOf(appSettings["osd_frame"]),
        description: "The frame of the OSD Background's box\n[ON] A line in the color scheme's color  [OFF] None  [SHADOW] The line and a shadow below and to the right, like a DOS window's",
        shownWith: { key: "osd_background", value: "Box" },
        moduleId: ""
    })

    // The loading screen and the boot screen, together: LOADING EFFECT,
    // LOADING COLORS and BOOT COLORS.
    loadingRows(items)

    // HINT BAR — the key hints on the bar at the foot of every screen
    // (Components/HintBar). Read in Main.qml.
    items.push({
        type: "list_single",
        key: "hint_bar",
        label: "Hint Bar",
        options: ["On", "Off"],
        value: appSettings["hint_bar"] === "Off" ? "Off" : "On",
        description: "The key hints on the bar at the foot of every screen\n[OFF] No bar; the keys work as they do",
        moduleId: ""
    })

    // HELP LINE — the line about the selected row in the box under a
    // menu, this one (Components/HelpLine). Read in Main.qml.
    items.push({
        type: "list_single",
        key: "help_line",
        label: "Help Line",
        options: ["On", "Off"],
        value: appSettings["help_line"] === "Off" ? "Off" : "On",
        description: "The box under a menu with a line about the selected row, this one\n[OFF] No box",
        moduleId: ""
    })

    // STATUS ICONS — what is on, at the title bar's top right and the
    // Arcade screen's bottom right (Components/StatusIcons). Read in
    // Main.qml.
    items.push({
        type: "list_single",
        key: "status_icons",
        label: "Status Icons",
        options: ["On", "Off"],
        value: appSettings["status_icons"] === "Off" ? "Off" : "On",
        description: "What is on, at the top right of the title bar and the bottom right of the Arcade screen: Wi-Fi's signal, Bluetooth, and the Arcade's HI (Save High Scores), trophy (RetroAchievements), globe (Online Play) and network (LAN Play)\n[OFF] None",
        moduleId: ""
    })
}

// The loading screen a video loads behind and the image's boot screen,
// three rows of Look's.
function loadingRows(items) {
    // Loading Effect — the screen a video loads behind is a tape loading
    // (Components/LoadingScreen): its noise and tracking bands, or, off,
    // the deck's display alone on the plain background. Read in Main.qml.
    items.push({
        type: "list_single",
        key: "loading_effect",
        label: "Loading Effect",
        options: ["On", "Off"],
        value: appSettings["loading_effect"] === "Off" ? "Off" : "On",
        description: "While a video loads, a tape's noise and tracking bands roll over its counters\n[OFF] The counters alone, on the plain background",
        moduleId: ""
    })
    // LOADING COLORS and BOOT COLORS — the screen a video loads behind,
    // and the boot screen, in the menus' colors, a theme's or a scheme's
    // (Menu, saved as Theme, the default), or in OSD/OS's own whatever the
    // look: Video 1's white on a VCR's blue (Blue, saved as Default). Read
    // in Main.qml (root.loadingInk, root.bootInk). The boot screen is the
    // OSD/OS image's (BootProgress's available): elsewhere there is none.
    items.push({
        type: "list_single",
        key: "loading_colors",
        label: "Loading Colors",
        options: ["Menu", "Blue"],
        values: ["Theme", "Default"],
        value: appSettings["loading_colors"] === "Default" ? "Blue" : "Menu",
        description: "The colors of the screen a video loads behind\n[MENU] The menus' own, a theme's or the color scheme's  [BLUE] White on blue",
        moduleId: ""
    })
    var bootScreen = typeof bootProgress !== "undefined" && !!bootProgress && bootProgress.available
    items.push({
        type: "list_single",
        key: "boot_colors",
        label: "Boot Colors",
        options: ["Menu", "Blue"],
        values: ["Theme", "Default"],
        value: appSettings["boot_colors"] === "Default" ? "Blue" : "Menu",
        description: "The colors of the boot screen, with its cassette, on the OSD/OS image\n[MENU] The menus' own, a theme's or the color scheme's  [BLUE] White on blue",
        unavailable: bootScreen ? "" : "Only on the OSD/OS image, which has a boot screen",
        moduleId: ""
    })
}

// LOOK / EFFECTS: the theme's effects (Main.qml; their names
// root.*Presets), never on a video, nor while one loads or has its menu
// open. Those the GPU draws need one that can.
function effectRows(items) {
    var noGpu = root.effectsUsable ? "" : "Only where the GPU draws the menus' effects"
    var text = partRow("text_effect", "Text Effect", Object.keys(root.textPresets), true,
        "What the text, the lines and the bars do, on the GPU\n[THEME] The theme's  [RAINBOW] Run through the colors  [SHIMMER] A glint sweeping across now and then  [GLOW] A halo round them  [FLICKER] Like a failing neon sign")
    text.unavailable = noGpu
    items.push(text)
    var background = partRow("background_effect", "Background Effect", Object.keys(root.backgroundPresets), true,
        "What goes on behind the menus, in the window, on the GPU\n[THEME] The theme's  [MATRIX] A rain of glyphs  [FIRE] Pixel flames burning up from under the window  [STARS] A starfield drifting by  [SNOW] Snow falling  [FLAG] A racing flag waving in the window  [PLASMA] An old demo's plasma drifting, dithered in pixels")
    background.unavailable = noGpu
    items.push(background)
    items.push(partRow("selector_effect", "Selector Effect", root.selectorPresets, true,
        "What goes on round the selected line\n[THEME] The theme's  [SPARKLES] Sparks flying off its corners  [WELDING] A welder's sparks bursting from its corners  [LIGHTNING] Bolts crackling out of it  [RAINBOW] A pixel rainbow running down from under it"))
    var screenFx = partRow("screen_effect", "Screen Effect", Object.keys(root.screenPresets), true,
        "A picture tube's or a tape's look over the menus, on the GPU\n[THEME] The theme's  [SCANLINES] Dark lines between the picture's  [CRT] A tube's curve, scanlines and glow  [VHS] A tape's color bleed and noise")
    screenFx.unavailable = noGpu
    items.push(screenFx)
    items.push(partRow("transition", "Transition", root.transitionPresets.filter(root.transitionUsable), true,
        "How one window gives way to the next\n[THEME] The theme's  [FADE] The old fades away  [CUBE] The window turns over like a cube, the next on a face at random  [RIPPLE] The old ripples out as the new ripples in  [WAVE] A wave runs out from a corner and dies away, the new one behind it  [DROP] A drop falls in a corner, the new one inside its spreading ring"))

    // Orb Fog, the last — the fog behind the panels of the Arcade screen's Orb look
    // (OrbLook.qml; Main.qml's orbFogLive), for a theme with that look:
    // the rows are made again as one is chosen (themeChosen()).
    items.push({
        type: "list_single",
        key: "orb_fog",
        label: "Orb Fog",
        options: ["Picture", "Live"],
        value: appSettings["orb_fog"] === "Live" ? "Live" : "Picture",
        description: "The fog drifting behind the Arcade screen's panels in the Orb look\n[PICTURE] A picture of fog, light on the GPU  [LIVE] Worked out as it drifts, as in the look's first design, more work for a Raspberry Pi's GPU",
        unavailable: root.theme && root.theme.arcade === "orb" ? "" : "Only with a theme in the Orb look",
        moduleId: ""
    })
}

// Display Output's value, where the Pi has it (displayOutput.available):
// the output in force, or the one written for the next start. STATUS
// shows it too.
function displayOutputValue() {
    return displayOutput.next !== "" ? "Next: " + displayOutput.nextLabel : displayOutput.currentLabel
}

// SCREEN: where the picture goes, setting the screen up, and what the
// menus do while no one is at them.
function screenRows(items) {
    // DISPLAY OUTPUT — where the picture goes, among the outputs this
    // board has (displayOutput, by its model): select opens them. The Pi
    // reads it at power-on, so a change restarts it, powers it off, or
    // waits for its next start, and the new output stays only when kept
    // on it (views/DisplayKeep.qml). Elsewhere there is nothing to
    // switch, and the row says so. Select opens its questions
    // (views/DisplayOutputPrompts.qml).
    if (displayOutput && displayOutput.available) {
        var next = displayOutput.next
        var nextLabel = displayOutput.nextLabel
        var currentLabel = displayOutput.currentLabel
        var later = displayOutput.nextStartAvailable || displayOutput.powerOffAvailable
        // The one in force's line, as the chooser has it for each.
        var inForce = (displayOutput.options || []).filter(function(o) { return o.id === displayOutput.current })[0]
        items.push({
            type: "display_output",
            label: "Display Output",
            value: displayOutputValue(),
            description: (next !== "" ? "Now " + currentLabel + ", " + nextLabel + " from the next start • " : "")
                         + "Where the picture goes, among the outputs this Pi has: "
                         + (later ? "now, with a restart, or from the next start. " : "a change restarts OSD/OS. ")
                         + "The new output stays only when you keep it on its screen, else the old one comes back in 30 seconds"
                         + (inForce && inForce.help ? "\n[" + currentLabel + "] " + inForce.help : ""),
            moduleId: ""
        })
    } else {
        items.push({
            type: "display_output",
            label: "Display Output",
            value: "",
            description: "Where the picture goes, among the outputs a Raspberry Pi has: HDMI, composite, SCART RGB or a JAMMA board",
            unavailable: "Only on a Raspberry Pi with the OSD/OS image",
            moduleId: ""
        })
    }
    // USE DISPLAY — which of a computer's displays OSD/OS opens on, and mpv
    // plays on (app.display_index, read as it starts: main.cpp). With
    // one display, as a Pi has, there is nothing to choose.
    var displays = Qt.application.screens
    var displayOpts = []
    var displayVals = []
    for (var d = 0; d < displays.length; d++) {
        displayOpts.push((d + 1) + ": " + (displays[d].name || "Display") + " " + displays[d].width + "x" + displays[d].height)
        displayVals.push(String(d))
    }
    var displayAt = displayVals.indexOf(String(parseInt(appSettings["display_index"]) || 0))
    items.push({
        type: "list_single",
        key: "display_index",
        label: "Use Display",
        options: displayOpts,
        values: displayVals,
        value: displayOpts[Math.max(0, displayAt)] || "",
        description: "Which display OSD/OS opens on and plays its videos on, from its next start",
        unavailable: displays.length > 1 ? "" : "Only with more than one display",
        moduleId: ""
    })
    // ROTATION — a monitor turned on its side for vertical games (TATE):
    // Main.qml turns what it draws to stand upright on it, MAME, RetroArch
    // and mpv their pictures, from the next game or video.
    // ROTATION: shown None over the saved Horizontal.
    var monitorVals = ["Horizontal", "Turned Right", "Turned Left"]
    var monitorOpts = ["None", "Turned Right", "Turned Left"]
    items.push({
        type: "list_single",
        key: "monitor",
        label: "Rotation",
        options: monitorOpts,
        values: monitorVals,
        value: monitorOpts[Math.max(0, monitorVals.indexOf(appSettings["monitor"]))],
        description: "A monitor turned on its side for vertical games (TATE): the menus, games and videos are turned to stand upright on it. Vertical games then fill the screen with their own lines, as in their cabinets, and the others lie across its middle. On a desktop, turn the screen in the system's display settings instead\n[NONE] The monitor stands as monitors do  [TURNED RIGHT] Its top on the right as you face it  [TURNED LEFT] Its top on the left",
        moduleId: ""
    })
    // TEST PATTERNS — a picture to set a TV or monitor up by, its own
    // knobs or menu: geometry, size, convergence, brightness, colour
    // (views/TestPatterns.qml).
    items.push({
        type: "submenu",
        key: "test_patterns",
        page: "views/TestPatterns.qml",
        label: "Test Patterns",
        description: "Pictures to set up the screen by, with its own knobs or menu: size and position, geometry, convergence, focus, brightness, contrast and color",
        moduleId: ""
    })

    // SCREEN SAVER — single control: OFF disables, a number of seconds sets
    // the timeout for both menu idle and playback pause (handled inside
    // mpv); five minutes when never saved (Main.qml, MpvController). A
    // number saved before these (30, 60, 120) is read as it is, shown as the
    // nearest of them, and kept until another is chosen.
    var saverVals = ["OFF", "60", "120", "300", "600"]
    var saverOpts = ["Off", "1 min", "2 min", "5 min", "10 min"]
    var saverSaved = appSettings["screensaver_timeout"]
    var saverAt = saverVals.indexOf("300")
    if (saverSaved !== undefined && saverSaved !== null && String(saverSaved) !== "") {
        var saverSec = parseInt(saverSaved)
        saverAt = 0
        if (saverSec > 0) {
            saverAt = 1
            for (var sv = 2; sv < saverVals.length; sv++) {
                if (Math.abs(parseInt(saverVals[sv]) - saverSec) < Math.abs(parseInt(saverVals[saverAt]) - saverSec))
                    saverAt = sv
            }
        }
    }
    items.push({
        type: "list_single",
        key: "screensaver_timeout",
        label: "Screen Saver",
        options: saverOpts,
        values: saverVals,
        value: saverOpts[saverAt],
        description: "A picture moving over the screen after this long without a key, in the menus or on a paused video, against a CRT's burn-in\n[OFF] Never",
        moduleId: ""
    })

    // MOUSE POINTER — drawn by Main.qml: it shows as the mouse moves and
    // goes again after this many seconds without moving.
    var pointerVals = ["off", "2", "5", "10", "30", "always"]
    var pointerOpts = ["Off", "2 sec", "5 sec", "10 sec", "30 sec", "Always"]
    var pointerIdx = pointerVals.indexOf(appSettings["mouse_pointer"] || "5")
    items.push({
        type: "list_single",
        key: "mouse_pointer",
        label: "Mouse Pointer",
        options: pointerOpts,
        values: pointerVals,
        value: pointerOpts[pointerIdx < 0 ? pointerVals.indexOf("5") : pointerIdx],
        description: "A mouse's pointer shows as it moves, and goes again after these seconds without moving\n[OFF] Never shown  [ALWAYS] Stays on screen",
        moduleId: ""
    })

    // WINDOW MODE — Main.qml: the menus in a window on the desktop in
    // place of the whole screen, where there is a desktop (desktopWindow).
    items.push({
        type: "list_single",
        key: "window_mode",
        label: "Window Mode",
        options: ["Off", "On"],
        value: appSettings["window_mode"] === "On" ? "On" : "Off",
        description: "The menus in a window on the desktop instead of the whole screen: 960×720 to start with, moved and sized as any window. F11 or Alt+Enter switches at any time",
        unavailable: settingsRoot.desktopWindow ? "" : "Only on a desktop: Windows, or Linux or BSD with one",
        moduleId: ""
    })
}

// Audio Output's cards as audioOutput has them now (its settingRow()), where
// OSD/OS chooses the card (audioOutput.available), kept as what the page
// shows (audioShown, which followAudio() reads again by). STATUS's Audio
// line is its value.
function audioSetting() {
    var audio = audioOutput.settingRow()
    audioShown = JSON.stringify(audio)
    return audio
}

// SOUND: the sound card the players play through, and the menu music.
function soundRows(items) {
    // AUDIO OUTPUT — the sound card the players play through, among those
    // ALSA has now (audioOutput): ◄ ► change it at once, for a video
    // playing behind the menus too. One chosen but unplugged stays
    // chosen, passed over until it is back.
    if (audioOutput && audioOutput.available) {
        // Without a USB sound card to choose: what the system sees of one
        // (note).
        var audio = audioSetting()
        var usbNote = audio.note || ""
        items.push({
            type: "list_single",
            key: "audio_output",
            label: "Audio Output",
            options: audio.options,
            values: audio.values,
            value: audio.value,
            description: "Which sound card plays: the AV jack, HDMI or a USB sound card, from the moment it is chosen"
                         + (usbNote !== "" ? ". " + usbNote : "")
                         + "\n[AUTO] The Pi's own default, or the first card that can play while that one can't (a Pi 5's HDMI without the picture)  [UNPLUGGED] Chosen but not plugged in: sound goes as on Auto until it is back",
            moduleId: ""
        })
    } else {
        items.push({
            type: "list_single",
            key: "audio_output",
            label: "Audio Output",
            options: [],
            value: "",
            description: "Which sound card plays: the AV jack, HDMI or a USB sound card",
            unavailable: "Only on a Raspberry Pi or a Linux without a sound server: here the system's sound settings choose",
            moduleId: ""
        })
    }

    // Menu Music — a tune under the menus (MenuMusic), never while a video
    // plays: the theme's, or a file of the user's own, picked on the file
    // picker; and how loud.
    items.push(partRow("menu_music", "Menu Music", ["File"], true,
        "A tune under the menus, never while a video plays, loads or has its menu open\n[THEME] The theme's, if it has one  [FILE] A file of your own, below"))
    var musicFile = appSettings["menu_music_file"] || ""
    items.push({
        type: "file",
        key: "menu_music_file",
        label: "Music File",
        value: musicFile !== "" ? musicFile.substring(musicFile.lastIndexOf("/") + 1) : "None",
        path: musicFile,
        picker: { mode: "file", types: ["ogg", "opus", "mp3", "flac", "wav", "m4a", "aac",
                                        "mid", "midi", "xm", "mod", "s3m", "it"],
                  defaultLabel: "No File" },
        description: "The menu music's file, played over and over\n[ENTER] Pick it on the file browser: an MP3, WAV, OGG, FLAC, Opus or M4A, a MIDI file (played with a SoundFont), or a tracker's XM, MOD, S3M or IT",
        shownWith: { key: "menu_music", value: "File" },
        moduleId: ""
    })
    var volume = parseInt(appSettings["menu_music_volume"])
    items.push({
        type: "slider",
        key: "menu_music_volume",
        label: "Music Volume",
        value: isNaN(volume) ? 60 : Math.max(0, Math.min(100, volume)),
        step: 10,
        startText: "QUIET",
        endText: "LOUD",
        description: "How loud the menu music is",
        shownWith: { key: "menu_music", not: "Off" },
        moduleId: ""
    })
}

// Why neither Wi-Fi nor Bluetooth can be used here, "" where one can: the
// first screen's Network row's reason.
function networkOff() {
    if ((wifiManager && wifiManager.supported) || (bluetoothManager && bluetoothManager.supported))
        return ""
    return wifiManager && wifiManager.launcherTooOld
           ? "Needs a newer launcher: run install.sh again, or put a newer OSD/OS image on the card"
           : "Only where NetworkManager or BlueZ runs, as on the OSD/OS image"
}

// NETWORK: joining a Wi-Fi network (NetworkManager), pairing Bluetooth
// (BlueZ); both Linux's.
function networkRows(items) {
    var wifi = !!(wifiManager && wifiManager.supported)
    // Kept off by the launcher alone (an image, or an install.sh, from
    // before the one that lets OSD/OS use NetworkManager): this machine
    // is one where NetworkManager runs, and the reason says what it needs.
    var oldLauncher = !!(wifiManager && wifiManager.launcherTooOld)
    items.push({
        type: "submenu",
        key: "wifi",
        page: "views/Wifi.qml",
        label: "Wi-Fi",
        description: "The networks within reach: join one with its password, or one that doesn't say its name, and the country Wi-Fi keeps to the rules of",
        unavailable: wifi ? "" : oldLauncher ? "Needs a newer launcher: run install.sh again, or put a newer OSD/OS image on the card"
                                             : "Only where NetworkManager runs, as on the OSD/OS image",
        moduleId: ""
    })
    var bluetooth = !!(bluetoothManager && bluetoothManager.supported)
    items.push({
        type: "submenu",
        key: "bluetooth",
        page: "views/Bluetooth.qml",
        label: "Bluetooth",
        description: "Pairing a keyboard, gamepad or remote, and the devices paired",
        unavailable: bluetooth ? "" : "Only where BlueZ runs, as on the OSD/OS image",
        moduleId: ""
    })
}

// Start on Module's value: the name of the module it names while that one
// is turned on, else None. STATUS shows it too.
function startModuleName() {
    var id = appSettings["startup_module"] || "None"
    var module = installedModules.filter(function(m) { return m.enabled && m.id === id })[0]
    return module ? module.name : "None"
}

// STARTUP: what OSD/OS opens on, and keeping it there.
function startupRows(items) {
    // Start on Module — pick a module to auto-launch into on startup.
    // Only present enabled modules as options so disabled ones won't display.
    // The setting is keyed by module id and the picker shows the display name
    // If the stored id isn't an enabled module, the display falls back to None.
    var moduleOpts = ["None"] // display labels
    var moduleVals = ["None"] // stored values (module ids)
    for (var mi = 0; mi < installedModules.length; mi++) {
        if (!installedModules[mi].enabled) continue
        moduleOpts.push(installedModules[mi].name)
        moduleVals.push(installedModules[mi].id)
    }
    items.push({
        type: "list_single",
        key: "startup_module",
        label: "Start On",
        options: moduleOpts,
        values: moduleVals,
        value: startModuleName(),
        description: "Directly launch into a specific module on startup",
        moduleId: ""
    })

    // Play at Startup — a favourite played straight after the boot screen,
    // ahead of Start on Module. It is chosen in its module (right on it,
    // OPTIONS, PLAY AT STARTUP); here it can only be turned off.
    var startupFav = appSettings["startup_favorite"]
    var favName = startupFav && startupFav.path ? (startupFav.name || "Favorite") : ""
    items.push({
        type: "list_single",
        key: "startup_favorite",
        label: "Play at Startup",
        options: favName !== "" ? ["None", favName] : ["None"],
        values: favName !== "" ? ["", startupFav] : [""],
        value: favName !== "" ? favName : "None",
        description: "A favorite played straight after the boot screen, instead of opening a module\nChoose one with right on it in its module: OPTIONS, PLAY AT STARTUP",
        moduleId: ""
    })

    // Startup From — where that favourite begins. Either way it begins
    // without asking: a resume prompt is no question to put to a player
    // switched on to play. For a favourite set above (shownWith), of a
    // module whose player reads it, Local Files' or YouTube's: an Arcade
    // game, or a Netflix or Prime Video title, has nowhere to resume.
    var resumes = ["com.osdos.local_files", "com.osdos.youtube"]
    items.push({
        type: "list_single",
        key: "startup_from",
        label: "Play From",
        options: ["Resume", "Beginning"],
        value: appSettings["startup_from"] === "Beginning" ? "Beginning" : "Resume",
        description: "Where the favorite played at startup begins, without asking\n[RESUME] Where it was stopped  [BEGINNING] From the start",
        shownWith: { key: "startup_favorite", not: "None" },
        unavailable: favName !== "" && resumes.indexOf(startupFav.module) < 0
                     ? "Only with a Local Files or YouTube favorite" : "",
        moduleId: ""
    })

    items.push(cabinetLockRow())
}

// CABINET LOCK — OSD/OS kept on Start on Module, the main menu and
// Settings opened with the unlock buttons alone (views/CabinetLock.qml).
// On with no module to stay on (Start on Module set to None since, or
// its module turned off) it locks nothing (Main.qml's cabinetLocked()),
// and the row says so.
function cabinetLockRow() {
    var on = appSettings["cabinet_lock"] === "On"
    var idle = on && !startModuleThere()
    return {
        type: "submenu",
        key: "cabinet_lock",
        page: "views/CabinetLock.qml",
        label: "Cabinet Lock",
        value: !on ? "Off" : idle ? "On (No Module)" : "On",
        description: (idle ? "Locks nothing now: there is no Start On module to stay on\n" : "")
                     + "Keeps OSD/OS on its Start On module, a cabinet's Arcade screen: the main menu, Settings and Quit open only with the unlock buttons held together for three seconds",
        moduleId: ""
    }
}
// Whether Start on Module names a module that is turned on, as
// AppCore::startupModuleEntryPoint() has it.
function startModuleThere() {
    var id = appSettings["startup_module"] || "None"
    return installedModules.some(function(m) { return m.enabled && m.id === id })
}

// SYSTEM: the update, this device's System Information, the system's
// time zone and keyboard layout, OSD/OS Setup's settings, About, and the
// modules of no group, each a page.
function systemRows(items) {
    items.push({
        type: "submenu",
        key: "software_update",
        page: "views/Update.qml",
        label: "Update",
        description: "A newer OSD/OS, downloaded and installed",
        moduleId: ""
    })
    // SYSTEM INFORMATION — this device: its name on the network (the row's
    // value), what it is and its connections (views/SystemInfo.qml).
    var info = typeof systemInfo !== "undefined" && systemInfo ? systemInfo : null
    items.push({
        type: "submenu",
        key: "system_info",
        page: "views/SystemInfo.qml",
        label: "System Information",
        value: info ? info.deviceName : "",
        description: "This device's name, which tells it apart from others on the network, and what it is: its model, OS,"
                     + " memory, storage and temperature, and each network connection's IP and MAC addresses",
        moduleId: ""
    })
    // TIME ZONE and KEYBOARD — the system's, as its files say
    // (systemSettings). On the OSD/OS image select opens the list to pick
    // another from, which its root helper sets (osdos-system.socket);
    // elsewhere they are the system's own settings' to change.
    var system = settingsRoot.system
    var systemOff = system && system.available ? "" : "Only on the OSD/OS image"
    items.push({
        type: "submenu",
        key: "time_zone",
        label: "Time Zone",
        value: !system ? "" : system.busy ? "Setting" : system.timeZone !== "" ? system.timeZone : "Not Set",
        description: "The time zone the clock goes by: Weather's, a film's dates and the logs'",
        unavailable: systemOff,
        moduleId: ""
    })
    items.push({
        type: "submenu",
        key: "keyboard",
        label: "Keyboard Layout",
        value: !system ? "" : system.busy ? "Setting" : system.keyboardName !== "" ? system.keyboardName : "Not Set",
        description: "The keyboard's layout at Exit to Terminal, where a name and a password are typed: OSD/OS's own menus take any keyboard as it is",
        unavailable: systemOff,
        moduleId: ""
    })
    // SSH — the image's SSH server on or off, as the system has it
    // (systemSettings.ssh), set through its root helper (setSsh(), the
    // row's toggle in Settings.qml's setChoice()): Setting while on its way,
    // a refusal said as Time Zone's is. Its note, why no one could sign in
    // yet (no password or key set), opens the help line while it is on. No
    // password here: OSD/OS Setup sets it and the keys.
    var sshOn = !!(system && system.ssh)
    var sshNote = system && system.sshNote ? String(system.sshNote) : ""
    items.push({
        type: "list_single",
        key: "ssh",
        systemSetting: true,
        label: "SSH",
        options: ["Off", "On"],
        value: system && system.busy ? "Setting" : sshOn ? "On" : "Off",
        description: (sshOn && sshNote !== "" ? sshNote + "\n" : "")
                     + "Lets a computer on your network sign in to this device; its password and keys are set with OSD/OS Setup"
                     + "\n[ON] SSH runs  [OFF] SSH is stopped",
        unavailable: systemOff,
        moduleId: ""
    })
    // EXPORT TO OSD/OS SETUP — this device's settings as QR codes, which
    // open OSD/OS Setup's page with them (views/SetupExport.qml).
    items.push({
        type: "submenu",
        key: "export_setup",
        page: "views/SetupExport.qml",
        label: "Export to OSD/OS Setup",
        description: "This device's settings as QR codes: a phone opens OSD/OS Setup's page (osdos.tv/setup) from"
                     + " them, filled in, passwords too, to change what you need and make a card's file of",
        moduleId: ""
    })
    // PASTE OSD/OS SETUP — the settings copied on OSD/OS Setup's page,
    // set once the question is answered (pasteSetup(),
    // AppCore::applySetupText()), where there is a clipboard to paste them
    // from: a computer's.
    items.push({
        type: "paste_setup",
        label: "Paste OSD/OS Setup",
        description: "The settings copied on OSD/OS Setup's page (osdos.tv/setup, Copy for a computer), set here once you say so:"
                     + " OSD/OS's own and the RetroAchievements account. The device's name, Wi-Fi and the"
                     + " rest are set on an OSD/OS card alone",
        unavailable: settingsRoot.canPaste ? "" : "Only on a computer, from its clipboard; an OSD/OS card takes OSD/OS Setup's file",
        moduleId: ""
    })
    // ABOUT — what OSD/OS is, who makes it, what it is made of and under
    // which license (views/About.qml).
    items.push({
        type: "submenu",
        key: "about",
        page: "views/About.qml",
        label: "About",
        description: "What OSD/OS is, who makes it, what it is made of and under which license",
        moduleId: ""
    })
    // The modules of no group with settings, a page each.
    moduleRows(items, groupModules(""))
}

// TV / Movie's page: how a picture fills the screen, Transparent Background
// and the info screen, then the video modules (manifest "settings_group":
// "tv") under MODULES, then the rest of the videos' rows under VIDEO.
function tvRows(items) {
    var video = []
    videoRows(video)
    var first = ["video_scaling", "transparent_background", "info_screen"]
    for (var f = 0; f < first.length; f++)
        items.push(video.filter(function(row) { return row.key === first[f] })[0])
    moduleRows(items, groupModules("tv"))
    items.push({ type: "section", label: "Video" })
    for (var v = 0; v < video.length; v++) {
        if (first.indexOf(video[v].key) < 0)
            items.push(video[v])
    }
}
// The videos' own rows, the first three and then the VIDEO heading's in
// their order.
function videoRows(items) {
    // Scaling — how a picture of another shape fills the screen: a 16:9 film
    // on a 4:3 tube above all. Every video module's settings can override
    // it for that module. It replaces Auto Crop, whose ON reads as Pan &
    // Scan until a Scaling is chosen. The OSC's CROP still toggles live.
    var scalingOpts = ["Letterbox", "14:9", "Pan & Scan", "Anamorphic"]
    var scaling = appSettings["video_scaling"]
                  || (appSettings["auto_crop"] === "On" ? "Pan & Scan" : "Letterbox")
    items.push({
        type: "list_single",
        key: "video_scaling",
        label: "Scaling",
        options: scalingOpts,
        value: scalingOpts.indexOf(scaling) >= 0 ? scaling : "Letterbox",
        description: "How a 16:9 picture fills the 4:3 screen, in every module that doesn't set its own\n[LETTERBOX] All of it, bars above and below  [14:9] A little of the sides cut, thinner bars  [PAN & SCAN] Fills it, the sides cut  [ANAMORPHIC] Fills it squeezed, for a TV set to 16:9",
        moduleId: ""
    })

    // Transparent Background — video played inside the app's own window,
    // so back from it returns to the menus with the picture going on
    // behind them (MpvController). A slider, like the deck's tape bar,
    // from TRANSPARENT to SOLID: how solid the menus' ground is over the
    // picture, in tenths, and seen at once over a video behind them.
    // SOLID hides the picture, which plays on, sound and all; select turns
    // the setting off and on, as it does a module's toggles. It needs
    // libmpv: without it the row says so.
    var embedded = mpvController.embeddedAvailable()
    var backgroundRaw = appSettings["transparent_background"]
    var backgroundOn = embedded && root.backgroundOn(backgroundRaw)
    items.push({
        type: "slider",
        key: "transparent_background",
        label: "Transparent Background",
        on: backgroundOn,
        offValue: "Off",
        // Off, the bar waits where turning it on puts it.
        value: backgroundOn ? root.solidityOf(backgroundRaw) : 40,
        step: 10,
        startText: "TRANSPARENT",
        endText: "SOLID",
        description: "How much of a video shows through the menus when back returns to them and leaves it playing behind, until you play something else or stop it on the main menu, whose first row takes it back to full screen\n[SOLID] None of it, but it plays on, sound and all  [ENTER] On or off: off, back stops the video, as it always has",
        unavailable: embedded ? "" : "Needs libmpv, which isn't installed",
        moduleId: ""
    })

    // Video Output Range, the RGB range mpv converts YUV into. mpv's default
    // is full range; a display expecting studio (limited) levels renders that
    // as crushed blacks and blown whites, and the reverse reads as washed-out
    // gray. Takes effect on the next video, and at once on one playing
    // behind the menus (MpvController::followVideoSetting(), as Scaling).
    // The key keeps mpv's own name for the option; the label uses the wording
    // TVs and GPU drivers use for the same control (HDMI RGB range, DRM
    // "Broadcast RGB"), since that's what the setting is matching.
    items.push({
        type: "list_single",
        key: "video_output_levels",
        label: "Video Levels",
        options: ["Auto", "Limited", "Full"],
        value: appSettings["video_output_levels"] || "Auto",
        description: "Match the color range your display expects\n[LIMITED] If blacks crush  [FULL] If blacks look gray",
        moduleId: ""
    })

    // 1080p Playback — for a device whose smooth decode path can't
    // crop/zoom (the Pi 3 overlay path). Default ON; turning it off
    // restores the crop-capable video output. Takes effect on the next
    // video. Elsewhere there is nothing to trade: the row says so.
    items.push({
        type: "list_single",
        key: "smooth_playback",
        label: "1080p Playback",
        options: ["On", "Off"],
        value: appSettings["smooth_playback"] || "On",
        description: "[ON] Enable 1080p content playback, crop, 14:9 and Pan & Scan will not function\n[OFF] Enable crop, 1080p content playback will stutter",
        unavailable: mpvController.hasSmoothPlaybackTradeoff() ? "" : "Only where smooth 1080p playback can't crop, as on a Raspberry Pi 3",
        moduleId: ""
    })

    // CHANNEL LOGO — OSD/OS's logo in a corner of the picture while a
    // video plays, drawn by mpv (scripts/mpv-logo.lua, loaded by
    // MpvController at each launch, so it applies from the next video).
    var logoVals = ["off", "tl", "tr", "bl", "br", "all"]
    var logoOpts = ["Off", "Top Left", "Top Right", "Bottom Left", "Bottom Right", "All Corners"]
    var logoIdx = logoVals.indexOf(appSettings["video_logo"] || "off")
    items.push({
        type: "list_single",
        key: "video_logo",
        label: "Channel Logo",
        options: logoOpts,
        values: logoVals,
        value: logoOpts[logoIdx < 0 ? 0 : logoIdx],
        description: "A logo in a corner of the picture while a video plays, like a channel's: OSD/OS's, or your own (Logo Image), from the next video on\n[ALL CORNERS] In all four  [OFF] None",
        moduleId: ""
    })

    // LOGO IMAGE — the channel logo's picture: OSD/OS's, or one of the
    // user's own, picked on the file picker (views/FilePicker.qml) with
    // select. Offered while Channel Logo is on; MpvController reads it.
    var logoImage = appSettings["video_logo_image"] || ""
    items.push({
        type: "file",
        key: "video_logo_image",
        label: "Logo Image",
        value: logoImage !== "" ? logoImage.substring(logoImage.lastIndexOf("/") + 1) : "OSD/OS",
        path: logoImage,
        picker: { mode: "file", types: ["png", "jpg", "jpeg", "svg", "gif", "bmp", "webp"],
                  defaultLabel: "OSD/OS Logo" },
        description: "The channel logo's picture, from the next video on: OSD/OS's, or one of your own, as tall as OSD/OS's\n[ENTER] Pick a picture on the file browser, a PNG with a clear background best",
        shownWith: { key: "video_logo", not: "Off" },
        moduleId: ""
    })

    // INFO SCREEN — a film's details in the trees (Netflix, Prime Video,
    // YouTube): with right on it always, and with a number also on its own
    // once the cursor has rested on it that many seconds. One control, like
    // the screen saver's.
    var infoVals = ["off", "key", "1", "2", "3", "5"]
    // With ►: ► as the controller in use names it (root.hints).
    var withKey = "With " + String(root.hints.browse || "[►]").replace(/^\[|\]$/g, "")
    var infoOpts = ["Off", withKey, "1 sec", "2 sec", "3 sec", "5 sec"]
    var infoIdx = infoVals.indexOf(appSettings["info_screen"] || "3")
    // Theirs alone: with none of the three turned on there is no tree
    // to show it in, and the row says so.
    var infoModules = ["com.osdos.netflix", "com.osdos.prime_video", "com.osdos.youtube"]
    var infoRead = installedModules.some(function(m) { return m.enabled && infoModules.indexOf(m.id) >= 0 })
    items.push({
        type: "list_single",
        key: "info_screen",
        label: "Info Screen",
        options: infoOpts,
        values: infoVals,
        value: infoOpts[infoIdx < 0 ? infoVals.indexOf("3") : infoIdx],
        description: "A film's details in Netflix, Prime Video and YouTube: with right on a film, and with a time also on its own once the cursor has rested on the film that long\n[" + withKey + "] Only with right on a film  [OFF] Never",
        unavailable: infoRead ? "" : "Only with Netflix, Prime Video or YouTube enabled",
        moduleId: ""
    })

    // VIDEO ARGUMENTS — mpv's options for how a video is put on the
    // screen and decoded, typed in place of those OSD/OS picks for the
    // device (MpvController's videoArgs): for a setup its own don't
    // suit. Nothing typed is OSD/OS's own.
    var videoArgs = String(appSettings["mpv_video_args"] || "").trim()
    items.push({
        type: "submenu",
        key: "mpv_video_args",
        label: "Video Arguments",
        value: videoArgs !== "" ? videoArgs : "Default",
        description: "For experts: mpv's own options for the video output and decoder, in place of those OSD/OS picks for this device, such as --vo=gpu --hwdec=no. From the next video\n[DEFAULT] Nothing typed: OSD/OS's own",
        moduleId: ""
    })
}

// The installed modules with settings of a group ("" for none, as a
// group with no page here counts), and their rows under the MODULES
// heading, a page each.
function groupModules(group) {
    return installedModules.filter(function(m) {
        var own = m.settings_group === "tv" || m.settings_group === "arcade" ? m.settings_group : ""
        return m.has_settings && own === group
    })
}
// Each says whether the module is on (its "enabled", which the main menu
// and its backend follow), ◄ ► turning it on and off there (Settings.qml's
// toggleModule()); select opens its settings. In AppCore's order.
function moduleRows(items, modules) {
    if (modules.length === 0)
        return
    items.push({ type: "section", label: "Modules" })
    for (var i = 0; i < modules.length; i++)
        items.push(moduleRow(modules[i]))
}
function moduleRow(m) {
    return { type: "module", key: "enabled", label: m.name, moduleId: m.id,
             on: !!m.enabled, value: m.enabled ? "On" : "Off",
             description: "Shows " + m.name + " on the main menu\n[ENTER] Opens its settings",
             page: "views/ModuleSettings.qml", params: { moduleId: m.id, alsoModules: [] } }
}

// A row choosing one of the themes or the skins there are (AppCore's
// themes(), skins(): [{ id, name }]), or none (`none`, saved as ""):
// saved by folder name, shown by name, two of one name apart by their
// folders.
// `saved` is the setting as saved, appSettings[key] unless given.
function lookRow(key, label, none, looks, description, saved) {
    if (saved === undefined)
        saved = appSettings[key]
    var row = { type: "list_single", key: key, label: label, options: [none], values: [""],
                value: none, description: description, moduleId: "" }
    for (var i = 0; i < looks.length; i++) {
        var name = looks[i].name
        if (row.options.indexOf(name) >= 0)
            name += " (" + looks[i].id + ")"
        row.options.push(name)
        row.values.push(looks[i].id)
        if (looks[i].id === saved)
            row.value = name
    }
    return row
}

// A row for one of the theme's parts: THEME (saved as "", as the theme
// has it) while there is a theme, OFF (with `off`, saved as "Off") and
// the choices. Without a theme, "" shows as the first of those, what it
// is then. A saved value that isn't among the choices (a custom scheme
// removed since, an effect this system can't draw) shows as what is
// drawn in its place (partInForce()), not as the row's first choice.
// A choice shown by a shorter name than it is saved by, so that its row
// shows it whole beside its label in Camcorder's narrower lines.
function shortName(name) {
    return ({ "Checkered Flag": "Flag", "SMPTE ECR 1-1978": "SMPTE ECR" })[name] || name
}
function partRow(key, label, choices, off, description) {
    var themed = !!appSettings["osd_theme"]
    var options = (themed ? ["Theme"] : []).concat(off ? ["Off"] : [], choices.map(shortName))
    var values = (themed ? [""] : []).concat(off ? ["Off"] : [], choices)
    var saved = appSettings[key] === undefined ? "" : String(appSettings[key])
    var at = values.indexOf(saved)
    if (at < 0)
        at = values.indexOf(partInForce(key, saved))
    return { type: "list_single", key: key, label: label, options: options, values: values,
             value: options[Math.max(0, at)], description: description, moduleId: "",
             themeValue: themed ? themeOwn(key) : undefined }
}
// What the theme in force (Main.qml's root.theme, AppCore::theme()) has for
// one of its parts, as the help line says it while the row is on THEME
// (Settings.qml's helpText: "Theme's: Glow"): a scheme's, a skin's or an
// effect's name, the music's file; "its own" for one the theme draws
// itself; "none" for none, as the theme is then drawn.
function themeOwn(key) {
    var theme = root.theme || {}
    if (key === "color_scheme")
        return typeof theme.colors === "string" ? theme.colors : theme.colors ? "its own" : "Video 1"
    if (key === "skin")
        return theme.skin && theme.skin.name ? theme.skin.name : theme.skin ? "its own" : "none"
    if (key === "menu_music") {
        var music = String(theme.music || "")
        return music !== "" ? music.substring(music.lastIndexOf("/") + 1) : "none"
    }
    var kinds = { text_effect: "text", background_effect: "background", selector_effect: "selector",
                  screen_effect: "screen", transition: "transition" }
    var effect = (theme.effects || {})[kinds[key]]
    return typeof effect === "string" ? effect : effect ? "its own" : "none"
}
// What Main.qml makes of a part's saved value that names nothing it has:
// a scheme not there is Video 1 (root.scheme), an effect's unknown name
// none (effectOf(), nameOf()), a transition this system can't play
// (Ripple, Wave or Drop without a GPU) Fade (WindowChange's caught(), by
// root.transitionUsable()) and an unknown one none. Menu Music's is the
// theme's, the row's first choice.
function partInForce(key, saved) {
    if (key === "color_scheme")
        return "Video 1"
    if (key === "transition")
        return root.transitionPresets.indexOf(saved) >= 0 ? "Fade" : "Off"
    return key === "menu_music" ? undefined : "Off"
}
