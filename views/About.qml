import QtQuick
import Components

// ABOUT, from Settings: the wordmark with its tagline, then System
// Information, this device's name its value, which opens its page
// (views/SystemInfo.qml; back from it, the cursor on its line again), then
// what OSD/OS is made of, who makes it and under which licence, as menu lines
// whose help line carries each one's detail (it stays whatever Settings' HELP
// LINE says: these lines are the page); then, under ARCADE, the free software
// the games play in and read with, and the sites they are read from. Behind
// the LICENSE line is the licence itself: the notice the GNU GPL asks an
// interactive program to show (whose copyright, no warranty, the freedom to
// pass it on, where the licence is), then the licence's text, the LICENSE
// file every build carries next to the app (appCore.licenseText()), a page at
// a time; behind an arcade line, its licence's (assets/licenses, or FinalBurn
// Neo's as it was installed).
FocusScope {
    id: aboutRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    // Who makes OSD/OS, and whose work it carries on.
    readonly property string developer: "mehmet raif tasdemir (darkBLACK)"
    readonly property string developerUrl: "github.com/mehmetraif"
    readonly property string websiteUrl: "osdos.tv"
    readonly property string sourceUrl: "github.com/mehmetraif/OSD-OS"
    readonly property string upstreamAuthor: "Anthony Caccese"
    readonly property string upstreamUrl: "github.com/anthonycaccese/240-MP"
    readonly property string year: "2026"
    // Context properties read null while the Loader tears this view down.
    readonly property string deviceName: systemInfo ? systemInfo.deviceName : ""

    // The lines, which nothing changes while the page is up: a new list
    // would take the cursor back to the first line, System Information, which
    // select opens. A description's {select} is the select key's hint, put in
    // as the help line shows it (keyboard or gamepad).
    readonly property var rows: [
        { key: "system_info", label: "System Information", page: "views/SystemInfo.qml",
          description: "This device's name, which tells it apart from others on the network • {select}"
                       + " What it is: its model, OS, memory, storage and temperature, and each network connection's"
                       + " IP and MAC addresses" },
        { key: "build", label: "Build", value: root.appBuild,
          description: "Version " + root.appVersion + " • The commit this build was made from, and the day"
                       + " • A VCR-style smart TV experience for CRTs: films, streams and arcade games with high"
                       + " scores, RetroAchievements and netplay • Built for TVs and JAMMA cabinets, running on"
                       + " Raspberry Pi, macOS and Linux" },
        { key: "developer", label: "Developer", value: aboutRoot.developer,
          description: "OSD/OS is developed by " + aboutRoot.developer + " • " + aboutRoot.developerUrl },
        { key: "upstream", label: "Based On", value: "240-MP",
          description: "240-MP by " + aboutRoot.upstreamAuthor + " and its contributors, " + aboutRoot.upstreamUrl
                       + " • OSD/OS is a modified version of it, from " + aboutRoot.year + ", under the same license" },
        { key: "license", label: "License", value: "GNU GPL v3", license: "LICENSE",
          description: "Free software under the GNU General Public License, version 3: use it, share it and change it, under the same license"
                       + " • It comes with no warranty • {select} The notice and the license's text" },
        { key: "website", label: "Website", value: aboutRoot.websiteUrl,
          description: "OSD/OS's website: what it is and how it is set up, the latest release to download, and"
                       + " OSD/OS Setup, " + aboutRoot.websiteUrl + "/setup, which writes a card's first settings"
                       + " before it goes into the Pi" },
        { key: "source", label: "Source", value: aboutRoot.sourceUrl,
          description: "The source code, the releases and the issues • Changes are welcome: see CONTRIBUTING.md there" },
        { key: "code", label: "Written With", value: "Claude Code",
          description: "OSD/OS's changes to 240-MP were written with Claude Code, Anthropic's coding agent,"
                       + " as a large part of 240-MP's own code was" },
        { key: "artwork", label: "Artwork", value: "ChatGPT",
          description: "The OSD/OS logo, the channel logo over the picture and the boot screen's cassette"
                       + " were made with ChatGPT, OpenAI's assistant" },
        { key: "fonts", label: "Fonts", value: "VCR OSD Mono, Unifont",
          description: "VCR OSD Mono by Riciery Santos Leal (mrmanet), free from dafont.com"
                       + " • GNU Unifont by Roman Czyborra, Paul Hardy and others, under the SIL Open Font License 1.1" },
        { key: "built", label: "Built With", value: "Qt, SDL2, mpv",
          description: "Qt 6 (LGPL v3) • SDL2 (zlib) • mpv and libmpv (GPL v2+)"
                       + " • On the OSD/OS image: yt-dlp (Unlicense), Deno (MIT), FFmpeg (LGPL/GPL), Chromium (BSD)"
                       + " • The arcade's under ARCADE, below" },
        { key: "system", label: "Image OS", value: "Raspberry Pi OS",
          description: "The OSD/OS image is Raspberry Pi OS Lite (64-bit), which is based on Debian 13 (trixie),"
                       + " built with Raspberry Pi's pi-gen, and OSD/OS on top • Each package keeps its own license:"
                       + " /usr/share/doc on the image, and /usr/share/doc/osdos/NOTICE for the rest"
                       + " • Raspberry Pi is a trademark of Raspberry Pi Ltd, and Debian a registered trademark of"
                       + " Software in the Public Interest, Inc.; OSD/OS is not affiliated with or endorsed by either" },
        { key: "data", label: "Data", value: "TMDB, Open-Meteo",
          description: "This product uses the TMDB API but is not endorsed or certified by TMDB: the Netflix and"
                       + " Prime Video catalogues, with JustWatch's listings through it • Weather data by"
                       + " Open-Meteo.com, under CC BY 4.0 • Titles' pages on the services from Wikidata" },

        // The arcade's (Settings → Arcade, the High Scores module): the free
        // software its games play in and read with, each licence a page of
        // its own (`license`: a file of the app's, "fbneo" FinalBurn Neo's),
        // and the sites it reads from.
        { type: "section", label: "Arcade" },
        { key: "mame", label: "MAME", value: "GPL v2+", license: "assets/licenses/GPL-2.0.txt",
          licenseName: "GNU General Public License, version 2 or later",
          description: "The arcade machine emulator the games play in, by the MAME team, mamedev.org"
                       + " • Free software under the GNU General Public License, version 2 or later"
                       + " • Its hiscore plugin, which keeps the games' high score tables, by borgar (CC0), and its"
                       + " hiscore.dat by Leezer • {select} The license's text" },
        { key: "retroarch", label: "RetroArch", value: "GPL v3", license: "LICENSE",
          licenseName: "GNU General Public License, version 3",
          description: "Plays the games FinalBurn Neo has, with RetroAchievements and online play, by libretro and its"
                       + " contributors, retroarch.com • Free software under the GNU General Public License, version 3,"
                       + " its rcheevos under the MIT License • {select} The license's text" },
        { key: "fbneo", label: "FinalBurn Neo", value: "Non-commercial", license: "fbneo",
          licenseName: "FinalBurn Neo's license",
          description: "The arcade emulator RetroArch plays those games with, as a libretro core, by Team FB Neo,"
                       + " github.com/libretro/FBNeo, from libretro's build server, buildbot.libretro.com"
                       + " • Free to use and share, never to sell; no ROMs come with it • {select}"
                       + " The license's text" },
        { key: "openhi2txt", label: "openhi2txt", value: "GPL v3", license: "LICENSE",
          licenseName: "GNU General Public License, version 3",
          description: "Reads MAME's high score files into the High Scores tables, by inigomonyota,"
                       + " github.com/inigomonyota/openhi2txt • Free software under the GNU General Public License,"
                       + " version 3 • {select} The license's text" },
        { key: "hi2txt", label: "hi2txt", value: "GPL v2", license: "assets/licenses/GPL-2.0.txt",
          licenseName: "GNU General Public License, version 2",
          description: "How each game keeps its high score table: hi2txt's game definitions by GreatStone and their"
                       + " contributors, many from HiToText, greatstone.free.fr/hi2txt • Under the GNU General Public"
                       + " License, version 2 • {select} The license's text" },
        { key: "rapidxml", label: "RapidXML", value: "Boost or MIT", license: "assets/licenses/RapidXML.txt",
          licenseName: "Boost Software License 1.0, or the MIT License",
          description: "The XML reader openhi2txt is built with, by Marcin Kalicinski, rapidxml.sourceforge.net"
                       + " • Under the Boost Software License 1.0 or the MIT License, either • {select}"
                       + " The licenses' text" },
        { key: "zlib", label: "minizip, zlib", value: "zlib", license: "assets/licenses/zlib.txt",
          licenseName: "zlib License",
          description: "Open hi2txt's definitions and FinalBurn Neo's download: minizip by Gilles Vollant and zlib by"
                       + " Jean-loup Gailly and Mark Adler, zlib.net • Under the zlib License • {select}"
                       + " The license's text" },
        { key: "lzma", label: "LZMA SDK", value: "Public domain", license: "assets/licenses/LZMA-SDK.txt",
          licenseName: "The LZMA SDK's notice",
          description: "Unpacks the MAME and RetroArch for Windows downloaded from their releases, 7z archives: the"
                       + " LZMA SDK's decoder by Igor Pavlov, 7-zip.org/sdk.html • In the public domain • {select}"
                       + " The notice's text" },
        { key: "achievements", label: "RetroAchievements", value: "retroachievements.org",
          description: "Leaderboards and hardcore play for the games FinalBurn Neo plays, with your own account,"
                       + " read through its Web API, and the games' pictures and the achievements' badges from its"
                       + " media server • OSD/OS is not affiliated with RetroAchievements" },
        { key: "lobby", label: "Online Play", value: "lobby.libretro.com",
          description: "The online games to join, from libretro's lobby, which also lists the games you host, and"
                       + " libretro's relay servers, which carry a hosted game through when Relay Server is set" },
        { key: "snaps", label: "progetto-SNAPS", value: "progettosnaps.net",
          description: "The games' pictures, the Snapshots and Titles packs, and their genres, catver.ini, by AntoPISA,"
                       + " progettosnaps.net: put in the games folder, or downloaded, the pictures game by game through"
                       + " ArcadeDB and catver.ini from AntoPISA's MAME_SupportFiles on GitHub • Screenshots are free"
                       + " to use under fair use; their copyright is held by the companies that developed and"
                       + " published the games" },
        { key: "arcadedb", label: "ArcadeDB", value: "adb.arcadeitalia.net",
          description: "Arcade Database by motoschifo, adb.arcadeitalia.net, whose scraper service gives progetto-SNAPS'"
                       + " pictures of a set on request, as Pictures From allows, one request at a time • Names and"
                       + " images for informational purposes only, under fair use; all copyrights and trademarks"
                       + " belong to their holders" }
    ]

    // The notice at the head of the licence page: the GPL's own, for this
    // program.
    readonly property string notice:
        "OSD/OS " + root.appVersion + "\n"
        + "Copyright (C) " + aboutRoot.year + " " + aboutRoot.developer + "\n"
        + "A modified version of 240-MP, Copyright (C) " + aboutRoot.year + " " + aboutRoot.upstreamAuthor
        + " and the 240-MP contributors (" + aboutRoot.upstreamUrl + ").\n\n"
        + "This program is free software: you can redistribute it and/or modify it under the terms of the"
        + " GNU General Public License as published by the Free Software Foundation, version 3.\n\n"
        + "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;"
        + " without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."
        + " See the GNU General Public License below for more details, or at gnu.org/licenses/gpl-3.0.html."

    // The device's name read again as the page opens, as System Information
    // reads it, in case it was changed outside OSD/OS; back from that page, the
    // cursor on its line again.
    Component.onCompleted: {
        if (systemInfo)
            systemInfo.refresh()
        if (navListState.currentIndex !== undefined) {
            list.currentIndex = Math.min(navListState.currentIndex, rows.length - 1)
            list.show(list.currentIndex)
        }
    }

    AppBar {
        iconSource: "../../assets/images/logo.svg"
        title: "About"
        subtitle: licensePage.visible ? licensePage.title : root.appVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The wordmark, as on the logo: the letters in the scheme's colour, the
    // slash in its own three, and SMART TV FOR CRT under a rule, in the
    // deck's own letters so a CRT reads it.
    Item {
        id: mark
        visible: !licensePage.visible
        x: root.sw * 0.125 //80
        y: root.sh * 0.2083333 //100
        width: root.sw * 0.75 //480
        height: tagline.y + tagline.height

        Image {
            id: letters
            anchors.horizontalCenter: parent.horizontalCenter
            height: root.sh * 0.075 //36
            width: implicitWidth
            sourceSize.height: height
            source: "image://osdicon/" + root.primaryColor.toString().replace("#", "")
                    + "/" + Qt.resolvedUrl("../assets/images/logo-wordmark.svg")
        }
        // The slash over the letters' own, in its colours: the same frame.
        Image {
            anchors.fill: letters
            sourceSize.width: letters.width
            sourceSize.height: letters.height
            source: "../assets/images/logo-slash.svg"
        }
        Rectangle {
            id: rule
            anchors.horizontalCenter: parent.horizontalCenter
            y: letters.height + root.px * 4
            width: Math.round(letters.width * 1.15)
            height: root.px
            color: root.primaryColor
            antialiasing: false
        }
        Text {
            id: tagline
            anchors.horizontalCenter: parent.horizontalCenter
            y: rule.y + rule.height + root.px * 3
            text: "SMART TV FOR CRT"
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667 //14
            font.letterSpacing: root.px * 2
        }
    }

    MenuList {
        id: list
        model: aboutRoot.rows
        visible: !licensePage.visible
        focus: !licensePage.visible
        // Under the wordmark: six lines, the rest on ▼, and room above them
        // for the ▲ clear of the tagline; a seventh with the hint bar off (its
        // help line stays, Help Line or not: these lines are the page).
        anchors.topMargin: root.sh * 0.375 //180
        height: root.sh * 0.35 + root.hintRoom //168

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label
            // The device's name read here, so that a new one doesn't make the
            // lines anew.
            value: modelData.key === "system_info" ? aboutRoot.deviceName : (modelData.value || "")
            // A line that leads to a page, or a licence's text, ends in a ►.
            opens: !!(modelData.page || modelData.license)
            selected: list.currentIndex === index
            // Read rather than chosen: a value too long for its line (the
            // developer, the source) scrolls through, selected or not.
            alwaysScroll: true
        }

        // The page a line leads to, or the licence it holds; select or ►.
        function openRow(row) {
            if (row && row.page)
                aboutRoot.navigateTo(row.page, {}, { currentIndex: currentIndex })
            else if (row && row.license)
                licensePage.open(row)
        }
        Keys.onReturnPressed: openRow(aboutRoot.rows[currentIndex])
        Keys.onRightPressed: openRow(aboutRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                aboutRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        readonly property var row: aboutRoot.rows[list.currentIndex]
        always: true
        visible: !licensePage.visible
        text: row && row.description ? row.description.split("{select}").join(root.hints.select) : ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property var row: aboutRoot.rows[list.currentIndex]
        visible: !licensePage.visible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE"
              + (row && row.page ? " " + root.hints.select + ":OPEN"
                 : row && row.license ? " " + root.hints.select + ":LICENSE" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A licence, in place of the lines below the title bar: OSD/OS's with the
    // notice, another's under its name, then the text, a page at a time with
    // ▲ ▼.
    FocusScope {
        id: licensePage
        anchors.fill: parent
        visible: false

        property string title: "License"
        property string notice: ""
        property string text: ""

        function open(row) {
            title = row.key === "license" ? "License" : row.label + " License"
            notice = row.key === "license" ? aboutRoot.notice : row.label + "\n" + row.licenseName
            var t = row.license === "fbneo"
                    ? (typeof arcadeBackend !== "undefined" && arcadeBackend ? arcadeBackend.fbneoLicenseText() : "")
                    : (appCore ? appCore.licenseText(row.license === "LICENSE" ? "" : row.license) : "")
            text = t !== "" ? t
                 : row.license === "fbneo"
                   ? "FinalBurn Neo isn't installed: Settings → Arcade → FinalBurn Neo downloads it, with its license."
                 : row.license === "LICENSE"
                   ? "The license's text is missing from this build. It is at gnu.org/licenses/gpl-3.0.html."
                   : "The license's text is missing from this build."
            flick.contentY = 0
            visible = true
            forceActiveFocus()
        }
        function close() {
            visible = false
            list.forceActiveFocus()
        }

        Keys.onUpPressed: flick.scrollBy(-1)
        Keys.onDownPressed: flick.scrollBy(1)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                licensePage.close()
                event.accepted = true
            }
        }

        // Everything under the title bar, on the ground every view has.
        OsdGround {
            y: root.sh * 0.1916667 //92
            width: parent.width
            height: parent.height - y
        }

        Rectangle {
            id: page
            x: root.sw * 0.125 //80
            y: root.sh * 0.25 //120
            width: root.sw * 0.75 //480
            // Down to just above the hint bar, the help line's room included;
            // with the hint bar off, down to where its ▼ ends at the content
            // box's foot.
            height: root.sh * 0.575 + (root.hintBar ? 0 : root.sh * 0.0416667) //276, 296
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false

            Flickable {
                id: flick
                anchors.fill: parent
                anchors.margins: root.sw * 0.009375 //6
                contentHeight: column.height
                clip: true

                // A page at a time, with a little of the last one kept.
                function scrollBy(direction) {
                    var step = height * 0.8
                    var maxY = Math.max(0, contentHeight - height)
                    contentY = Math.max(0, Math.min(maxY, contentY + direction * step))
                }

                Column {
                    id: column
                    width: flick.width
                    spacing: root.sh * 0.0291667 //14

                    Text {
                        width: parent.width
                        text: licensePage.notice
                        textFormat: Text.PlainText
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.pixelSize: root.sh * 0.0333333 //16
                    }
                    Text {
                        width: parent.width
                        text: licensePage.text
                        textFormat: Text.PlainText
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.pixelSize: root.sh * 0.0291667 //14
                    }
                }
            }
        }

        ScrollMarks {
            anchors.fill: page
            list: flick
        }

        HintBar {
            text: root.hints.back + ":BACK " + root.hints.navigate + ":SCROLL"
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.bottomMargin: root.sh * 0.1041667 //50
            anchors.leftMargin: root.sw * 0.125 //80
        }
    }
}
