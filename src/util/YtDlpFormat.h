#pragma once
#include <QString>
#include <QStringList>

namespace ytdlp {

// The format yt-dlp is to choose a YouTube video by (-f, and mpv's
// --ytdl-format): at most `height` lines and, with fps30, 30 fps; H.264 first
// where h264First (the Pi decodes it in hardware), then any codec; the audio
// track in audioLanguage, by yt-dlp's language code ("" or "original": the
// one the video was made in). Each falls back to what the video has, down to
// its best single file. YouTube's ADVANCED settings and the NFC Reader's
// YouTube cards both choose through it.
inline QString format(int height, bool h264First, bool fps30, const QString &audioLanguage) {
    // "<=?" also takes a format that doesn't say its height or rate.
    QString cap = QStringLiteral("[height<=?%1]").arg(height);
    if (fps30)
        cap += QStringLiteral("[fps<=?30]");

    QStringList videos;
    if (h264First)
        videos << QStringLiteral("bestvideo") + cap + QStringLiteral("[vcodec^=avc1]");
    videos << QStringLiteral("bestvideo") + cap;

    // Plain bestaudio is the original track: yt-dlp ranks it first.
    QStringList audios;
    const QString language = audioLanguage.trimmed().toLower();
    if (!language.isEmpty() && language != QLatin1String("original"))
        audios << QStringLiteral("bestaudio[language^=%1]").arg(language);
    audios << QStringLiteral("bestaudio");

    // The language outranks the codec: a dub in VP9 before the original in H.264.
    QStringList choices;
    for (const QString &audio : audios)
        for (const QString &video : videos)
            choices << video + QLatin1Char('+') + audio;
    choices << QStringLiteral("best") + cap << QStringLiteral("best");
    return choices.join(QLatin1Char('/'));
}

}
