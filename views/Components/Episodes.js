.pragma library

// The episode a show's or a season's PLAY opens, of its episodes in order:
// the first one part-watched, else the first unwatched, else the first.
// watched says whether an episode has been seen, in its server's terms
// (Plex's viewCount, Jellyfin's and Emby's played). Plex's backend picks a
// card's episode the same way (PlexBackend::pickOnDeckEpisode).
function best(episodes, watched) {
    if (!episodes || episodes.length === 0)
        return null
    for (var i = 0; i < episodes.length; i++)
        if (episodes[i].viewOffset > 0)
            return episodes[i]
    for (var j = 0; j < episodes.length; j++)
        if (!watched(episodes[j]))
            return episodes[j]
    return episodes[0]
}
