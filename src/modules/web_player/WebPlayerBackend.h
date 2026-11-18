#pragma once
#include <QObject>
#include <QString>
#include <QVariant>
#include "TmdbCatalog.h"

class AppCore;
class DisplayHandoff;
class QTimer;
class ScriptLauncher;

// A streaming service opened as its own web player, full screen: Netflix and
// Prime Video, one instance each. Neither has an API a front end like this one
// could browse with, and their streams are Widevine-protected, so mpv can't
// play them; Chromium with Widevine can. scripts/web-player.sh works out how
// to run the browser on each target, under the cage kiosk compositor on a
// headless Pi, and this runs it as a takeover through its own ScriptLauncher,
// the way the Scripts module runs a takeover script: on a headless Pi
// DisplayHandoff brackets the run, and the screen comes back once the browser
// and everything it started have exited. Its catalog (TmdbCatalog) is what the
// module browses before it opens a title.
//
// The browser keeps a profile of its own per service, and the service's
// sign-in lives there: SIGN IN in the module's settings opens signInUrl in it,
// and signOut() deletes it. YouTube's sign-in is one of these too, with no
// catalogue, owned by YouTubeBackend, whose yt-dlp reads the sign-in from it.
class WebPlayerBackend : public QObject {
    Q_OBJECT
    // Not done with the screen yet: the browser is open, or it has closed and
    // the processes it left behind are still exiting.
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Null for a service with no catalogue (YouTube's sign-in).
    Q_PROPERTY(QObject *catalog READ catalog CONSTANT)
    Q_PROPERTY(QString signInUrl READ signInUrl CONSTANT)
    // The player has been opened since the app started: the way back has been
    // shown once, and later titles open without stopping to show it again.
    Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)

public:
    struct Service {
        QString id;         // the short name the run and the browser profile go by ("netflix")
        QString homeUrl;    // the page the service opens at
        QString signInUrl;  // the page SIGN IN opens at
        // No providerName: a service with nothing to browse, so no catalogue.
        TmdbCatalog::Service catalog;
        // Its sign-in is read back from the browser's profile (YouTube's, by
        // yt-dlp), so Safari, which keeps none, won't do: on a Mac it needs
        // Google Chrome.
        bool needsChromium = false;
    };

    // The module's settings, and the app's Scaling, are read through appCore;
    // without one, there are none.
    explicit WebPlayerBackend(const Service &service,
                              const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                              DisplayHandoff *handoff, QObject *parent = nullptr);

    bool running() const;
    QObject *catalog() const { return m_catalog; }
    QString signInUrl() const { return m_signInUrl; }
    bool opened() const { return m_opened; }

    // Where web-player.sh keeps the browser's profile for this service, and so
    // its sign-in.
    QString browserProfile() const;

    // Opens the service at url, or at its home page. Returns false, with
    // lastError() saying why, when nothing was started: no browser, or no
    // kiosk compositor on a headless Pi. Those are checked before the screen is
    // handed over, so a missing package never blanks the screen.
    Q_INVOKABLE bool launch(const QString &url = QString());
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    // The last lines the launcher printed: the only diagnostic a user gets
    // when the browser exits at once on a headless Pi.
    Q_INVOKABLE QString output() const;

    // Closes the browser and everything it started: the way back without a
    // keyboard to close it with. As Ctrl+W closes it where web-player.sh
    // --close can, so it saves a fresh sign-in first; stopped outright where
    // it can't, or if it is still open a few seconds on.
    Q_INVOKABLE void close();

    // manifest: sign_out (action). Deletes the browser profile, which is where
    // the service's sign-in lives.
    Q_INVOKABLE void signOut();

public slots:
    // The module's catalogue region and language (wired by registerModule).
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void runningChanged();
    void openedChanged();
    // Once per launch. reason: "ok" (the browser was closed), "failed",
    // "stopped" (close() had to stop it) or "failed_to_start".
    void finished(int exitCode, const QString &reason);

private:
    // The module's Scaling, or the app's when it is "Default", as
    // web-player.sh takes it: letterbox, 14:9, panscan or anamorphic.
    QString scaling() const;
    QString moduleId() const { return QStringLiteral("com.osdos.") + m_service; }
    QString script() const;
    void    applySetting(const QString &key, const QVariant &value);
    // close()'s last resort: SIGTERM, then SIGKILL, to everything the run started.
    void    stop();

    QString         m_service;
    QString         m_url;
    QString         m_signInUrl;
    bool            m_needsChromium = false;
    TmdbCatalog    *m_catalog = nullptr;
    QString         m_appRoot;
    QString         m_dataRoot;
    AppCore        *m_appCore = nullptr;
    QString         m_lastError;
    ScriptLauncher *m_launcher = nullptr;
    bool            m_opened   = false;
    // Running from close() until the run ends: the browser's time to close.
    QTimer         *m_closeTimer = nullptr;
};
