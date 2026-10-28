#include "MenuMusic.h"
#include "AudioOutput.h"
#include "../util/MpvLocator.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>

MenuMusic *MenuMusic::s_instance = nullptr;

namespace {
// How long the menus wait, nothing holding the music, before it starts.
constexpr int kStartDelayMs = 700;
// mpv ending sooner than this after it started couldn't play the file.
constexpr qint64 kFailedWithinMs = 3000;
// Where a system keeps its General MIDI SoundFont: Debian's and Raspberry Pi
// OS's (fluid-soundfont-gm, timgm6mb-soundfont), others', Homebrew's.
const char *const kSystemSoundFonts[] = {
    "/usr/share/sounds/sf2/default-GM.sf2",
    "/usr/share/sounds/sf2/FluidR3_GM.sf2",
    "/usr/share/sounds/sf2/TimGM6mb.sf2",
    "/usr/share/soundfonts/default.sf2",
    "/usr/share/soundfonts/FluidR3_GM.sf2",
    "/opt/homebrew/share/soundfonts/default.sf2",
    "/usr/local/share/soundfonts/default.sf2",
};

// Calls `over` once the process is over, with its exit code, -1 if it
// crashed or couldn't be started at all (which emits no finished()).
template <typename Over>
void whenOver(QProcess *process, QObject *context, Over over) {
    QObject::connect(process, &QProcess::finished, context, [over](int code, QProcess::ExitStatus status) {
        over(status == QProcess::NormalExit ? code : -1);
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [over](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            over(-1);
    });
}
} // namespace

MenuMusic::MenuMusic(const QString &dataRoot, QObject *parent) : QObject(parent), m_dataRoot(dataRoot) {
    s_instance = this;
    m_socketPath = QDir::tempPath() + QStringLiteral("/osdos-menu-music-%1.sock")
                                          .arg(QCoreApplication::applicationPid());
    m_startDelay.setSingleShot(true);
    m_startDelay.setInterval(kStartDelayMs);
    connect(&m_startDelay, &QTimer::timeout, this, &MenuMusic::start);
    // mpv makes its socket a moment after it starts.
    m_connect.setInterval(100);
    connect(&m_connect, &QTimer::timeout, this, [this]() {
        if (m_ipc.state() == QLocalSocket::UnconnectedState)
            m_ipc.connectToServer(m_socketPath);
    });
    connect(&m_ipc, &QLocalSocket::connected, this, [this]() {
        m_connect.stop();
        // Set while the socket was still being made (the slider, as the
        // music came back after a video): told now, or mpv would keep the
        // volume it started with.
        sendVolume();
    });
}

void MenuMusic::sendVolume() {
    if (m_ipc.state() != QLocalSocket::ConnectedState)
        return;
    const QJsonObject command{ { "command", QJsonArray{ "set_property", "volume", m_volume } } };
    m_ipc.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
}

// Whether the music should be playing as things now are.
bool MenuMusic::wantedNow() const {
    return m_wanted && m_holds.isEmpty() && !m_source.isEmpty() && m_source != m_failed;
}

MenuMusic::~MenuMusic() {
    stopNow();
    if (s_instance == this)
        s_instance = nullptr;
}

void MenuMusic::setSource(const QString &source) {
    if (source == m_source)
        return;
    m_source = source;
    m_failed.clear();
    m_renderModule = false;
    stopNow();
    reconsider();
    emit sourceChanged();
}

void MenuMusic::setVolume(int volume) {
    volume = qBound(0, volume, 100);
    if (volume == m_volume)
        return;
    m_volume = volume;
    sendVolume();
    emit volumeChanged();
}

void MenuMusic::setWanted(bool wanted) {
    if (wanted == m_wanted)
        return;
    m_wanted = wanted;
    reconsider();
    emit wantedChanged();
}

void MenuMusic::hold(const QString &who) {
    if (!s_instance)
        return;
    s_instance->m_holds.insert(who);
    s_instance->reconsider();
}

void MenuMusic::release(const QString &who) {
    if (!s_instance || !s_instance->m_holds.remove(who))
        return;
    s_instance->reconsider();
}

void MenuMusic::restart() {
    if (!playing())
        return;
    stopNow();
    reconsider();
}

QString MenuMusic::path() const {
    const QUrl url(m_source);
    return url.isLocalFile() ? url.toLocalFile() : m_source;
}

MenuMusic::Kind MenuMusic::kindOf(const QString &file) {
    const QString type = QFileInfo(file).suffix().toLower();
    if (type == QLatin1String("mid") || type == QLatin1String("midi"))
        return Kind::Midi;
    if (type == QLatin1String("xm") || type == QLatin1String("mod") || type == QLatin1String("s3m")
            || type == QLatin1String("it"))
        return Kind::Module;
    return Kind::Recording;
}

void MenuMusic::reconsider() {
    if (!wantedNow()) {
        m_startDelay.stop();
        // At once: whatever holds it is about to open the sound card.
        stopNow();
    } else if (!playing() && !rendering() && !m_startDelay.isActive()) {
        m_startDelay.start();
    }
}

void MenuMusic::start() {
    if (playing() || rendering() || !wantedNow())
        return;
    const QString file = path();
    if (!QFileInfo(file).isFile()) {
        qWarning("[MenuMusic] %s: no such file", qPrintable(file));
        m_failed = m_source;
        return;
    }
    const Kind kind = kindOf(file);
    if (kind == Kind::Midi || (kind == Kind::Module && m_renderModule))
        render(file);
    else
        play(file);
}

QString MenuMusic::soundFont(const QString &midi) const {
    const QFileInfo info(midi);
    QStringList candidates = { info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                               + QStringLiteral(".sf2") };
    if (!m_dataRoot.isEmpty()) {
        const QFileInfoList own = QDir(m_dataRoot + QStringLiteral("/soundfonts"))
                                      .entryInfoList({ QStringLiteral("*.sf2"), QStringLiteral("*.SF2") },
                                                     QDir::Files, QDir::Name);
        for (const QFileInfo &font : own)
            candidates << font.absoluteFilePath();
    }
    for (const char *font : kSystemSoundFonts)
        candidates << QString::fromLatin1(font);
    for (const QString &font : candidates) {
        if (QFileInfo(font).isFile())
            return font;
    }
    return {};
}

QString MenuMusic::renderedPath(const QString &file) const {
    // Made again when the file, or the SoundFont it is played with, changes.
    const QFileInfo info(file);
    QByteArray key = info.canonicalFilePath().toUtf8() + '|' + QByteArray::number(info.size()) + '|'
                     + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    if (kindOf(file) == Kind::Midi)
        key += '|' + soundFont(file).toUtf8();
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/menu-music/")
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".wav");
}

