#pragma once
#include <QRegularExpression>
#include <QString>

// Text for the log, or for a notice on the screen, with every secret an
// address or a header can hold blanked out: a media server's token in a
// URL's query (Jellyfin's ApiKey, Emby's api_key), Plex's in its header,
// Jellyfin's in its Authorization header, and the sign-in before an
// address's host (scheme://user:password@host), which a card's tag file, an
// .m3u or a playlist may hold and mpv prints as it was given. The host
// stays: it is what a log is read for. Header only: what an mpv process
// prints goes through it on its way to the log (MpvController).
// scripts/save-logs.sh does the same to the logs it copies (hide()).
inline QString redactSecrets(QString text) {
    static const QRegularExpression kApiKey(QStringLiteral("Api[_-]?Key=[^&\\s]+"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kPlexToken(QStringLiteral("X-Plex-Token[=:][^&\\s]+"));
    static const QRegularExpression kQuotedToken(QStringLiteral("Token=\"[^\"]+\""));
    // The sign-in, name and all, in one of two shapes:
    //  - with a password (a name, a colon, and what follows is no port
    //    number): up to the word's last "@". A password written with its
    //    own "/", "?" or "#" makes an address no player can open, which is
    //    when mpv prints it, and a sign-in taken to end at the first of
    //    those, or at an "@" of the password's before one, would leave the
    //    password's tail. An address that has a password and another "@"
    //    further on loses its host for that.
    //  - without one (a name alone, which may be a token; a password of
    //    digits): up to the last "@" before the path, as FFmpeg reads it.
    // An "@" in a path or a query alone is no sign-in (a channel's address,
    // a search), and "host:8096" and "[fe80::1]:8096" are a host and its port.
    static const QRegularExpression kSignIn(QStringLiteral(
        "://(?:[^/?#\\s:@\\[\"]*:[0-9]*[^0-9/?#\\s]\\S*|[^/?#\\s]*)@"));
    text.replace(kApiKey, QStringLiteral("ApiKey=REDACTED"));
    text.replace(kPlexToken, QStringLiteral("X-Plex-Token=REDACTED"));
    text.replace(kQuotedToken, QStringLiteral("Token=\"REDACTED\""));
    text.replace(kSignIn, QStringLiteral("://REDACTED@"));
    return text;
}
