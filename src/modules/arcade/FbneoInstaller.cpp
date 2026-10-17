#include "FbneoInstaller.h"
#include "Libretro.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>
#ifdef OSDOS_MINIZIP
#include <minizip/unzip.h>
#endif

namespace {

// What each download counts for in the progress: the core, its list of
// games, its licence.
constexpr int kShares[] = { 58, 40, 2 };
// A list of fewer games than this isn't FinalBurn Neo's arcade DAT (it has
// some 9,000).
constexpr int kLeastGames = 1000;

// Where each download goes until all three are whole: beside where it ends
// up, with .part after its name.
QString partOf(const QString &path) {
    return path + QStringLiteral(".part");
}

// The core out of its zip, into place: "" once it is, else why not.
QString unpackCore(const QString &zip, const QString &core) {
#ifdef OSDOS_MINIZIP
    unzFile archive = unzOpen64(QFile::encodeName(zip).constData());
    if (!archive)
        return QStringLiteral("The core's download isn't a zip");
    const QByteArray entry = QFileInfo(core).fileName().toUtf8();
    if (unzLocateFile(archive, entry.constData(), 1) != UNZ_OK || unzOpenCurrentFile(archive) != UNZ_OK) {
        unzClose(archive);
        return QStringLiteral("The download has no ") + QString::fromUtf8(entry);
    }
    QSaveFile out(core);
    bool written = out.open(QIODevice::WriteOnly);
    char buffer[65536];
    int read = 0;
    while (written && (read = unzReadCurrentFile(archive, buffer, sizeof buffer)) > 0)
        written = out.write(buffer, read) == read;
    const bool whole = read == 0 && unzCloseCurrentFile(archive) == UNZ_OK;
    unzClose(archive);
    if (!whole)
        return QStringLiteral("The core's zip is damaged");
    if (!written || !out.commit())
        return QStringLiteral("The core could not be written: ") + out.errorString();
    return QString();
#else
    Q_UNUSED(zip)
    Q_UNUSED(core)
    return QStringLiteral("This build of OSD/OS can't unpack the core (no minizip): install it by hand");
#endif
}

} // namespace

FbneoInstaller::Source FbneoInstaller::defaultSource() {
    Source source;
    const QString arch = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_MACOS)
    if (arch == QLatin1String("arm64") || arch == QLatin1String("x86_64"))
        source.core = QUrl(QStringLiteral("https://buildbot.libretro.com/nightly/apple/osx/%1/latest/fbneo_libretro.dylib.zip")
                               .arg(arch));
#elif defined(Q_OS_LINUX)
    const QString machine = arch == QLatin1String("arm64") ? QStringLiteral("aarch64") : arch;
    if (machine == QLatin1String("aarch64") || machine == QLatin1String("x86_64"))
        source.core = QUrl(QStringLiteral("https://buildbot.libretro.com/nightly/linux/%1/latest/fbneo_libretro.so.zip")
                               .arg(machine));
#endif
    source.dat = QUrl(QStringLiteral("https://raw.githubusercontent.com/libretro/FBNeo/master/dats/"
                                     "FinalBurn%20Neo%20(ClrMame%20Pro%20XML%2C%20Arcade%20only).dat"));
    source.license = QUrl(QStringLiteral("https://raw.githubusercontent.com/libretro/FBNeo/master/src/license.txt"));
    return source;
}

FbneoInstaller::FbneoInstaller(const QString &dataRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_nam(new QNetworkAccessManager(this)),
      m_watcher(new QFutureWatcher<QString>(this))
{
    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        const QString error = m_watcher->result();
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        m_running = false;
        m_progress = 100;
        emit progressChanged();
        emit finished(true, QString());
    });
}

void FbneoInstaller::start(const Source &source) {
    if (m_running)
        return;
    if (source.core.isEmpty()) {
        emit finished(false, QStringLiteral("libretro builds no FinalBurn Neo for this machine (%1)")
                                 .arg(QSysInfo::currentCpuArchitecture()));
        return;
    }
    m_source = source;
    m_running = true;
    m_progress = 0;
    emit progressChanged();
    QDir().mkpath(QFileInfo(libretro::downloadedCore(m_dataRoot)).absolutePath());
    fetch(0);
}

void FbneoInstaller::fetch(int index) {
    const QUrl urls[] = { m_source.core, m_source.dat, m_source.license };
    const QString targets[] = { libretro::downloadedCore(m_dataRoot) + QStringLiteral(".zip"),
                                libretro::downloadedDat(m_dataRoot), libretro::downloadedLicense(m_dataRoot) };
    if (index == 3) {
        unpack();
        return;
    }
    QNetworkRequest request(urls[index]);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion()));
    m_reply = m_nam->get(request);
    auto *file = new QFile(partOf(targets[index]), m_reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = file->errorString();
        m_reply->abort();
        fail(QStringLiteral("%1 could not be written: %2").arg(file->fileName(), error));
        return;
    }
    int base = 0;
    for (int i = 0; i < index; ++i)
        base += kShares[i];
    connect(m_reply, &QNetworkReply::readyRead, file, [this, file] {
        if (file->write(m_reply->readAll()) < 0)
            m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, base, index](qint64 received, qint64 total) {
        const int progress = base + (total > 0 ? int(kShares[index] * received / total) : 0);
        if (progress != m_progress) {
            m_progress = progress;
            emit progressChanged();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, file, index] {
        QNetworkReply *reply = std::exchange(m_reply, nullptr);
        reply->deleteLater();
        if (!m_running)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        file->write(reply->readAll());
        const bool flushed = file->flush();
        file->close();
        if (reply->error() != QNetworkReply::NoError || status != 200 || !flushed) {
            QFile::remove(file->fileName());
            // The server's answer first (Qt takes a 404 for an error too).
            fail(status != 0 && status != 200                 ? QStringLiteral("%1 answered %2").arg(reply->url().host()).arg(status)
                 : reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                            : file->errorString());
            return;
        }
        fetch(index + 1);
    });
}

void FbneoInstaller::unpack() {
    // Off the GUI thread: the core is tens of megabytes unpacked, the list
    // as many to read.
    const QString core = libretro::downloadedCore(m_dataRoot);
    const QString dat = libretro::downloadedDat(m_dataRoot);
    const QString license = libretro::downloadedLicense(m_dataRoot);
    m_watcher->setFuture(QtConcurrent::run([core, dat, license]() -> QString {
        const auto cleanUp = [&] {
            for (const QString &path : { partOf(core + QStringLiteral(".zip")), partOf(dat), partOf(license) })
                QFile::remove(path);
        };
        if (libretro::readDat(partOf(dat)).size() < kLeastGames) {
            cleanUp();
            return QStringLiteral("The list of games downloaded isn't FinalBurn Neo's");
        }
        const QString error = unpackCore(partOf(core + QStringLiteral(".zip")), core);
        if (!error.isEmpty()) {
            cleanUp();
            return error;
        }
        // Over the ones before (rename() replaces a file whole), then the
        // zip goes.
        for (const QString &path : { dat, license }) {
            if (std::rename(QFile::encodeName(partOf(path)).constData(), QFile::encodeName(path).constData()) != 0) {
                cleanUp();
                return QStringLiteral("%1 could not be put in place").arg(path);
            }
        }
        cleanUp();
        return QString();
    }));
}

void FbneoInstaller::fail(const QString &message) {
    qWarning("[Arcade] FinalBurn Neo's download failed: %s", qPrintable(message));
    m_running = false;
    emit finished(false, message);
}
