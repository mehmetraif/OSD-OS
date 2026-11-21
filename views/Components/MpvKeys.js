.pragma library

// The keys a player's view sends on to mpv as its video plays, by Qt key:
// mpv's name for each, as mpvController.sendKey() takes it. One table for
// every player, so that each sends the same keys under the same names; the
// keys come as they always do, a pad's buttons among them (InputManager's
// key events).
//
//     var key = MpvKeys.name(event.key)
//     if (key !== "") {
//         mpvController.sendKey(key, event.isAutoRepeat)
//         event.accepted = true
//     }
//
// Back (Esc, a remote's Back) goes as ESC and Backspace as BS, which
// MpvController acts on itself. The channel keys, a TV remote's or the page
// keys, step a playlist in mpv (mpv-media-keys.lua): a player that plays one
// asks for them (channels), and sends them once per press (PlayerSession's
// forwardKey() does both, for every player).

var keys = {}
keys[Qt.Key_Escape] = "ESC"
keys[Qt.Key_Back] = "ESC"
keys[Qt.Key_Backspace] = "BS"
keys[Qt.Key_Up] = "UP"
keys[Qt.Key_Down] = "DOWN"
keys[Qt.Key_Left] = "LEFT"
keys[Qt.Key_Right] = "RIGHT"
keys[Qt.Key_Space] = "SPACE"
keys[Qt.Key_Return] = "ENTER"
keys[Qt.Key_Enter] = "ENTER"

var channelKeys = {}
channelKeys[Qt.Key_PageUp] = "CHANNEL_UP"
channelKeys[Qt.Key_ChannelUp] = "CHANNEL_UP"
channelKeys[Qt.Key_PageDown] = "CHANNEL_DOWN"
channelKeys[Qt.Key_ChannelDown] = "CHANNEL_DOWN"

// mpv's name for a Qt key, the channel keys' with channels, or "" for a key
// the player keeps (or lets go on to Main.qml).
function name(key, channels) {
    if (keys[key] !== undefined)
        return keys[key]
    if (channels && channelKeys[key] !== undefined)
        return channelKeys[key]
    return ""
}

// One of the channel keys.
function isChannel(name) {
    return name === "CHANNEL_UP" || name === "CHANNEL_DOWN"
}
