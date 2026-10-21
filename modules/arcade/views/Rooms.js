.pragma library

// What the RetroArch Lobby page (Lobby.qml) and the Arcade screen's RetroArch
// Lobby filter say of a room of RetroArch's whole lobby: a room as
// ArcadeBackend::lobby() has it, with lobby its read (yourCore,
// yourRetroArch and their states).

// How long ago a room was hosted: "5M Ago", "1H 20M Ago".
function ago(minutes) {
    return minutes >= 60 ? Math.floor(minutes / 60) + "H " + (minutes % 60) + "M Ago" : minutes + "M Ago"
}
function yesNo(on) { return on ? "Yes" : "No" }
// A room's version against yours (the backend's coreMatch, retroarchMatch):
// ok for the same, no for another, nothing where either isn't known.
function matchIcons(match) { return match === "same" ? ["ok"] : match === "other" ? ["no"] : [] }
// The same, after a room's version on a help line.
function yours(match, version) {
    return match === "same" ? " (yours too)" : match === "other" ? " (yours " + version + ")" : ""
}
// Why yours isn't known (the backend's yourCoreState, yourRetroArchState):
// program not installed, still being read, or it couldn't be.
function notKnown(state, program) {
    return state === "missing" ? program + " is not installed here"
         : state === "reading" ? "yours is still being read" : "yours couldn't be read"
}

// What the help line says of a room: its core and version, its ROM, who
// hosts it, its RetroArch, how it is reached, and whether it can be joined.
function about(r, lobby) {
    var parts = [r.core + (r.coreVersion ? " " + r.coreVersion : "") + yours(r.coreMatch, lobby.yourCore)]
    parts.push("ROM " + r.rom + (r.crc ? ", CRC " + r.crc : ""))
    parts.push("Hosted by " + r.host + (r.country ? " in " + r.country.toUpperCase() : ""))
    if (r.retroarch)
        parts.push("RetroArch " + r.retroarch + yours(r.retroarchMatch, lobby.yourRetroArch)
                   + (r.frontend ? " on " + r.frontend : ""))
    parts.push(r.relay !== "" ? "Through the relay " + r.relay : "Straight to the host")
    parts.push(r.joinable ? "Select to join it" : r.why)
    return parts.join("\n")
}

// A room's lines: what the lobby says of it, each with what the help line
// says of it; Join first for one that can be joined.
function linesOf(r, lobby) {
    var made = []
    var fbneo = r.core === "FinalBurn Neo"
    var coreHere = lobby.yourCoreState !== "missing"
    if (r.joinable)
        made.push({ type: "join", label: "Join",
                    description: "Play " + r.title + " with " + r.host + ", in FinalBurn Neo"
                                 + (r.coreMatch === "other" ? "\nYour core's version isn't theirs: the game may fall out of step" : "") })
    made.push({ label: "Game", value: r.title, description: r.title })
    if (r.rom !== "" && r.rom !== r.title)
        made.push({ label: "ROM", value: r.rom, description: "The game's file, as its host has it: " + r.rom })
    made.push({ label: "CRC", value: r.crc || "?",
                description: r.crc ? "The ROM's CRC: a copy of the same ROM has the same one" : "Its host's RetroArch doesn't say" })
    made.push({ label: "Core", value: r.core, icons: fbneo && coreHere ? ["ok"] : ["no"],
                description: !fbneo ? "The emulator it is played in: OSD/OS plays FinalBurn Neo's games alone"
                             : coreHere ? "The emulator it is played in: yours too"
                             : "The emulator it is played in: FinalBurn Neo, not installed here" })
    // Its version against yours; a room kept as it left the lobby keeps the
    // match it had, just its version once yours has been read since.
    if (r.coreVersion)
        made.push({ label: "Core Version", value: r.coreVersion, icons: matchIcons(r.coreMatch),
                    description: r.coreMatch === "same" ? "The same as yours: " + lobby.yourCore
                                 : r.coreMatch === "other" ? "Yours is " + lobby.yourCore
                                                             + ": another version, the game may fall out of step"
                                 : !fbneo || lobby.yourCoreState === "read" ? r.core + " " + r.coreVersion
                                 : "Its host's version: " + notKnown(lobby.yourCoreState, "FinalBurn Neo") })
    if (r.subsystem)
        made.push({ label: "Subsystem", value: r.subsystem, description: "The core's subsystem it is played in" })
    made.push({ label: "RetroArch", value: r.retroarch || "?", icons: matchIcons(r.retroarchMatch),
                description: r.retroarchMatch === "same" ? "The same as yours: RetroArch " + lobby.yourRetroArch
                             : r.retroarchMatch === "other" ? "Yours is " + lobby.yourRetroArch + ": another version"
                             : lobby.yourRetroArchState !== "read"
                               ? "The RetroArch its host plays it in: " + notKnown(lobby.yourRetroArchState, "RetroArch")
                             : "The RetroArch its host plays it in" })
    if (r.frontend)
        made.push({ label: "System", value: r.frontend, description: "What its host plays it on" })
    made.push({ label: "Host", value: r.host, description: "Who hosts it" })
    made.push({ label: "Country", value: r.country ? r.country.toUpperCase() : "?", description: "Where its host is" })
    made.push({ label: "Connection", value: r.relay !== "" ? "Relay" : "Direct",
                description: r.relay !== "" ? "Through the relay " + r.relay + ", for a host whose router turns players away"
                                            : "Straight to the host" })
    made.push({ label: "Password", value: yesNo(r.password),
                description: r.password ? "It asks for a password to play" : "Anyone can play" })
    if (r.spectatePassword)
        made.push({ label: "Watch Password", value: "Yes", description: "It asks for a password to watch" })
    if (r.players >= 0)
        made.push({ label: "Players", value: String(r.players), description: "Playing in it now" })
    if (r.spectators >= 0)
        made.push({ label: "Spectators", value: String(r.spectators), description: "Watching it now" })
    if (r.minutes >= 0)
        made.push({ label: "Hosted", value: ago(r.minutes), description: "How long ago it was hosted" })
    made.push({ label: "Can Join", value: yesNo(r.joinable),
                description: r.joinable ? "From here, in FinalBurn Neo" : r.why })
    return made
}

// The lines the Arcade screen's box has room for, in this order: the core,
// its version and RetroArch's against yours, who hosts it, where, how it is
// reached, how many play and how long ago it was hosted. The help line has
// the rest.
// What marks a room in a list: ▶ one to join, a lock one with a password,
// nothing any other.
function mark(r) { return r.joinable ? "play" : r.password ? "lock" : "" }

// What the lobby says while it has no room to list: being read, not
// reached, or no one hosting.
function emptyLine(lobby) {
    return lobby.state === "failed" ? "Lobby Not Reachable"
         : lobby.state === "ready" ? "No one is hosting a game" : "Reading RetroArch's lobby…"
}

var kBoxLines = ["Core", "Core Version", "RetroArch", "Host", "Country", "Connection", "Players", "Hosted"]
function boxLines(r, lobby) {
    return linesOf(r, lobby).filter(function(line) { return kBoxLines.indexOf(line.label) >= 0 })
        .map(function(line) {
            return line.label === "Core Version" ? Object.assign({}, line, { label: "Version" }) : line
        })
}
