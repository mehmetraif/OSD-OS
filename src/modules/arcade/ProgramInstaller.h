#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

// What Settings → Arcade's downloads share (FbneoInstaller, ProgramInstaller):
// one at a time (running()), its progress from 0 to 100, and finished() once
// it is over. The last of the work, off the GUI thread, gives m_watcher ""
// once all of it is in place, else why not; one that fails is logged, and
// what it brought goes (removeParts()). What was installed stays as it was.
class Installer : public QObject {
    Q_OBJECT
public:
    bool running() const { return m_running; }
    int  progress() const { return m_progress; }   // 0 to 100

signals:
    void progressChanged();
    void finished(bool ok, const QString &message);

protected:
    explicit Installer(QObject *parent);
    // Under way, from 0.
    void begin();
    void setProgress(int progress);
    void fail(const QString &message);

    QNetworkAccessManager *const m_nam;
    QFutureWatcher<QString> *const m_watcher;

private:
    // What is downloaded, for the log: "FinalBurn Neo", "RetroArch".
    virtual QString what() const = 0;
    virtual void removeParts() = 0;

    int  m_progress = 0;
    bool m_running = false;
};

// A program for Windows, where no package manager brings it, downloaded as
// a 7z archive and unpacked into <data>/arcade: RetroArch (Settings → Arcade
// → FinalBurn Neo), from libretro's build server, and MAME (Settings → Arcade
// → MAME), from mamedev's releases on GitHub, a self-extracting archive. Each
// is the version every OSD/OS machine plays with (scripts/engines.env): the
// OSD/OS image's, Debian trixie's, so that a PC plays with a Pi on the same
// programs: the same sets, and the same RetroArch over the network. The
// archive is downloaded as a .part file and unpacked beside it (SevenZip),
// and the folder the program came out in is moved into place once it is
// whole, over one downloaded before. Nothing is in place before all of it is.
// The archive is checked against its SHA-256 (scripts/engines.env) before
// any of it is unpacked, where one is known.
class ProgramInstaller : public Installer {
    Q_OBJECT
public:
    struct Source {
        QString name;              // "RetroArch", "MAME": for the messages
        QString version;           // the one downloaded: "1.20.0", "0.276"
        bool    available = false; // built for this machine: 64-bit Windows
        QUrl    archive;           // the archive, a 7z or a self-extracting one
        // Its SHA-256 in hexadecimal, which the download must have before
        // anything of it is unpacked; "" when none is known (not checked,
        // and logged).
        QString sha256;
        // What the archive must hold, at its top or one folder down.
        QString program;
        // The folder it ends up as: <data>/arcade/retroarch/RetroArch-Win64.
        QString target;
    };
    // libretro's RetroArch and mamedev's MAME, for a 64-bit Windows.
    static Source retroarch(const QString &dataRoot);
    static Source mame(const QString &dataRoot);
    // The name of the 64-bit Windows build in mamedev's release of a
    // version: mame0276b_64bit.exe up to 0.280, mame0281b_x64.exe from 0.281.
    static QString mameWindowsFile(const QString &version);

    explicit ProgramInstaller(QObject *parent = nullptr);

    // Downloads and unpacks it; finished() follows. A download cut short
    // leaves what was there.
    void start(const Source &source);

private:
    void fetchArchive(const QUrl &url);
    void unpack();
    QString what() const override { return m_source.name; }
    // Hundreds of megabytes on the disk.
    void removeParts() override;
    // The archive as it downloads, and the folder it is unpacked in, both
    // beside the folder it ends up as.
    QString archivePart() const;
    QString unpackFolder() const;

    Source m_source;
};
