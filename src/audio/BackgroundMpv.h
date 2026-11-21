#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

class MpvSocket;

// An mpv of a module's own that plays sound alone, beside the module's screen
// or under its clips: Weather's music and Ambient Mode's audio. Never the
// screen's: no video, a process apart from MpvController's, its playlist
// looped (--loop-playlist=inf), on Settings → Sound → Audio Output's card.
//
// The menu music is held off while it plays (MenuMusic::hold(), under its
// tag) and let go once its mpv has gone and the sound card is free. Ended
// from a view, mpv isn't waited for (util/EndProcess.h): the menus come back
// at once, and it goes in its own time. mpv ending on its own (no file it can
// play, no network, the sound card gone), or never starting, ends it as
// end(false) does, so the menu music plays again and the next start() starts
// afresh.
class BackgroundMpv : public QObject {
    Q_OBJECT
public:
    // `tag`: its hold on the menu music ("weather"). `what`: its name at the
    // head of its lines in the log ("[Weather] music").
    BackgroundMpv(const QString &tag, const char *what, QObject *parent = nullptr);
    // Ended, and waited for.
    ~BackgroundMpv() override;

    // Plays `files` (paths, URLs, playlists), with mpv's `options` (--shuffle).
    // With a `socketName`, a control socket of that name in the run-time
    // folder (util/RuntimeFiles.h), for setPaused(); where that folder can't
    // be had, it plays without one. One asked to end a moment ago is waited
    // for first: not two on the card at once. False, with nothing started,
    // where there is no mpv.
    bool start(const QStringList &files, const QStringList &options = {},
               const QString &socketName = QString());
    // `wait`: gone before this returns (as the app ends, or before another
    // starts). Otherwise asked to end and left to go.
    void end(bool wait);

    // Started, and not ended yet.
    bool active() const { return m_process != nullptr; }
    // Its mpv running, paused or not.
    bool running() const { return m_process && m_process->state() != QProcess::NotRunning; }
    bool paused() const { return m_paused; }
    // Paused or played on, over its control socket, where the track is.
    // False, with nothing changed, where it isn't running or isn't connected
    // to its socket (yet).
    bool setPaused(bool paused);

signals:
    // Started, ended, paused or played on.
    void stateChanged();

private:
    QString            m_tag;
    const char        *m_what;
    QProcess          *m_process = nullptr;
    QPointer<QProcess> m_ending;            // asked to end, not gone yet
    MpvSocket         *m_ipc = nullptr;
    QString            m_socketPath;
    bool               m_paused = false;
};
