import QtQuick
import Components

FocusScope {
    id: settingsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    property var appSettings: ({})
    property var installedModules: []

    // Flat model: mix of section headers and rows
    property var settingsItems: []
    // Counted up as a row changes in place, so the rows read theirs again:
    // the model stays as it is, and the list where it is.
    property int revision: 0


    // Quit overlay choices. Under the autostart service (headless RPi) the quit menu has an
    // "Exit to Terminal" option that drops to a tty1 login without powering off, and a
    // "Restart" that reboots (where the stop helper knows how, see canRestartSystem());
    // they are not needed on macOS/Desktop or when run by hand, so it's yes/no for that case.
    property bool autostartSession: false
    property bool canRestart: false

    // Display Output, read from displayOutput in buildModel(): the outputs
    // this board has, the one in force, the one written for the next start,
    // the ways to switch the launcher knows, and the one chosen to switch to.
    property string boardModel: ""
    property var outputOptions: []
    property string outputCurrent: ""
    property string outputCurrentLabel: ""
    property string outputNext: ""
    property string outputNextLabel: ""
    property bool outputAtNextStart: false
    property bool outputPowerOff: false
    property var outputChoice: ({})
    property string outputFailure: ""
    property var quitOptions: settingsRoot.autostartSession
        ? [{ label: "Power Off",        action: "quit"     }]
          .concat(settingsRoot.canRestart ? [{ label: "Restart", action: "restart" }] : [])
          .concat([{ label: "Exit to Terminal", action: "terminal" },
                   { label: "Cancel",           action: "cancel"   }])
        : [{ label: "Yes", action: "quit" },
           { label: "No",  action: "cancel" }]

    // `at`: the row the cursor stays on, else the one it was on last.
    function buildModel(at) {
        var cfg = appCore.get_settings()
        appSettings = cfg.app || {}
        installedModules = appCore.get_installed_modules()
        autostartSession = appCore.isAutostartSession()
        canRestart = appCore.canRestartSystem()

        var items = []

        // APPLICATION section
        // Theme — the whole look in one (AppCore::themes(), the app's own and
        // the data folder's themes folder; read in Main.qml as root.theme):
        // the color scheme, the skin, the effects and the menu music. Each of
        // those has its row below, THEME (as the theme has it) unless set
        // otherwise; choosing a theme sets them all back to THEME
        // (themeChosen()). Without a theme they have no THEME: "" is what
        // each is without one (Video 1, OSD/OS's own window, off).
        items.push(lookRow("osd_theme", "Theme", "None", appCore.themes(),
            "The whole look in one: the color scheme, the skin, the effects and the menu music\n[NONE] OSD/OS's own  Choosing one sets each row below back to the theme's  A new one is a folder in the data folder's themes, see the wiki"))

        var colorOpts = ["Video 1","Late Night","Synthwave","Terminal","T-120","Amber","Kinescope","SMPTE ECR 1-1978"]
        // Adding a new approach to add multiple custom themes at once
        var cThemes = appCore.getCustomColorSchemes()
        if (Object.keys(cThemes).length > 0) {
            for (var cTheme in cThemes) {
                if (!colorOpts.includes(cTheme) && Object.keys(cThemes[cTheme]).length === 5) colorOpts.push(cTheme)
            }
        }
        // Still support the single-theme approach
        var custom = appCore.getCustomColorScheme()
        if (!colorOpts.includes("Custom") && Object.keys(custom).length === 5) colorOpts.push("Custom")
        items.push(partRow("color_scheme", "Color Scheme", colorOpts, false,
            "The two colors everything is drawn in\n[THEME] The theme's, else Video 1  Please see the wiki for details on adding a custom one"))

        // Skin — how the window is dressed, apart from the colours: the
        // shapes of its frame, bars and selected line, and its icons, in the
        // scheme's colours (AppCore::skins(); read in Main.qml as root.skin).
        // Saved by folder name, shown by the skin's name.
        // A skin chosen before skins had their name was saved as app.theme.
        var skinRow = lookRow("skin", "Skin", "Theme", appCore.skins(),
            "How the window is dressed: its frame, the title and hint bars, the selected line and the icons, in the color scheme's colors\n[THEME] The theme's  [NONE] OSD/OS's own  A new one is a folder in the data folder's skins, see the wiki",
            appSettings["skin"] !== undefined ? appSettings["skin"] : (appSettings["theme"] || ""))
        skinRow.options.splice(1, 0, "None")
        skinRow.values.splice(1, 0, "Off")
        if (appSettings["skin"] === "Off" || (!appSettings["osd_theme"] && skinRow.value === "Theme"))
            skinRow.value = "None"
        if (!appSettings["osd_theme"]) {
            skinRow.options.shift()
            skinRow.values.shift()
        }
        items.push(skinRow)

        // The effects (Main.qml; their names root.*Presets): never on a video,
        // nor while one loads or has its menu open. Those the GPU draws are
        // offered only where it can.
        if (root.effectsUsable) {
            items.push(partRow("text_effect", "Text Effect", Object.keys(root.textPresets), true,
                "What the text, the lines and the bars do, on the GPU\n[THEME] The theme's  [RAINBOW] Run through the colors  [SHIMMER] A glint sweeping across now and then  [GLOW] A halo round them  [FLICKER] Like a failing neon sign"))
            items.push(partRow("background_effect", "Background Effect", Object.keys(root.backgroundPresets), true,
                "What goes on behind the menus, in the window, on the GPU\n[THEME] The theme's  [MATRIX] A rain of glyphs  [FIRE] Pixel flames burning up from under the window  [STARS] A starfield drifting by  [SNOW] Snow falling"))
        }
        items.push(partRow("selector_effect", "Selector Effect", root.selectorPresets, true,
            "What goes on round the selected line\n[THEME] The theme's  [SPARKLES] Sparks flying off its corners  [WELDING] A welder's sparks bursting from its corners  [LIGHTNING] Bolts crackling out of it  [RAINBOW] A pixel rainbow running down from under it"))
        if (root.effectsUsable) {
            items.push(partRow("screen_effect", "Screen Effect", Object.keys(root.screenPresets), true,
                "A picture tube's or a tape's look over the menus, on the GPU\n[THEME] The theme's  [SCANLINES] Dark lines between the picture's  [CRT] A tube's curve, scanlines and glow  [VHS] A tape's color bleed and noise"))
        }
        items.push(partRow("transition", "Transition", root.transitionPresets.filter(root.transitionUsable), true,
            "How one window gives way to the next\n[THEME] The theme's  [FADE] The old fades away  [CUBE] The window turns over like a cube, the next on a face at random  [CUBE LEFT / RIGHT / UP / DOWN] Always turning that way  [RIPPLE] The old ripples out as the new ripples in  [WAVE] A wave runs out from a corner and dies away, the new one behind it  [DROP] A drop falls in a corner, the new one inside its spreading ring"))

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

        // OSD Background — what the menus are drawn on (Components/OsdGround,
        // read in Main.qml): the colour scheme's background over the whole
        // screen, none (black), or a framed window of it behind the menus.
        items.push({
            type: "list_single",
            key: "osd_background",
            label: "OSD Background",
            options: ["Full", "Off", "Window"],
            value: root.osdBackgroundOf(appSettings["osd_background"]),
            description: "What the menus are drawn on\n[FULL] The color scheme's background, all over  [OFF] None, the menus on black like a deck's on-screen display  [WINDOW] A framed window of it behind the menus, black around it",
            moduleId: ""
        })

        // Window Frame — the frame of OSD Background's window, offered only
        // while it is Window (shownWith): the row comes and goes as that
        // changes. Read in Main.qml, drawn by OsdGround.
        items.push({
            type: "list_single",
            key: "osd_frame",
            label: "Window Frame",
            options: ["On", "Off", "Shadow"],
            value: root.osdFrameOf(appSettings["osd_frame"]),
            description: "The frame of the OSD Background's window\n[ON] A line in the color scheme's color  [OFF] None  [SHADOW] The line and a shadow below and to the right, like a DOS window's",
            shownWith: { key: "osd_background", value: "Window" },
            moduleId: ""
        })

        // Start on Module — pick a module to auto-launch into on startup.
        // Only present enabled modules as options so disabled ones won't display.
        // The setting is keyed by module id and the picker shows the display name
        // If the stored id isn't an enabled module, the display falls back to None.
        var moduleOpts = ["None"] // display labels
        var moduleVals = ["None"] // stored values (module ids)
        var startupId = appSettings["startup_module"] || "None"
        var startupValue = "None"
        for (var mi = 0; mi < installedModules.length; mi++) {
            if (!installedModules[mi].enabled) continue
            moduleOpts.push(installedModules[mi].name)
            moduleVals.push(installedModules[mi].id)
            if (installedModules[mi].id === startupId) startupValue = installedModules[mi].name
        }
        items.push({
            type: "list_single",
            key: "startup_module",
            label: "Start on Module",
            options: moduleOpts,
            values: moduleVals,
            value: startupValue,
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
            description: "A favourite played straight after the boot screen, instead of opening a module\nChoose one with right on it in its module: OPTIONS, PLAY AT STARTUP",
            moduleId: ""
        })

        // Startup From — where that favourite begins. Either way it begins
        // without asking: a resume prompt is no question to put to a player
        // switched on to play. Offered while there is one.
        if (favName !== "") {
            items.push({
                type: "list_single",
                key: "startup_from",
                label: "Startup From",
                options: ["Resume", "Beginning"],
                value: appSettings["startup_from"] === "Beginning" ? "Beginning" : "Resume",
                description: "Where the favourite played at startup begins, without asking\n[RESUME] Where it was stopped  [BEGINNING] From the start",
                moduleId: ""
            })
        }

        // Smooth Playback — only shown on devices whose smooth decode path can't
        // crop/zoom (the Pi 3 overlay path). Default ON; turning it off restores the
        // crop-capable video output. Takes effect on the next video.
        if (mpvController.hasSmoothPlaybackTradeoff()) {
            items.push({
                type: "list_single",
                key: "smooth_playback",
                label: "1080p Playback",
                options: ["On", "Off"],
                value: appSettings["smooth_playback"] || "On",
                description: "[ON] Enable 1080p content playback, crop, 14:9 and Pan & Scan will not function\n[OFF] Enable crop, 1080p content playback will stutter",
                moduleId: ""
            })
        }

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
        // libmpv, so it is offered only where that is installed.
        if (mpvController.embeddedAvailable()) {
            var backgroundRaw = appSettings["transparent_background"]
            var backgroundOn = root.backgroundOn(backgroundRaw)
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
                moduleId: ""
            })
        }

        // Video Output Range, the RGB range mpv converts YUV into. mpv's default
        // is full range; a display expecting studio (limited) levels renders that
        // as crushed blacks and blown whites, and the reverse reads as washed-out
        // gray. Takes effect on the next video.
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

        // LOADING SCREEN COLORS, BOOT SCREEN COLORS — those two screens in the
        // colours in force, a theme's or a scheme's (Theme, the default), or
        // in OSD/OS's own whatever the look: Video 1's white on a VCR's blue.
        // Read in Main.qml (root.loadingInk, root.bootInk).
        items.push({
            type: "list_single",
            key: "loading_colors",
            label: "Loading Screen Colors",
            options: ["Theme", "Default"],
            value: appSettings["loading_colors"] === "Default" ? "Default" : "Theme",
            description: "The colours of the screen a video loads behind\n[THEME] The theme's or color scheme's  [DEFAULT] White on blue",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "boot_colors",
            label: "Boot Screen Colors",
            options: ["Theme", "Default"],
            value: appSettings["boot_colors"] === "Default" ? "Default" : "Theme",
            description: "The colours of the boot screen, with its cassette, on the OSD/OS image\n[THEME] The theme's or color scheme's  [DEFAULT] White on blue",
            moduleId: ""
        })

        // CHANNEL LOGO — OSD/OS's logo in a corner of the picture while a
        // video plays, drawn by mpv (scripts/mpv-logo.lua, loaded by
        // MpvController at each launch, so it applies from the next video).
        var logoVals = ["off", "tl", "tr", "bl", "br", "all"]
        var logoOpts = ["Off", "Top Left", "Top Right", "Bottom Left", "Bottom Right", "All Corners"]
        var logoIdx = logoVals.indexOf(appSettings["video_logo"] || "tr")
        items.push({
            type: "list_single",
            key: "video_logo",
            label: "Channel Logo",
            options: logoOpts,
            values: logoVals,
            value: logoOpts[logoIdx < 0 ? logoVals.indexOf("tr") : logoIdx],
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

        // SCREEN SAVER section — single control: OFF disables, a number sets the
        // timeout for both menu idle and playback pause (handled inside mpv).
        items.push({
            type: "list_single",
            key: "screensaver_timeout",
            label: "Screen Saver",
            options: ["OFF", "30", "60", "120"],
            value: appSettings["screensaver_timeout"] || "OFF",
            description: "Prevent CRT burn-in after seconds of inactivity or pause",
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

        // INFO SCREEN — a film's details in the trees (Netflix, Prime Video,
        // YouTube): with right on it always, and with a number also on its own
        // once the cursor has rested on it that many seconds. One control, like
        // the screen saver's.
        var infoVals = ["off", "key", "1", "2", "3", "5"]
        var infoOpts = ["Off", "Key", "1 sec", "2 sec", "3 sec", "5 sec"]
        var infoIdx = infoVals.indexOf(appSettings["info_screen"] || "3")
        items.push({
            type: "list_single",
            key: "info_screen",
            label: "Info Screen",
            options: infoOpts,
            values: infoVals,
            value: infoOpts[infoIdx < 0 ? infoVals.indexOf("3") : infoIdx],
            description: "A film's details in Netflix, Prime Video and YouTube\n[KEY] With right on a film  [SEC] Also on its own once the cursor rests on it",
            moduleId: ""
        })

        // MODULES section — only show modules with has_settings
        var hasModuleSettings = false
        for (var i = 0; i < installedModules.length; i++) {
            if (installedModules[i].has_settings) { hasModuleSettings = true; break }
        }

        if (hasModuleSettings) {
            items.push({ type: "section", label: "Modules" })
            for (var j = 0; j < installedModules.length; j++) {
                var m = installedModules[j]
                if (m.has_settings) {
                    items.push({ type: "submenu", label: m.name, moduleId: m.id })
                }
            }
        }

        // SYSTEM section
        items.push({ type: "section", label: "Application" })
        // DISPLAY OUTPUT — where the picture goes, among the outputs this
        // board has (displayOutput, by its model): select opens them. The Pi
        // reads it at power-on, so a change restarts it, powers it off, or
        // waits for its next start, and the new output stays only when kept
        // on it (views/DisplayKeep.qml).
        if (displayOutput && displayOutput.available) {
            boardModel = displayOutput.boardModel
            outputOptions = displayOutput.options
            outputCurrent = displayOutput.current
            outputCurrentLabel = displayOutput.currentLabel
            outputNext = displayOutput.next
            outputNextLabel = displayOutput.nextLabel
            outputAtNextStart = displayOutput.nextStartAvailable
            outputPowerOff = displayOutput.powerOffAvailable
            var later = outputAtNextStart || outputPowerOff
            items.push({
                type: "display_output",
                label: "Display Output",
                value: outputNext !== "" ? "Next: " + outputNextLabel : outputCurrentLabel,
                description: (outputNext !== "" ? "Now " + outputCurrentLabel + ", " + outputNextLabel + " from the next start • " : "")
                             + "Where the picture goes, among the outputs this Pi has: "
                             + (later ? "now, with a restart, or from the next start. " : "a change restarts OSD/OS. ")
                             + "The new output stays only when you keep it on its screen, else the old one comes back in 15 seconds\n[COMPOSITE] The AV jack (Pi 4) or TV pads (Pi 5)  [GPIO COMPOSITE] Pi 5's GPIO pins  [SCART RGB] A cable on the GPIO pins  [JAMMA] A Raspberry Pi to JAMMA board",
                moduleId: ""
            })
        }
        // AUDIO OUTPUT — the sound card the players play through, among those
        // ALSA has now (audioOutput): ◄ ► change it at once, for a video
        // playing behind the menus too. One chosen but unplugged stays
        // chosen, passed over until it is back.
        if (audioOutput && audioOutput.available) {
            var audio = audioOutput.settingRow()
            items.push({
                type: "list_single",
                key: "audio_output",
                label: "Audio Output",
                options: audio.options,
                values: audio.values,
                value: audio.value,
                description: "Which sound card plays: the AV jack, HDMI or a USB sound card, from the moment it is chosen\n[AUTO] The Pi's own default  [UNPLUGGED] Chosen but not plugged in: sound goes as on Auto until it is back",
                moduleId: ""
            })
        }
        items.push({
            type: "submenu",
            key: "remap_controls",
            label: "Controls",
            description: "Every controller connected, each with its own buttons (and a pad's button labels), and extra keys for every keyboard and remote",
            moduleId: ""
        })
        // Pairing a Bluetooth keyboard, gamepad or remote (BlueZ, so Linux).
        if (bluetoothManager && bluetoothManager.supported) {
            items.push({
                type: "submenu",
                key: "bluetooth",
                label: "Bluetooth",
                moduleId: ""
            })
        }
        items.push({
            type: "submenu",
            key: "software_update",
            label: "Update",
            moduleId: ""
        })
        // ABOUT — what OSD/OS is, who makes it, what it is made of and under
        // which licence (views/About.qml).
        items.push({
            type: "submenu",
            key: "about",
            label: "About",
            description: "What OSD/OS is, who makes it, what it is made of and under which license",
            moduleId: ""
        })
        items.push({ type: "quit", label: "Quit" })

        settingsItems = items

        // Restore saved position, or default to first selectable row
        var start = at !== undefined ? Math.min(at, items.length - 1)
                  : navListState.currentIndex !== undefined ? Math.min(navListState.currentIndex, items.length - 1) : 0
        for (var k = 0; k < items.length; k++) {
            if (selectable((start + k) % items.length)) {
                settingsList.currentIndex = (start + k) % items.length
                break
            }
        }
        settingsList.positionViewAtIndex(settingsList.currentIndex, ListView.Contain)
        settingsList.showWholeRows()
    }

    // A row offered only while another row has a value (its shownWith,
    // { key, value }), or any but one ({ key, not }): Window Frame while OSD
    // Background is Window, Logo Image while Channel Logo isn't Off, Music
    // File while Menu Music is File. It is in the model all the time, so the
    // list stays where it is as it comes and goes.
    function rowShown(idx) {
        var row = settingsItems[idx]
        if (!row || !row.shownWith)
            return true
        for (var i = 0; i < settingsItems.length; i++) {
            if (settingsItems[i].key === row.shownWith.key)
                return row.shownWith.not !== undefined ? settingsItems[i].value !== row.shownWith.not
                                                      : settingsItems[i].value === row.shownWith.value
        }
        return false
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
    // is then.
    function partRow(key, label, choices, off, description) {
        var themed = !!appSettings["osd_theme"]
        var options = (themed ? ["Theme"] : []).concat(off ? ["Off"] : [], choices)
        var values = (themed ? [""] : []).concat(off ? ["Off"] : [], choices)
        var at = values.indexOf(appSettings[key] === undefined ? "" : String(appSettings[key]))
        return { type: "list_single", key: key, label: label, options: options, values: values,
                 value: options[Math.max(0, at)], description: description, moduleId: "" }
    }

    // The theme's parts, each set back to THEME as a theme is chosen (to what
    // it is without one, for None), hidden rows too; the rows made again.
    readonly property var themeParts: ["color_scheme", "skin", "text_effect", "background_effect",
                                       "selector_effect", "screen_effect", "transition", "menu_music"]
    function themeChosen() {
        for (var i = 0; i < themeParts.length; i++)
            appCore.save_setting("", themeParts[i], "")
        buildModel(settingsList.currentIndex)
    }

    // The cursor stops on rows, not on headings or rows not offered now.
    function selectable(idx) {
        return settingsItems[idx].type !== "section" && rowShown(idx)
    }

    // The outputs, the cursor on the one in force.
    function openOutputs() {
        outputPrompt.open()
        for (var i = 0; i < outputOptions.length; i++) {
            if (outputOptions[i].id === outputCurrent)
                outputPrompt.choiceIndex = i
        }
    }

    // The next row the cursor stops on, up (-1) or down (1), round from one
    // end of the list to the other.
    function nextSelectable(idx, direction) {
        var n = settingsItems.length
        for (var step = 1; step < n; step++) {
            var i = ((idx + direction * step) % n + n) % n
            if (selectable(i)) return i
        }
        return idx
    }

    Component.onCompleted: buildModel()

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: root.appVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // IP address
    property string ipAddress: ""
    Timer {
        interval: 5000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: settingsRoot.ipAddress = appCore.localIpAddress()
    }
    Text {
        text: settingsRoot.ipAddress
        visible: settingsRoot.ipAddress !== ""
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.sh * 0.125 //60
        anchors.rightMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0291667 //14
        // On the title bar, so in the bar's text colour.
        color: root.surfaceColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        topPadding: root.sh * 0.0125 //6
        rightPadding: root.sw * 0.00625 //4
    }

    ListView {
        id: settingsList
        model: settingsItems
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        // One row short of the space, so the ▼ fits above the help line; a
        // row more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true

        Keys.onUpPressed: {
            currentIndex = settingsRoot.nextSelectable(currentIndex, -1)
            settingsList.positionViewAtIndex(currentIndex, ListView.Contain)
            showWholeRows()
        }
        Keys.onDownPressed: {
            currentIndex = settingsRoot.nextSelectable(currentIndex, 1)
            settingsList.positionViewAtIndex(currentIndex, ListView.Contain)
            showWholeRows()
        }

        // Rows are whole lines, a slider's several, so a scroll can stop with
        // a slider cut by an edge: carry on past it, keeping the current row.
        function showWholeRows() {
            var top = itemAt(0, contentY + 1)
            if (top && top !== currentItem && top.y < contentY - 1)
                contentY = Math.min(top.y + top.height, originY + contentHeight - height)
            var bottom = itemAt(0, contentY + height - 1)
            if (bottom && bottom !== currentItem && bottom.y + bottom.height > contentY + height + 1)
                contentY = Math.max(bottom.y - height, originY)
        }

        // The current row, changed, in place of the old one. The model is
        // not replaced: a new one starts the list over, and it is not put
        // back where it was for sure once the list has been scrolled about
        // (a row's height is not known until it is drawn).
        function replaceCurrentRow(row) {
            settingsItems[currentIndex] = row
            settingsRoot.revision++
        }

        // A slider's step toward one end (-1 left, 1 right), kept and saved.
        // One that is off turns on instead, where its bar waits.
        function moveSlider(direction) {
            var row = settingsItems[currentIndex]
            if (row.on === false) {
                toggleSlider()
                return
            }
            var v = Math.max(0, Math.min(100, row.value + direction * row.step))
            if (v === row.value)
                return
            replaceCurrentRow(Object.assign({}, row, { value: v }))
            appCore.save_setting(row.moduleId, row.key, v)
        }

        // A slider that can be off (one with `on`), turned off, saved as its
        // offValue, or back on at its value.
        function toggleSlider() {
            var row = settingsItems[currentIndex]
            var on = !row.on
            replaceCurrentRow(Object.assign({}, row, { on: on }))
            appCore.save_setting(row.moduleId, row.key, on ? row.value : row.offValue)
        }

        Keys.onLeftPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "slider") {
                moveSlider(-1)
            } else if (row && row.type === "list_single") {
                var opts = row.options
                var idx = opts.indexOf(row.value)
                var newIdx = (idx - 1 + opts.length) % opts.length
                var newVal = opts[newIdx]
                // Display the label; persist the parallel value when one exists.
                var savedVal = row.values ? row.values[newIdx] : newVal
                replaceCurrentRow(Object.assign({}, row, { value: newVal }))
                appCore.save_setting(row.moduleId, row.key, savedVal)
                if (row.key === "osd_theme")
                    settingsRoot.themeChosen()
            }
        }

        Keys.onRightPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "slider") {
                moveSlider(1)
            } else if (row && row.type === "list_single") {
                var opts = row.options
                var idx = opts.indexOf(row.value)
                var newIdx = (idx + 1) % opts.length
                var newVal = opts[newIdx]
                // Display the label; persist the parallel value when one exists.
                var savedVal = row.values ? row.values[newIdx] : newVal
                replaceCurrentRow(Object.assign({}, row, { value: newVal }))
                appCore.save_setting(row.moduleId, row.key, savedVal)
                if (row.key === "osd_theme")
                    settingsRoot.themeChosen()
            }
        }

        Keys.onReturnPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "submenu") {
                if (row.key === "software_update")
                    settingsRoot.navigateTo("views/Update.qml", {}, { currentIndex: settingsList.currentIndex })
                else if (row.key === "remap_controls")
                    settingsRoot.navigateTo("views/Controllers.qml", {}, { currentIndex: settingsList.currentIndex })
                else if (row.key === "bluetooth")
                    settingsRoot.navigateTo("views/Bluetooth.qml", {}, { currentIndex: settingsList.currentIndex })
                else if (row.key === "about")
                    settingsRoot.navigateTo("views/About.qml", {}, { currentIndex: settingsList.currentIndex })
                else
                    settingsRoot.navigateTo("views/ModuleSettings.qml", { moduleId: row.moduleId }, { currentIndex: settingsList.currentIndex })
            } else if (row && row.type === "display_output") {
                settingsRoot.openOutputs()
            } else if (row && row.type === "file") {
                // Picked on the file picker, which saves it and comes back.
                settingsRoot.navigateTo("views/FilePicker.qml",
                                        Object.assign({ moduleId: row.moduleId, settingKey: row.key,
                                                        currentPath: row.path, label: row.label }, row.picker),
                                        { currentIndex: settingsList.currentIndex })
            } else if (row && row.type === "quit") {
                quitPrompt.open()
            } else if (row && row.type === "slider" && row.on !== undefined) {
                toggleSlider()
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                settingsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            id: rowItem
            // The row as it is now, read again as one changes.
            readonly property var row: (settingsRoot.revision, settingsRoot.settingsItems[index])
            // Not offered now (shownWith): no line at all.
            readonly property bool shown: (settingsRoot.revision, settingsRoot.rowShown(index))
            readonly property bool slider: row.type === "slider"
            readonly property real lineHeight: root.sh * 0.0583333 //28
            // A slider's bar takes whole lines under its own, so every line
            // keeps to the list's rule as it scrolls.
            readonly property int barLines: slider ? Math.ceil((tape.height + 2 * root.px) / lineHeight) : 0
            width: settingsList.width
            height: shown ? lineHeight * (1 + barLines) : 0
            visible: shown

            // A line laid out like a camcorder's menu, "DISPLAY······ON", or a
            // section's heading, as large as the lines under it: "MODULES ─────".
            MenuRow {
                width: parent.width
                height: rowItem.lineHeight
                heading: rowItem.row.type === "section"
                label: rowItem.row.label || ""
                value: rowItem.row.type === "list_single" || rowItem.row.type === "display_output"
                       || rowItem.row.type === "file"
                       ? (rowItem.row.value || "")
                     : rowItem.slider && rowItem.row.on !== undefined ? (rowItem.row.on ? "On" : "Off") : ""
                selected: settingsList.currentIndex === index
            }

            // A slider's setting, as the deck's tape bar shows how far the
            // tape is: ◄ ► move the ▼ between the two ends. Off, its lines
            // stay, empty.
            Loader {
                id: tape
                active: rowItem.slider
                visible: rowItem.row.on !== false
                x: root.sw * 0.009375 //6
                y: rowItem.lineHeight + Math.round((rowItem.barLines * rowItem.lineHeight - height) / 2)
                width: parent.width - 2 * x
                sourceComponent: OsdTapeBar {
                    fontSize: root.sh * 0.0333333 //16
                    value: rowItem.row.value / 100
                    startText: rowItem.row.startText
                    endText: rowItem.row.endText
                }
            }
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: settingsList
        list: settingsList
    }

    // --- HELP TEXT --- (shown when a focused row has a description), on one
    // line that scrolls when it is too long for the box.
    HelpLine {
        property var currentRow: settingsRoot.settingsItems[settingsList.currentIndex]
        visible: !!(currentRow && currentRow.description)
        text: (currentRow && currentRow.description) || ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":CHANGE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- DISPLAY OUTPUT: the outputs this board has, then how to switch ---
    ChoiceOverlay {
        id: outputPrompt
        anchors.fill: parent
        promptText: "Display Output"
        subtitleText: settingsRoot.boardModel + "\nNow: " + settingsRoot.outputCurrentLabel
                      + (settingsRoot.outputNext !== "" ? "  Next start: " + settingsRoot.outputNextLabel : "")
        choices: settingsRoot.outputOptions.map(function(o) { return { label: o.label, action: o.id } })
        onActivated: function(id) {
            for (var i = 0; i < settingsRoot.outputOptions.length; i++) {
                if (settingsRoot.outputOptions[i].id === id)
                    settingsRoot.outputChoice = settingsRoot.outputOptions[i]
            }
            // The one in force: nothing to do, or what waits for the next
            // start taken back.
            if (id === settingsRoot.outputCurrent) {
                if (settingsRoot.outputNext !== "")
                    outputStay.open()
                return
            }
            outputConfirm.open()
        }
        onClosed: settingsList.forceActiveFocus()
    }

    // Now, with a restart; written now for the next start, the Pi left on;
    // or written, then off: on the new output the next time it is switched
    // on, as when it goes into a cabinet whose screen this one isn't.
    ChoiceOverlay {
        id: outputConfirm
        anchors.fill: parent
        promptText: "Switch to " + (settingsRoot.outputChoice.label || "") + "?"
        subtitleText: (settingsRoot.outputAtNextStart || settingsRoot.outputPowerOff
                       ? "Now, or from the next time the Pi starts. " : "OSD/OS restarts on it. ")
                      + "Keep it there, or in 15 seconds it comes back to " + settingsRoot.outputCurrentLabel
        choices: [{ label: "Switch and Restart", action: "switch" }]
                 .concat(settingsRoot.outputAtNextStart && settingsRoot.outputChoice.id !== settingsRoot.outputNext
                         ? [{ label: "At Next Start", action: "next" }] : [])
                 .concat(settingsRoot.outputPowerOff ? [{ label: "Switch and Power Off", action: "poweroff" }] : [])
                 .concat([{ label: "Cancel", action: "cancel" }])
        onActivated: function(act) {
            if (act === "switch")
                displayOutput.apply(settingsRoot.outputChoice.id)
            else if (act === "next")
                displayOutput.applyAtNextStart(settingsRoot.outputChoice.id)
            else if (act === "poweroff")
                displayOutput.applyAndPowerOff(settingsRoot.outputChoice.id)
        }
        onClosed: settingsList.forceActiveFocus()
    }

    ChoiceOverlay {
        id: outputStay
        anchors.fill: parent
        promptText: "Stay on " + settingsRoot.outputCurrentLabel + "?"
        subtitleText: settingsRoot.outputNextLabel + " was to come at the next start"
        choices: [{ label: "Stay on " + settingsRoot.outputCurrentLabel, action: "stay" },
                  { label: "Cancel", action: "cancel" }]
        onActivated: function(act) {
            if (act === "stay")
                displayOutput.applyAtNextStart(settingsRoot.outputCurrent)
        }
        onClosed: settingsList.forceActiveFocus()
    }

    // At Next Start's answer: the row as it is now, or why it didn't happen.
    Connections {
        target: displayOutput
        function onChanged() {
            if (displayOutput.available)
                settingsRoot.buildModel(settingsList.currentIndex)
        }
        function onNextStartFinished(ok, reason) {
            if (ok)
                return
            settingsRoot.outputFailure = reason
            outputFailed.open()
        }
    }

    ChoiceOverlay {
        id: outputFailed
        anchors.fill: parent
        promptKind: "notice"
        promptText: "Not written for the next start"
        subtitleText: (settingsRoot.outputChoice.label || "") + " couldn't be written: " + settingsRoot.outputFailure
        choices: []
        onClosed: settingsList.forceActiveFocus()
    }

    // --- QUIT CONFIRMATION OVERLAY ---
    ChoiceOverlay {
        id: quitPrompt
        anchors.fill: parent
        promptText: "Really quit?"
        choices: quitOptions
        onActivated: function(act) {
            if (act === "quit")          Qt.quit()
            else if (act === "restart")  Qt.exit(12)   // osdos-stop reboots on it
            else if (act === "terminal") Qt.exit(10)   // matches EXIT_STATUS check in osdos-stop
        }
        onClosed: settingsList.forceActiveFocus()
    }
}