void MenuMusic::render(const QString &file) {
    const QString out = renderedPath(file);
    if (QFileInfo(out).size() > 0) {
        play(out);
        return;
    }
    // Made under another name, its own (one stopped but slow to go mustn't
    // take the next one's with it), and named once whole.
    const QString part = out.left(out.size() - 4) + QStringLiteral(".%1.part.wav").arg(++m_renders);
    QString program;
    QStringList args;
    if (kindOf(file) == Kind::Midi) {
        program = QStandardPaths::findExecutable(QStringLiteral("fluidsynth"));
        const QString font = soundFont(file);
        if (program.isEmpty() || font.isEmpty()) {
            qWarning("[MenuMusic] %s: a MIDI file needs FluidSynth and a SoundFont: %s", qPrintable(file),
                     program.isEmpty() ? "fluidsynth is not installed" : "no SoundFont found");
            m_failed = m_source;
            return;
        }
        // No MIDI input, no shell; as fast as it can, into the file.
        args << QStringLiteral("-ni") << QStringLiteral("-q") << QStringLiteral("-g") << QStringLiteral("0.8")
             << QStringLiteral("-r") << QStringLiteral("44100") << QStringLiteral("-F") << part << font << file;
    } else {
        program = QStandardPaths::findExecutable(QStringLiteral("openmpt123"));
        if (program.isEmpty()) {
            qWarning("[MenuMusic] mpv couldn't play %s, and openmpt123 is not installed", qPrintable(file));
            m_failed = m_source;
            return;
        }
        args << QStringLiteral("--batch") << QStringLiteral("--quiet") << QStringLiteral("--force")
             << QStringLiteral("--samplerate") << QStringLiteral("44100") << QStringLiteral("--repeat")
             << QStringLiteral("0") << QStringLiteral("-o") << part << QStringLiteral("--") << file;
    }
    const QString dir = QFileInfo(out).absolutePath();
    QDir().mkpath(dir);
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    whenOver(process, this, [this, process, file, part, out, dir](int code) {
        const QByteArray output = process->readAll().trimmed().right(300);
        const QByteArray said = output.isEmpty() && code == -1 ? process->errorString().toUtf8() : output;
        process->deleteLater();
        // Stopped (stopNow(): it is no longer m_render): what it made so far
        // goes.
        if (process != m_render) {
            QFile::remove(part);
            return;
        }
        m_render = nullptr;
        if (code != 0 || QFileInfo(part).size() <= 0) {
            qWarning("[MenuMusic] %s couldn't be made into a WAV (exit %d): %s", qPrintable(file), code,
                     said.constData());
            QFile::remove(part);
            m_failed = m_source;
            return;
        }
        // Only the newest is kept.
        const QFileInfoList made = QDir(dir).entryInfoList({ QStringLiteral("*.wav") }, QDir::Files);
        for (const QFileInfo &old : made) {
            if (old.absoluteFilePath() != part)
                QFile::remove(old.absoluteFilePath());
        }
        QFile::rename(part, out);
        start();
    });
    m_render = process;
    qInfo("[MenuMusic] making %s into a WAV with %s", qPrintable(file), qPrintable(QFileInfo(program).fileName()));
    process->start(program, args);
}

