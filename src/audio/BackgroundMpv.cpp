#include "BackgroundMpv.h"
#include "AudioOutput.h"
#include "MenuMusic.h"
#include "../util/EndProcess.h"
#include "../util/MpvLocator.h"
#include "../util/RuntimeFiles.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QTimer>
#include <utility>

BackgroundMpv::BackgroundMpv(const QString &tag, const char *what, QObject *parent)
    : QObject(parent), m_tag(tag), m_what(what) {}

BackgroundMpv::~BackgroundMpv() {
    end(true);
}

bool BackgroundMpv::start(const QStringList &files, const QStringList &options, const QString &socketName) {
    // One asked to end a moment ago still has the sound card: not two at once.
    if (m_ending && m_ending->state() != QProcess::NotRunning)
        m_ending->waitForFinished(1000);

    const QString bin = mpvbin::locate();
    if (bin.isEmpty())
        return false;

    // Its own socket name: MpvController uses osdos-mpv.sock for the player,
    // and two mpvs sharing one path would hand the wrong process our commands.
    // In a folder of the user's own rather than the temp folder every account
    // writes in (util/RuntimeFiles.h), asked for at each start: the folder may
    // have been taken away and made again since. Where there is none, it
    // plays without one, and can't be paused.
    m_socketPath = socketName.isEmpty() ? QString() : runtimefiles::path(socketName);

    QStringList args;
    args << QStringLiteral("--no-video")
         << QStringLiteral("--loop-playlist=inf")
         << QStringLiteral("--no-terminal")
         << QStringLiteral("--really-quiet")
         << options;
    if (!m_socketPath.isEmpty())
        args << QStringLiteral("--input-ipc-server=%1").arg(m_socketPath);
    // Settings → Sound → Audio Output's card, while it is plugged in; Auto's as
    // ALSA's default (applyTo() below).
    args << AudioOutput::mpvArgs()
         // Everything after this is a file, so an entry starting with '-'
         // cannot be mistaken for an option.
         << QStringLiteral("--")
         << files;

    m_paused = false;
    // The menu music stops first: this has the sound card now.
    MenuMusic::hold(m_tag);
    m_process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    AudioOutput::applyTo(env);
    m_process->setProcessEnvironment(env);
    // mpv ending on its own, or never starting: the menu music has the card
    // back, and no stale process is kept for the next start() to end.
    connect(m_process, &QProcess::finished, this, [this]() {
        qInfo("%s process exited", m_what);
        end(false);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning("%s process could not be started", m_what);
        end(false);
    });
    m_process->start(bin, args);

    // mpv creates the socket a beat after launch, so a single connect attempt
    // loses the race. Same retry shape as MpvController. Not for one that
    // couldn't be started, and has been ended already.
    if (m_process && !m_socketPath.isEmpty()) {
        if (!m_ipc) {
            m_ipc = new QLocalSocket(this);
            connect(m_ipc, &QLocalSocket::connected, this, [this]() { m_connect->stop(); });
        }
        if (!m_connect) {
            m_connect = new QTimer(this);
            m_connect->setInterval(100);
            connect(m_connect, &QTimer::timeout, this, [this]() {
                if (m_ipc->state() == QLocalSocket::ConnectedState
                    || m_ipc->state() == QLocalSocket::ConnectingState)
                    return;
                m_ipc->connectToServer(m_socketPath);
            });
        }
        m_connect->start();
    }
    emit stateChanged();
    return true;
}

void BackgroundMpv::end(bool wait) {
    if (m_connect) m_connect->stop();
    if (m_ipc)     m_ipc->abort();
    // One asked to end a moment ago still has the sound card.
    if (wait && m_ending && m_ending->state() != QProcess::NotRunning)
        m_ending->waitForFinished(1000);

    QProcess *process = std::exchange(m_process, nullptr);  // cleared first: terminate() can re-enter
    m_paused = false;
    if (!process)
        return;
    process->disconnect(this);      // ended here: not taken for an end of its own (start())
    if (process->state() != QProcess::NotRunning && !wait) {
        m_ending = process;
        endProcessLater(process, this, [this]() {
            // Not while it plays again: its hold is the new one's.
            if (!m_process)
                MenuMusic::release(m_tag);
        });
        qDebug("%s process asked to stop", m_what);
        emit stateChanged();
        return;
    }
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        process->waitForFinished(1000);
    }
    process->deleteLater();
    MenuMusic::release(m_tag);
    qDebug("%s process stopped", m_what);
    emit stateChanged();
}

bool BackgroundMpv::setPaused(bool paused) {
    if (!running() || !m_ipc || m_ipc->state() != QLocalSocket::ConnectedState)
        return false;
    m_paused = paused;
    // set_property, not "cycle pause": our idea of the state and mpv's can then
    // never drift apart.
    const QJsonObject cmd{
        { QStringLiteral("command"),
          QJsonArray{ QStringLiteral("set_property"), QStringLiteral("pause"), m_paused } }
    };
    m_ipc->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
    emit stateChanged();
    return true;
}
