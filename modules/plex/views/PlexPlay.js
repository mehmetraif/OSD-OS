.pragma library

// What Plex's Player is opened with for an item's stream, for every view
// that launches one (Item, Extras, CardPlay, QueuePlay): the item's detail
// `d`, the stream streamUrlReady brought, and the session it was asked for
// with (plexBackend.start_stream()). The tracks are the server's selections
// unless the view's own are given in `more`, which adds to or replaces any
// of these.
function playerParams(d, url, plexToken, sessionId, more) {
    var params = {
        streamUrl:          url,
        plexToken:          plexToken,
        ratingKey:          d.ratingKey,
        partKey:            d.partKey,
        partId:             d.partId,
        title:              d.title,
        viewOffset:         d.viewOffset || 0,
        duration:           d.duration || 0,
        audioStreams:       d.audioStreams || [],
        subtitleStreams:    d.subtitleStreams || [],
        selectedAudioId:    d.selectedAudioId || "",
        selectedSubtitleId: d.selectedSubtitleId || "0",
        sessionId:          sessionId,
        isTranscoding:      d.forceTranscode || false
    }
    for (var key in (more || {}))
        params[key] = more[key]
    return params
}
