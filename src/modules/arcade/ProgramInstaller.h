#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// A program for Windows, where no package manager brings it, downloaded as
// a 7z archive and unpacked into <data>/arcade: RetroArch, libretro's latest
// stable build from its build server (Settings → Arcade → FinalBurn Neo),
// and MAME, mamedev's latest release from GitHub, a self-extracting archive
// (Settings → Arcade → MAME). The archive's URL is read from the server's
// index (the build server's list of versions, GitHub's release), else the
// one known is taken; the archive is downloaded as a .part file and unpacked
// beside it (SevenZip), and the folder the program came out in is moved into
// place once it is whole, over one downloaded before. Nothing is in place
// before all of it is.
class ProgramInstaller : public QObject {
    Q_OBJECT
public:
    struct Source {
        QString name;              // "RetroArch", "MAME": for the messages
        bool    available = false; // built for this machine: 64-bit Windows
        // Where the archive's URL is read from; none to take fallback as it
        // is. GitHub's API is asked as it wants (github).
        QUrl    index;
        bool    github = false;
        // The archive's URL from the index's answer; an empty URL for none.
        std::function<QUrl(const QByteArray &index)> pick;
        // The archive known, taken when the index can't be read or names
        // none.
        QUrl    fallback;
        // What the archive must hold, at its top or one folder down.
        QString program;
        // The folder it ends up as: <data>/arcade/retroarch/RetroArch-Win64.
        QString target;
    };
    // libretro's RetroArch and mamedev's MAME, for a 64-bit Windows.
    static Source retroarch(const QString &dataRoot);
    static Source mame(const QString &dataRoot);
    // The newest version a page of libretro's build server links to (1.22.2
    // over 1.9.1), "" for none.
    static QString newestVersion(const QByteArray &page);
    // The 64-bit Windows build among a GitHub release's assets
    // (mame0289b_64bit.exe), an empty URL for none.
    static QUrl mameWindowsAsset(const QByteArray &release);

    explicit ProgramInstaller(QObject *parent = nullptr);

    // Downloads and unpacks it; finished() follows. A download cut short
    // leaves what was there.
    void start(const Source &source);
    bool running() const { return m_running; }
    int  progress() const { return m_progress; }   // 0 to 100

signals:
    void progressChanged();
    void finished(bool ok, const QString &message);

private:
    void fetchIndex();
    void fetchArchive(const QUrl &url);
    void unpack();
    void setProgress(int progress);
    void fail(const QString &message);
    // The archive as it downloads, and the folder it is unpacked in, both
    // beside the folder it ends up as.
    QString archivePart() const;
    QString unpackFolder() const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFutureWatcher<QString> *m_watcher = nullptr;
    Source m_source;
    int  m_progress = 0;
    bool m_running = false;
};
