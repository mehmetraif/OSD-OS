#include "ProgramInstaller.h"
#include "Http.h"
#include "Libretro.h"
#include "Mame.h"
#include "../../util/SevenZip.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace {

// The versions the OSD/OS image has, Debian trixie's mame and retroarch
// (os/stage-osdos/08-arcade): the same on every machine, for the sets the
// games are made of and for playing together over the network.
constexpr const char *kRetroArchVersion = "1.20.0";
constexpr const char *kMameVersion = "0.276";

// The progress the download reaches, the unpacking after it.
constexpr int kDownloadDone = 90;
// A download given up on once nothing has come for this long, and the
// archive once it is bigger than this (RetroArch's some 200 MB, MAME's 100).
constexpr int kDownloadTimeoutMs = 60000;
constexpr qint64 kArchiveMaxBytes = 512 * 1024 * 1024;
// The archive unpacked is refused beyond this (some 540 MB for RetroArch,
// 500 for MAME), as is a solid block of it the SDK would have to hold in
// memory whole (380 MB, the bigger of RetroArch's two).
constexpr qint64 kUnpackedMaxBytes = qint64(3) * 1024 * 1024 * 1024;
constexpr qint64 kBlockMaxBytes = qint64(1536) * 1024 * 1024;

// Whether this is a machine the two are built for.
bool windows64() {
#if defined(Q_OS_WIN)
    return QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64");
#else
    return false;
#endif
}

} // namespace

ProgramInstaller::Source ProgramInstaller::retroarch(const QString &dataRoot) {
    // The build server keeps every stable version's Windows build:
    // stable/<version>/windows/x86_64/RetroArch.7z, RetroArch-Win64 in it.
    Source source;
    source.name = QStringLiteral("RetroArch");
    source.version = QLatin1String(kRetroArchVersion);
    source.available = windows64();
    source.archive = QUrl(QStringLiteral("https://buildbot.libretro.com/stable/%1/windows/x86_64/RetroArch.7z")
                              .arg(source.version));
    source.program = QStringLiteral("retroarch.exe");
    source.target = QFileInfo(libretro::downloadedRetroArch(dataRoot)).absolutePath();
    return source;
}

ProgramInstaller::Source ProgramInstaller::mame(const QString &dataRoot) {
    // mamedev's release mame0276 has mame0276b_64bit.exe, the 64-bit Windows
    // build, a self-extracting archive with mame.exe at its top, beside the
    // sources (mame0276s.exe) and the ARM build.
    Source source;
    source.name = QStringLiteral("MAME");
    source.version = QLatin1String(kMameVersion);
    source.available = windows64();
    const QString tag = QStringLiteral("mame") + QString(source.version).remove(QLatin1Char('.'));
    source.archive = QUrl(QStringLiteral("https://github.com/mamedev/mame/releases/download/%1/%1b_64bit.exe").arg(tag));
    source.program = QStringLiteral("mame.exe");
    source.target = QFileInfo(mame::downloadedMame(dataRoot)).absolutePath();
    return source;
}

ProgramInstaller::ProgramInstaller(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_watcher(new QFutureWatcher<QString>(this))
{
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, &ProgramInstaller::setProgress);
    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        const QString error = m_watcher->future().resultCount() > 0 ? m_watcher->result() : QString();
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        m_running = false;
        setProgress(100);
        emit finished(true, QString());
    });
}

QString ProgramInstaller::archivePart() const {
    return m_source.target + QStringLiteral(".7z.part");
}

QString ProgramInstaller::unpackFolder() const {
    return m_source.target + QStringLiteral(".part");
}

void ProgramInstaller::start(const Source &source) {
    if (m_running)
        return;
    if (!source.available) {
        emit finished(false, QStringLiteral("There is no %1 build for this machine (%2)")
                                 .arg(source.name, QSysInfo::currentCpuArchitecture()));
        return;
    }
    m_source = source;
    m_running = true;
    m_progress = 0;
    emit progressChanged();
    QDir().mkpath(QFileInfo(archivePart()).absolutePath());
    fetchArchive(m_source.archive);
}

