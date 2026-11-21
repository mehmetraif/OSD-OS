#include "FbneoInstaller.h"
#include "Http.h"
#include "Libretro.h"
#include "Mame.h"
#include "../../util/DurableFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <cerrno>
#include <cstdio>
#include <system_error>
#ifdef OSDOS_MINIZIP
#include <minizip/unzip.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// What each download counts for in the progress: the core, its list of
// games, its licence.
constexpr int kShares[] = { 58, 40, 2 };
// A list of fewer games than this isn't FinalBurn Neo's arcade DAT (it has
// some 8,400).
constexpr int kLeastGames = 1000;
// A download given up on once nothing has come for this long, or it is
// bigger than this (the core is tens of megabytes, the list a few).
constexpr int kDownloadTimeoutMs = 60000;
constexpr qint64 kDownloadMaxBytes = 64 * 1024 * 1024;

// Where each download goes until all three are whole: beside where it ends
// up, with .part after its name.
QString partOf(const QString &path) {
    return path + QStringLiteral(".part");
}

// A download over the file before it, whole: "" once it is, else why not.
// rename() does that on POSIX; Windows' refuses a file that exists (so a
// second download would end here), and MoveFileEx replaces it.
QString putInPlace(const QString &from, const QString &to) {
#ifdef Q_OS_WIN
    const QString source = QDir::toNativeSeparators(from);
    const QString target = QDir::toNativeSeparators(to);
    if (MoveFileExW(reinterpret_cast<const wchar_t *>(source.utf16()), reinterpret_cast<const wchar_t *>(target.utf16()),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return QString();
    const std::string why = std::error_code(int(GetLastError()), std::system_category()).message();
#else
    if (std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0)
        return QString();
    const std::string why = std::error_code(errno, std::generic_category()).message();
#endif
    return QString::fromLocal8Bit(why.c_str());
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
#elif defined(Q_OS_WIN)
    if (arch == QLatin1String("x86_64"))
        source.core = QUrl(QStringLiteral("https://buildbot.libretro.com/nightly/windows/x86_64/latest/fbneo_libretro.dll.zip"));
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
    : Installer(parent), m_dataRoot(dataRoot)
{
    // What a download stopped by the app closing left: up to tens of
    // megabytes on the card, and nothing downloads yet.
    removeParts();
}

void FbneoInstaller::start(const Source &source) {
    if (running())
        return;
    if (source.core.isEmpty()) {
        emit finished(false, QStringLiteral("libretro builds no FinalBurn Neo for this machine (%1)")
                                 .arg(QSysInfo::currentCpuArchitecture()));
        return;
    }
    m_source = source;
    begin();
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
    int base = 0;
    for (int i = 0; i < index; ++i)
        base += kShares[i];
    // Given up on once nothing has come for a minute, and once it is bigger
    // than any of the three can be, or a server could fill the card. One
    // that failed goes, with those before it (fail()).
    arcadehttp::downloadToFile(
        m_nam, urls[index], partOf(targets[index]), kDownloadTimeoutMs, kDownloadMaxBytes,
        QStringLiteral("FinalBurn Neo's files"), this,
        [this, base, index](qint64 received, qint64 total) {
            setProgress(base + (total > 0 ? int(kShares[index] * received / total) : 0));
        },
        [this, index](const QString &error) {
            if (!running())
                return;
            if (!error.isEmpty()) {
                fail(error);
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
        // On the card before they go over the ones before (a power cut
        // after the rename would leave them short, and nothing to go back
        // to), then over them, whole, then the zip goes.
        for (const QString &path : { dat, license }) {
            const QString why = syncPath(partOf(path));
            if (!why.isEmpty()) {
                cleanUp();
                return QStringLiteral("%1 could not be written: %2").arg(partOf(path), why);
            }
        }
        for (const QString &path : { dat, license }) {
            const QString why = putInPlace(partOf(path), path);
            if (!why.isEmpty()) {
                cleanUp();
                return QStringLiteral("%1 could not be put in place: %2").arg(path, why);
            }
        }
        cleanUp();
        // The renames on the card too: the core's (QSaveFile wrote it whole)
        // and the list's and licence's.
        for (const QString &folder : { QFileInfo(core).absolutePath(), QFileInfo(dat).absolutePath() }) {
            const QString why = syncPath(folder, true);
            if (!why.isEmpty())
                qWarning("[Arcade] %s could not be synced: %s", qPrintable(folder), qPrintable(why));
        }
        return QString();
    }));
}

void FbneoInstaller::removeParts() {
    for (const QString &path : { libretro::downloadedCore(m_dataRoot) + QStringLiteral(".zip"),
                                 libretro::downloadedDat(m_dataRoot), libretro::downloadedLicense(m_dataRoot) })
        QFile::remove(partOf(path));
}
