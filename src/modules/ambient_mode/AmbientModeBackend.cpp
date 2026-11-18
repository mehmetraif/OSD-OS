#include "AmbientModeBackend.h"
#include "../../AppCore.h"
#include "../../audio/BackgroundMpv.h"
#include <QDir>
#include <QDebug>

static const QString kModuleId = QStringLiteral("com.osdos.ambient_mode");
static const QStringList kVideoExts = {
    "mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob"
};
static const QStringList kAudioExts = {
    "mp3", "wav", "flac", "m4a", "m3u", "ogg", "aac", "m3u8"
};

AmbientModeBackend::AmbientModeBackend(const QString &dataRoot, AppCore *appCore, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_mediaRoot(dataRoot + "/ambient"),
      m_audio(new BackgroundMpv(QStringLiteral("ambient"), "[AmbientMode] audio", this))
{
    // Resolve the configured media directory (falls back to the dataRoot/ambient default).
    const QString dir = appCore ? appCore->get_setting(kModuleId, QStringLiteral("media_directory")).toString()
                                : QString();
    if (!dir.isEmpty())
        setMediaRoot(dir);
}

// As the app ends: mpv is gone before this returns.
AmbientModeBackend::~AmbientModeBackend()
{
    m_audio->end(true);
}

QString AmbientModeBackend::mediaRoot() const
{
    return m_mediaRoot;
}

void AmbientModeBackend::setMediaRoot(const QString &path)
{
    // An empty (reset) setting means back to the dataRoot/ambient default.
    m_mediaRoot = path.isEmpty() ? m_dataRoot + "/ambient" : path;
    QDir().mkpath(m_mediaRoot);
    qDebug("[AmbientMode] media root: %s", qPrintable(m_mediaRoot));
}

QVariantList AmbientModeBackend::scanFiles(const QStringList &extensions) const
{
    QVariantList result;
    QDir dir(m_mediaRoot);
    if (!dir.exists())
        return result;
    for (const QString &name : dir.entryList(QDir::Files, QDir::Name)) {
        if (!extensions.contains(QFileInfo(name).suffix().toLower()))
            continue;
        QVariantMap item;
        item["name"] = name;
        item["path"] = dir.absoluteFilePath(name);
        result.append(item);
    }
    return result;
}

QVariantList AmbientModeBackend::getVideoFiles() const
{
    return scanFiles(kVideoExts);
}

QVariantList AmbientModeBackend::getAudioFiles() const
{
    return scanFiles(kAudioExts);
}

void AmbientModeBackend::startAudio(const QStringList &paths, bool shuffle)
{
    // The audio before is gone before this starts: not two on the card at once.
    m_audio->end(true);

    if (paths.isEmpty())
        return;

    // Absolute paths from scanFiles(). An .m3u/.m3u8 counts as ONE entry here:
    // mpv expands it lazily when playback reaches it, so shuffling picks whole
    // playlists as units and the tracks inside one still play in their listed
    // order. mpv shuffles once, at load — with --loop-playlist=inf the same
    // shuffled order replays on every wrap rather than being reshuffled each
    // cycle.
    if (!m_audio->start(paths, shuffle ? QStringList{ QStringLiteral("--shuffle") } : QStringList())) {
        qWarning("[AmbientMode] mpv not found in PATH — audio will not play");
        return;
    }
    qDebug("[AmbientMode] audio process started: %lld entr%s%s",
           static_cast<long long>(paths.size()),
           paths.size() == 1 ? "y" : "ies",
           shuffle ? ", shuffled" : "");
}

// Stopped from the player, mpv is not waited for: the menus come back at
// once, and the menu music has the card only once mpv has gone.
void AmbientModeBackend::stopAudio()
{
    m_audio->end(false);
}

void AmbientModeBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    if (moduleId == kModuleId && key == QLatin1String("media_directory"))
        setMediaRoot(value.toString());
}