void ProgramInstaller::fetchArchive(const QUrl &url) {
    // Given up on once nothing has come for a minute (a Wi-Fi that dropped:
    // TCP alone takes a quarter of an hour to say so), and once it is bigger
    // than the archive can be.
    m_reply = m_nam->get(arcadehttp::requestFor(url, kDownloadTimeoutMs));
    arcadehttp::limitReply(m_reply, kArchiveMaxBytes);
    auto *file = new QFile(archivePart(), m_reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = QStringLiteral("%1 could not be written: %2").arg(file->fileName(), file->errorString());
        // Nothing listens to it yet: it goes here, the file with it.
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        reply->abort();
        reply->deleteLater();
        fail(error);
        return;
    }
    connect(m_reply, &QNetworkReply::readyRead, file, [this, file] {
        if (file->write(m_reply->readAll()) < 0)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        // No further than the download's share, should a server send more
        // than it said.
        if (total > 0)
            setProgress(int(kDownloadDone * qMin(received, total) / total));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, file] {
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        reply->deleteLater();
        if (!m_running)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        file->write(reply->readAll());
        const bool flushed = file->flush();
        file->close();
        if (reply->error() != QNetworkReply::NoError || status != 200 || !flushed) {
            // The server's answer first (Qt takes a 404 for an error too).
            fail(arcadehttp::tooBig(reply, kArchiveMaxBytes) ? QStringLiteral("%1 sent more than %2's archive can be").arg(reply->url().host(), m_source.name)
                 : status != 0 && status != 200              ? QStringLiteral("%1 answered %2").arg(reply->url().host()).arg(status)
                 : reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                            : file->errorString());
            return;
        }
        setProgress(kDownloadDone);
        unpack();
    });
}

void ProgramInstaller::unpack() {
    // Off the GUI thread: hundreds of megabytes to write, and a block of
    // the archive held in memory meanwhile. Into a folder beside the one it
    // ends up as, then the folder the program came out in moved into place
    // (RetroArch's archive holds RetroArch-Win64; MAME's has its files at
    // the top, so the whole is moved), over the one downloaded before, which
    // goes once it is.
    const QString part = archivePart();
    const QString staging = unpackFolder();
    const QString target = m_source.target;
    const QString program = m_source.program;
    m_watcher->setFuture(QtConcurrent::run([part, staging, target, program](QPromise<QString> &promise) {
        promise.setProgressRange(0, 100);
        const auto cleanUp = [&] {
            QDir(staging).removeRecursively();
            QFile::remove(part);
        };
        const auto done = [&](const QString &error) {
            cleanUp();
            promise.addResult(error);
        };
        QString error;
        const QStringList names = sevenzip::list(part, &error);
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        // The program at the archive's top, or in a folder there.
        QString programDir;
        bool found = false;
        for (const QString &name : names) {
            const QString relative = sevenzip::safeRelativePath(name);
            if (name.endsWith(QLatin1Char('/')) || relative.isEmpty())
                continue;
            const QStringList parts = relative.split(QLatin1Char('/'));
            if (parts.size() <= 2 && parts.last().compare(program, Qt::CaseInsensitive) == 0) {
                programDir = parts.size() == 2 ? parts.first() : QString();
                found = true;
                break;
            }
        }
        if (!found) {
            done(QStringLiteral("The download has no %1").arg(program));
            return;
        }
        QDir(staging).removeRecursively();
        error = sevenzip::extract(part, staging, kUnpackedMaxBytes, kBlockMaxBytes, [&promise](qint64 written, qint64 total) {
            if (total > 0)
                promise.setProgressValue(kDownloadDone + int((100 - kDownloadDone) * written / total));
            return true;
        });
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        // Into place: the one before set aside first, so that a failure
        // here leaves one or the other whole, and removed once the new one
        // is in.
        const QString from = programDir.isEmpty() ? staging : staging + QLatin1Char('/') + programDir;
        const QString before = target + QStringLiteral(".old");
        QDir(before).removeRecursively();
        if (QFileInfo::exists(target) && !QDir().rename(target, before)) {
            done(QStringLiteral("%1 could not be set aside for the new one").arg(target));
            return;
        }
        if (!QDir().rename(from, target)) {
            QDir().rename(before, target);
            done(QStringLiteral("%1 could not be put in place").arg(target));
            return;
        }
        QDir(before).removeRecursively();
        done(QString());
    }));
}

void ProgramInstaller::setProgress(int progress) {
    if (progress == m_progress)
        return;
    m_progress = progress;
    emit progressChanged();
}

void ProgramInstaller::fail(const QString &message) {
    qWarning("[Arcade] %s's download failed: %s", qPrintable(m_source.name), qPrintable(message));
    // What came goes: hundreds of megabytes on the disk. What was installed
    // stays as it was.
    QFile::remove(archivePart());
    QDir(unpackFolder()).removeRecursively();
    m_running = false;
    emit finished(false, message);
}
