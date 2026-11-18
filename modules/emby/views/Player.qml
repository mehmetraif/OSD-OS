import QtQuick
import Components

// Emby's player: see MediaServerPlayer.
MediaServerPlayer {
    backend: embyBackend
    // Emby sends a transcode's text subtitles as files beside the stream
    // (EmbyBackend::subtitleProfiles()): burnt in, they fail on its server.
    sidecarSubsInTranscode: true
}
