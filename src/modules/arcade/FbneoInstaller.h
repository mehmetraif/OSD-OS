#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Settings → Arcade → FinalBurn Neo's download, for an install the OSD/OS
// image didn't bring it with: the core from libretro's build server, and its
// list of games and its licence from FinalBurn Neo's own sources, each put
// in place (libretro::downloadedCore() and the rest) only once it is whole:
// the core taken out of its zip, the list read to be one. Unpacking needs
// minizip (OSDOS_MINIZIP), as the High Scores module does.
class FbneoInstaller : public QObject {
    Q_OBJECT
public:
    struct Source {
        QUrl core;      // a zip with the core in it
        QUrl dat;
        QUrl license;
    };
    // libretro's nightly build of the core for this machine (Linux on arm64
    // or x86_64, a Mac on either), FinalBurn Neo's arcade DAT and licence
    // from its sources; no core URL for a machine libretro builds none for.
    static Source defaultSource();

    explicit FbneoInstaller(const QString &dataRoot, QObject *parent = nullptr);

    // Downloads the three; finished() follows. Nothing is changed before all
    // three are whole: a download cut short leaves what was there.
    void start(const Source &source);
    bool running() const { return m_running; }
    int  progress() const { return m_progress; }   // 0 to 100

signals:
    void progressChanged();
    void finished(bool ok, const QString &message);

private:
    void fetch(int index);
    void unpack();
    void fail(const QString &message);

    QString m_dataRoot;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFutureWatcher<QString> *m_watcher = nullptr;
    Source m_source;
    int  m_progress = 0;
    bool m_running = false;
};
