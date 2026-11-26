#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

class QTimer;

// Every QML file of a new build compiled into Qt's QML cache once the menus
// are up and idle, so that the pages opened after it, and every start after
// that, find them compiled. Qt keeps that cache by each file's path and
// time, and an update (the launcher's swap of /opt/osdos, a new AppImage or
// app bundle) changes them all: each page compiled again as it first opened,
// and the first start's menus before their first frame (12.9 s on a Pi 4
// where a start with the cache takes 2). The image's build compiles it ahead
// (`osdos --compile-qml`, os/stage-osdos/01-app); an update can't, from the
// old version, before the new one is in place: the launcher puts it there
// as the app starts again, and the old app has no new binary to run.
//
// The build is told by its stamp (stamp()): the app's binary, its size and
// time, the QML root and its Main.qml's time, and Qt's version. A stamp
// already compiled is in the cache folder's `qmlcache-built.json`, written
// by `--compile-qml` (markBuilt()) and by this once it has compiled; a build
// whose stamp is there starts nothing. Otherwise `program args` (the app
// itself with `--compile-qml`) is started once nothing is busy (busy(): a
// video or a game on the screen), at the lowest priority (nice 19, and on
// Linux SCHED_IDLE; below normal on Windows), stopped should something
// become busy and started again once it isn't, and given up on after
// timeoutMs. Its failure fails nothing: the pages are compiled as they
// open, as before, and the next run tries again.
class QmlWarmUp : public QObject {
    Q_OBJECT
public:
    QmlWarmUp(const QString &program, const QStringList &args, const QString &marker, const QString &stamp,
              QObject *parent = nullptr);
    ~QmlWarmUp() override;

    // This build's stamp: the binary at `binary`, the QML under `appRoot`,
    // the Qt the app runs on.
    static QString stamp(const QString &binary, const QString &appRoot);
    // Where the stamp compiled is kept: the cache folder, beside qmlcache.
    static QString markerPath();
    // Whether `stamp` is the one kept at `marker`.
    static bool built(const QString &marker, const QString &stamp);
    // `stamp` kept at `marker` as compiled; false where it couldn't be.
    static bool markBuilt(const QString &marker, const QString &stamp);

    void setBusy(std::function<bool()> busy) { m_busy = std::move(busy); }
    // How often busy() is asked while it is waited for or the compile runs.
    void setPollMs(int ms) { m_pollMs = ms; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Once, `delayMs` from now (the menus' first frame, and a moment for
    // them to settle): nothing where this stamp is compiled.
    void start(int delayMs);
    bool running() const { return m_process != nullptr; }

signals:
    // Compiled and kept (true), or given up on for this run (false).
    void finished(bool compiled);

private:
    void poll();
    void launch();
    void stopProcess();

    QString m_program;
    QStringList m_args;
    QString m_marker;
    QString m_stamp;
    std::function<bool()> m_busy;
    int m_pollMs = 5000;
    int m_timeoutMs = 600000;
    QTimer *m_poll = nullptr;
    QTimer *m_timeout = nullptr;
    QPointer<QProcess> m_process;
    bool m_done = false;
};
