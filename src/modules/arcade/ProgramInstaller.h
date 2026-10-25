#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// A program for Windows, where no package manager brings it, downloaded as
// a 7z archive and unpacked into <data>/arcade: RetroArch (Settings → Arcade
// → FinalBurn Neo), from libretro's build server, and MAME (Settings → Arcade
// → MAME), from mamedev's releases on GitHub, a self-extracting archive. Each
// is the version the OSD/OS image has, Debian trixie's (kRetroArchVersion,
// kMameVersion), so that a PC plays with a Pi on the same programs: the same
// sets, and the same RetroArch over the network. The archive is downloaded as
// a .part file and unpacked beside it (SevenZip), and the folder the program
// came out in is moved into place once it is whole, over one downloaded
// before. Nothing is in place before all of it is.
class ProgramInstaller : public QObject {
    Q_OBJECT
public:
    struct Source {
        QString name;              // "RetroArch", "MAME": for the messages
        QString version;           // the one downloaded: "1.20.0", "0.276"
        bool    available = false; // built for this machine: 64-bit Windows
        QUrl    archive;           // the archive, a 7z or a self-extracting one
        // What the archive must hold, at its top or one folder down.
        QString program;
        // The folder it ends up as: <data>/arcade/retroarch/RetroArch-Win64.
        QString target;
    };
    // libretro's RetroArch and mamedev's MAME, for a 64-bit Windows.
    static Source retroarch(const QString &dataRoot);
    static Source mame(const QString &dataRoot);

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
