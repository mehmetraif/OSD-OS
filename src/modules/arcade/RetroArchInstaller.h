#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Settings → Arcade → FinalBurn Neo's download of RetroArch itself, on
// Windows, where no package manager brings it: libretro's latest stable
// build for 64-bit Windows, a 7z archive of some 200 MB from its build
// server, unpacked into <data>/arcade/retroarch as RetroArch-Win64
// (libretro::downloadedRetroArch()), where the module looks for it. Nothing
// is in place before all of it is: the archive is downloaded as a .part
// file and unpacked beside it (SevenZip), and the folder moved into place
// once it is whole, over one downloaded before.
class RetroArchInstaller : public QObject {
    Q_OBJECT
public:
    struct Source {
        // The server's list of its stable versions, a page with a link to
        // each ("1.22.2/"); none to take fallback as it is.
        QUrl versions;
        // The archive's URL, with %1 for the version; "" for a machine the
        // server builds nothing for.
        QString archive;
        // The version taken when the list can't be read or names none.
        QString fallback;
    };
    // libretro's build server's, for a 64-bit Windows; no archive elsewhere.
    static Source defaultSource();
    // The newest version a page of the server's links to (1.22.2 over
    // 1.9.1), "" for none.
    static QString newestVersion(const QByteArray &page);

    explicit RetroArchInstaller(const QString &dataRoot, QObject *parent = nullptr);

    // Downloads and unpacks it; finished() follows. A download cut short
    // leaves what was there.
    void start(const Source &source);
    bool running() const { return m_running; }
    int  progress() const { return m_progress; }   // 0 to 100

signals:
    void progressChanged();
    void finished(bool ok, const QString &message);

private:
    void fetchVersions();
    void fetchArchive(const QString &version);
    void unpack();
    void setProgress(int progress);
    void fail(const QString &message);
    // The archive as it downloads, and the folder it is unpacked in, both
    // beside the folder it ends up as.
    QString archivePart() const;
    QString unpackFolder() const;

    QString m_dataRoot;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFutureWatcher<QString> *m_watcher = nullptr;
    Source m_source;
    int  m_progress = 0;
    bool m_running = false;
};
