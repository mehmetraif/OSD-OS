#pragma once
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>

// Settings → Menu Music, or a theme's: a tune looping under the menus. Main.qml
// says what plays (`source`, a sound file, "" for none), how loud (`volume`,
// 0 to 100) and when it is wanted: in the menus, and never while a video
// plays or loads. An mpv process plays it, on Settings → Audio Output's card.
//
// A recording (OGG, Opus, MP3, FLAC, WAV, M4A, AAC) and a tracker's module
// (XM, MOD, S3M, IT, through ffmpeg's libopenmpt) play as they are. A MIDI
// file is played by FluidSynth into a WAV first, with a SoundFont: the file's
// own (beside it, of its name), the first in the data folder's `soundfonts`,
// or the system's General MIDI one. So is a module mpv can't play, by
// openmpt123, where it is installed. What they make is kept in the cache
// folder (the newest only), so it is made once for a file.
//
// Anything about to play sound of its own (a video, a module's music, a script
// or a web player) holds it off first: hold() stops it there and then, so the
// sound card is free before the other starts, and it plays again once nothing
// holds it and it is still wanted, from the start. A file mpv can't play isn't
// tried again until the source changes.
class MenuMusic : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool wanted READ wanted WRITE setWanted NOTIFY wantedChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
public:
    // dataRoot: the data folder, for the user's SoundFonts.
    explicit MenuMusic(const QString &dataRoot = QString(), QObject *parent = nullptr);
    ~MenuMusic() override;

    QString source() const { return m_source; }
    void setSource(const QString &source);
    int volume() const { return m_volume; }
    void setVolume(int volume);
    bool wanted() const { return m_wanted; }
    void setWanted(bool wanted);
    bool playing() const { return m_process && m_process->state() != QProcess::NotRunning; }

    // `who` (any name, the same for its release) is about to play sound of
    // its own: the music stops now. No-ops without menu music.
    static void hold(const QString &who);
    static void release(const QString &who);

public slots:
    // On another sound card (Settings → Audio Output): from the start there.
    void restart();

signals:
    void sourceChanged();
    void volumeChanged();
    void wantedChanged();
    void playingChanged();

private:
    // What a file is: a recording or a module mpv plays as it is, or a MIDI
    // file FluidSynth plays into a recording first.
    enum class Kind { Recording, Module, Midi };
    static Kind kindOf(const QString &file);

    // Plays or stops as things now are, after a moment for a start, so that a
    // hold and release in quick turns doesn't start it between them.
    void reconsider();
    void start();
    bool wantedNow() const;
    // The volume to mpv, once its socket is there.
    void sendVolume();
    // mpv playing `file`, looping.
    void play(const QString &file);
    // The source made into a WAV in the cache, then played.
    void render(const QString &file);
    QString renderedPath(const QString &file) const;
    QString soundFont(const QString &midi) const;
    void stopNow();
    QString path() const;
    bool rendering() const { return m_render && m_render->state() != QProcess::NotRunning; }

    static MenuMusic *s_instance;

    QString m_dataRoot;
    QString m_source;
    int m_volume = 60;
    bool m_wanted = false;
    QSet<QString> m_holds;
    // A source mpv gave up on, not tried again until it changes.
    QString m_failed;
    QPointer<QProcess> m_process;
    // FluidSynth or openmpt123 making the WAV, while it does.
    QPointer<QProcess> m_render;
    // A module mpv couldn't play: openmpt123 makes it into a WAV instead.
    bool m_renderModule = false;
    // WAVs begun, each made under a name of its own.
    int m_renders = 0;
    QLocalSocket m_ipc;
    QString m_socketPath;
    QTimer m_startDelay;
    QTimer m_connect;
};
