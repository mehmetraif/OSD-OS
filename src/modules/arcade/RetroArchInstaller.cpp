#include "RetroArchInstaller.h"
#include "Http.h"
#include "Libretro.h"
#include "../../util/SevenZip.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QRegularExpression>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {

// The progress each step reaches: the list of versions read, the archive
// downloaded, then unpacked.
constexpr int kVersionsDone = 1;
constexpr int kDownloadDone = 90;
// A download given up on once nothing has come for this long, and the
// archive once it is bigger than this (some 200 MB as of 1.22) or the
// list of versions than this.
constexpr int kDownloadTimeoutMs = 60000;
constexpr qint64 kArchiveMaxBytes = 512 * 1024 * 1024;
constexpr qint64 kVersionsMaxBytes = 1024 * 1024;
// The archive unpacked is refused beyond this (some 540 MB as of 1.22), as
// is a solid block of it the SDK would have to hold in memory whole
// (380 MB, the bigger of its two, as of 1.22).
constexpr qint64 kUnpackedMaxBytes = qint64(3) * 1024 * 1024 * 1024;
constexpr qint64 kBlockMaxBytes = qint64(1536) * 1024 * 1024;

const QString kProgram = QStringLiteral("retroarch.exe");

} // namespace

RetroArchInstaller::Source RetroArchInstaller::defaultSource() {
    Source source;
    source.versions = QUrl(QStringLiteral("https://buildbot.libretro.com/stable/"));
    source.fallback = QStringLiteral("1.22.2");
#if defined(Q_OS_WIN)
    if (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
        source.archive = QStringLiteral("https://buildbot.libretro.com/stable/%1/windows/x86_64/RetroArch.7z");
#endif
    return source;
}

QString RetroArchInstaller::newestVersion(const QByteArray &page) {
    // The server's index links each version's folder: href="/stable/1.22.2/"
    // or href="1.22.2/".
    static const QRegularExpression link(QStringLiteral("href=\"(?:[^\"]*/)?(\\d+)\\.(\\d+)\\.(\\d+)/\""));
    QString newest;
    QList<int> newestParts;
    auto match = link.globalMatch(QString::fromUtf8(page));
    while (match.hasNext()) {
        const QRegularExpressionMatch m = match.next();
        const QList<int> parts{ m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt() };
        if (newest.isEmpty() || std::lexicographical_compare(newestParts.cbegin(), newestParts.cend(), parts.cbegin(), parts.cend())) {
            newest = QStringLiteral("%1.%2.%3").arg(parts[0]).arg(parts[1]).arg(parts[2]);
            newestParts = parts;
        }
    }
    return newest;
}

RetroArchInstaller::RetroArchInstaller(const QString &dataRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_nam(new QNetworkAccessManager(this)),
      m_watcher(new QFutureWatcher<QString>(this))
{
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, &RetroArchInstaller::setProgress);
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

QString RetroArchInstaller::archivePart() const {
    return QFileInfo(libretro::downloadedRetroArch(m_dataRoot)).absolutePath() + QStringLiteral(".7z.part");
}

QString RetroArchInstaller::unpackFolder() const {
    return QFileInfo(libretro::downloadedRetroArch(m_dataRoot)).absolutePath() + QStringLiteral(".part");
}

void RetroArchInstaller::start(const Source &source) {
    if (m_running)
        return;
    if (source.archive.isEmpty()) {
        emit finished(false, QStringLiteral("libretro builds no RetroArch for this machine (%1)")
                                 .arg(QSysInfo::currentCpuArchitecture()));
        return;
    }
    m_source = source;
    m_running = true;
    m_progress = 0;
    emit progressChanged();
    QDir().mkpath(QFileInfo(archivePart()).absolutePath());
    if (m_source.versions.isEmpty())
        fetchArchive(m_source.fallback);
    else
        fetchVersions();
}

void RetroArchInstaller::fetchVersions() {
    m_reply = m_nam->get(arcadehttp::requestFor(m_source.versions, kDownloadTimeoutMs));
    arcadehttp::limitReply(m_reply, kVersionsMaxBytes);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        reply->deleteLater();
        if (!m_running)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A list that can't be had, or names no version, leaves the one
        // known: the archive's download says whether it is still there.
        const QString newest = reply->error() == QNetworkReply::NoError && status == 200
                                   ? newestVersion(reply->readAll()) : QString();
        setProgress(kVersionsDone);
        fetchArchive(newest.isEmpty() ? m_source.fallback : newest);
    });
}

void RetroArchInstaller::fetchArchive(const QString &version) {
    const QUrl url(m_source.archive.arg(version));
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
        if (total > 0)
            setProgress(kVersionsDone + int((kDownloadDone - kVersionsDone) * received / total));
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
            fail(arcadehttp::tooBig(reply, kArchiveMaxBytes) ? QStringLiteral("%1 sent more than RetroArch's archive can be").arg(reply->url().host())
                 : status != 0 && status != 200              ? QStringLiteral("%1 answered %2").arg(reply->url().host()).arg(status)
                 : reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                            : file->errorString());
            return;
        }
        setProgress(kDownloadDone);
        unpack();
    });
}

void RetroArchInstaller::unpack() {
    // Off the GUI thread: half a gigabyte to write, and a block of it held
    // in memory meanwhile. Into a folder beside the one it ends up as, then
    // the folder retroarch.exe came out in moved into place (the archive
    // holds RetroArch-Win64; one with the files at its top would be moved
    // whole), over the one downloaded before, which goes once it is.
    const QString part = archivePart();
    const QString staging = unpackFolder();
    const QString target = QFileInfo(libretro::downloadedRetroArch(m_dataRoot)).absolutePath();
    m_watcher->setFuture(QtConcurrent::run([part, staging, target](QPromise<QString> &promise) {
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
        // retroarch.exe at the archive's top, or in a folder there.
        QString programDir;
        bool found = false;
        for (const QString &name : names) {
            const QString relative = sevenzip::safeRelativePath(name);
            if (name.endsWith(QLatin1Char('/')) || relative.isEmpty())
                continue;
            const QStringList parts = relative.split(QLatin1Char('/'));
            if (parts.size() <= 2 && parts.last().compare(kProgram, Qt::CaseInsensitive) == 0) {
                programDir = parts.size() == 2 ? parts.first() : QString();
                found = true;
                break;
            }
        }
        if (!found) {
            done(QStringLiteral("The download has no %1").arg(kProgram));
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

void RetroArchInstaller::setProgress(int progress) {
    if (progress == m_progress)
        return;
    m_progress = progress;
    emit progressChanged();
}

void RetroArchInstaller::fail(const QString &message) {
    qWarning("[Arcade] RetroArch's download failed: %s", qPrintable(message));
    // What came goes: hundreds of megabytes on the disk. What was installed
    // stays as it was.
    QFile::remove(archivePart());
    QDir(unpackFolder()).removeRecursively();
    m_running = false;
    emit finished(false, message);
}