void MenuMusic::play(const QString &file) {
    const QString bin = mpvbin::locate();
    if (bin.isEmpty()) {
        qWarning("[MenuMusic] mpv not found: no menu music");
        return;
    }
    QStringList args;
    args << QStringLiteral("--no-config")
         << QStringLiteral("--no-video")
         << QStringLiteral("--no-terminal")
         << QStringLiteral("--really-quiet")
         << QStringLiteral("--loop-file=inf")
         << QStringLiteral("--volume=%1").arg(m_volume)
         << QStringLiteral("--input-ipc-server=%1").arg(m_socketPath)
         // Settings → Audio Output's card, while it is plugged in; Auto's as
         // ALSA's default (applyTo() below).
         << AudioOutput::mpvArgs()
         << QStringLiteral("--")
         << file;
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    AudioOutput::applyTo(env);
    process->setProcessEnvironment(env);
    QElapsedTimer clock;
    clock.start();
    whenOver(process, this, [this, process, clock](int code) {
        process->deleteLater();
        // Killed by stopNow(): it is no longer m_process.
        if (process != m_process)
            return;
        // Over on its own: its socket isn't waited for any more.
        m_process = nullptr;
        m_connect.stop();
        m_ipc.abort();
        emit playingChanged();
        if (clock.elapsed() >= kFailedWithinMs)
            return;
        // A file it couldn't play isn't tried again until the source changes.
        if (kindOf(path()) == Kind::Module && !m_renderModule
                && !QStandardPaths::findExecutable(QStringLiteral("openmpt123")).isEmpty()) {
            // mpv's ffmpeg without libopenmpt: openmpt123 makes it a WAV.
            m_renderModule = true;
            start();
        } else {
            const QString why = code == -1 ? process->errorString() : QStringLiteral("exit %1").arg(code);
            qWarning("[MenuMusic] mpv couldn't play %s (%s)", qPrintable(path()), qPrintable(why));
            m_failed = m_source;
        }
    });
    m_process = process;
    QFile::remove(m_socketPath);
    qInfo("[MenuMusic] playing %s", qPrintable(file));
    process->start(bin, args);
    // Unless it couldn't even be started, and is over already.
    if (m_process == process) {
        m_connect.start();
        emit playingChanged();
    }
}

void MenuMusic::stopNow() {
    if (QProcess *render = m_render) {
        // Its CPU is the video's now: made again from the start next time.
        m_render = nullptr;
        render->kill();
        render->waitForFinished(300);
    }
    m_connect.stop();
    m_ipc.abort();
    QProcess *process = m_process;
    if (!process)
        return;
    m_process = nullptr;
    if (process->state() != QProcess::NotRunning) {
        // Killed and waited for: the sound card is free when this returns.
        process->kill();
        process->waitForFinished(300);
    }
    QFile::remove(m_socketPath);
    emit playingChanged();
}
