#pragma once
#include <QRegularExpression>
#include <QString>

// Text for the log, or for a notice on the screen, with every secret an
// address or a header can hold blanked out: a media server's token in a
// URL's query (Jellyfin's ApiKey, Emby's api_key), Plex's in its header,
// Jellyfin's in its Authorization header, and the sign-in of an address,
// before its host (scheme://user:password@host) or in its query
// (?username=...&password=..., as a television provider's addresses have
// it), which a card's tag file, an .m3u or a playlist may hold and mpv
// prints as it was given. The host and the rest of the query stay: they
// are what a log is read for. Header only: what mpv says goes
// through it wherever it is logged, a process's output (MpvController) and
// libmpv's messages (EmbeddedMpv) alike.
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
    // The sign-in in a query: the value of a key that is, or ends in,
    // "password", "passwd", "pwd" or "token" (stream_password, access_token),
    // or is "pass", "pw", "username", "user" or "login", written in any
    // case. The name goes with the password, as it does before a host. Up
    // to the next "&", so that a password's own "#" or ";" leaves no tail;
    // an empty value stays, which says that none was given. A key that only
    // holds one of the words (bypass, userid, password_hint) is no sign-in.
    static const QRegularExpression kQuerySignIn(
        QStringLiteral("([?&;](?:[\\w.-]*(?:password|passwd|pwd|token)|pass|pw|username|user|login)=)[^&\\s]+"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(kApiKey, QStringLiteral("ApiKey=REDACTED"));
    text.replace(kPlexToken, QStringLiteral("X-Plex-Token=REDACTED"));
    text.replace(kQuotedToken, QStringLiteral("Token=\"REDACTED\""));
    text.replace(kSignIn, QStringLiteral("://REDACTED@"));
    text.replace(kQuerySignIn, QStringLiteral("\\1REDACTED"));
    return text;
}
