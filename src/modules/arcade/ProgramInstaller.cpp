#include "ProgramInstaller.h"
#include "Http.h"
#include "Libretro.h"
#include "Mame.h"
#include "../../util/SevenZip.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QPromise>
#include <QSysInfo>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// The versions every OSD/OS machine plays with (scripts/engines.env, which
// CMake passes in): the OSD/OS image's, Debian trixie's mame and retroarch,
// the lowest any distribution has, for the sets the games are made of and
// for playing together over the network. They rise with that floor.
constexpr const char *kRetroArchVersion = OSDOS_RETROARCH_VERSION;
constexpr const char *kMameVersion = OSDOS_MAME_VERSION;
// The SHA-256 each one's Windows download has (scripts/engines.env), checked
// before anything of it is unpacked: MAME's from its release's SHA256SUMS,
// RetroArch's empty, as libretro publishes none (not checked, and logged).
constexpr const char *kRetroArchSha256 = OSDOS_RETROARCH_WIN_SHA256;
constexpr const char *kMameSha256 = OSDOS_MAME_WIN_SHA256;

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

// The file's SHA-256 in lower-case hexadecimal, or "" when it can't be read.
QString sha256Of(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
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
    source.sha256 = QLatin1String(kRetroArchSha256);
    source.program = QStringLiteral("retroarch.exe");
    source.target = QFileInfo(libretro::downloadedRetroArch(dataRoot)).absolutePath();
    return source;
}

QString ProgramInstaller::mameWindowsFile(const QString &version) {
    // mamedev's release mameNNNN has the 64-bit Windows build as
    // mameNNNNb_64bit.exe up to 0.280 and as mameNNNNb_x64.exe from 0.281,
    // when an ARM build (mameNNNNb_arm64.exe) came beside it.
    const QString tag = QStringLiteral("mame") + QString(version).remove(QLatin1Char('.'));
    const bool x64 = QVersionNumber::fromString(version) >= QVersionNumber(0, 281);
    return tag + (x64 ? QStringLiteral("b_x64.exe") : QStringLiteral("b_64bit.exe"));
}

ProgramInstaller::Source ProgramInstaller::mame(const QString &dataRoot) {
    // mamedev's release of the version: its 64-bit Windows build
    // (mameWindowsFile()), a self-extracting archive with mame.exe at its
    // top, beside the sources (mameNNNNs.exe). 0.276 has no ARM build.
    Source source;
    source.name = QStringLiteral("MAME");
    source.version = QLatin1String(kMameVersion);
    source.available = windows64();
    const QString tag = QStringLiteral("mame") + QString(source.version).remove(QLatin1Char('.'));
    source.archive = QUrl(QStringLiteral("https://github.com/mamedev/mame/releases/download/%1/%2")
                              .arg(tag, mameWindowsFile(source.version)));
    source.sha256 = QLatin1String(kMameSha256);
    source.program = QStringLiteral("mame.exe");
    source.target = QFileInfo(mame::downloadedMame(dataRoot)).absolutePath();
    return source;
}

Installer::Installer(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_watcher(new QFutureWatcher<QString>(this))
{
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

void Installer::begin() {
    m_running = true;
    m_progress = 0;
    emit progressChanged();
}

void Installer::setProgress(int progress) {
    if (progress == m_progress)
        return;
    m_progress = progress;
    emit progressChanged();
}

void Installer::fail(const QString &message) {
    qWarning("[Arcade] %s's download failed: %s", qPrintable(what()), qPrintable(message));
    removeParts();
    m_running = false;
    emit finished(false, message);
}

ProgramInstaller::ProgramInstaller(QObject *parent)
    : Installer(parent)
{
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, &ProgramInstaller::setProgress);
}

QString ProgramInstaller::archivePart() const {
    return m_source.target + QStringLiteral(".7z.part");
}

QString ProgramInstaller::unpackFolder() const {
    return m_source.target + QStringLiteral(".part");
}

void ProgramInstaller::start(const Source &source) {
    if (running())
        return;
    if (!source.available) {
        emit finished(false, QStringLiteral("There is no %1 build for this machine (%2)")
                                 .arg(source.name, QSysInfo::currentCpuArchitecture()));
        return;
    }
    m_source = source;
    begin();
    QDir().mkpath(QFileInfo(archivePart()).absolutePath());
    fetchArchive(m_source.archive);
}

void ProgramInstaller::fetchArchive(const QUrl &url) {
    // Given up on once nothing has come for a minute, and once it is bigger
    // than the archive can be. One that failed goes (fail()).
    arcadehttp::downloadToFile(
        m_nam, url, archivePart(), kDownloadTimeoutMs, kArchiveMaxBytes, m_source.name + QStringLiteral("'s archive"),
        this,
        [this](qint64 received, qint64 total) {
            // No further than the download's share, should a server send
            // more than it said.
            if (total > 0)
                setProgress(int(kDownloadDone * qMin(received, total) / total));
        },
        [this](const QString &error) {
            if (!running())
                return;
            if (!error.isEmpty()) {
                fail(error);
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
    const QString expected = m_source.sha256.toLower();
    if (expected.isEmpty())
        qWarning("[Arcade] %s's download is not checked: no SHA-256 is known for %s",
                 qPrintable(m_source.name), qPrintable(m_source.archive.fileName()));
    m_watcher->setFuture(QtConcurrent::run([part, staging, target, program, expected](QPromise<QString> &promise) {
        promise.setProgressRange(0, 100);
        const auto cleanUp = [&] {
            QDir(staging).removeRecursively();
            QFile::remove(part);
        };
        const auto done = [&](const QString &error) {
            cleanUp();
            promise.addResult(error);
        };
        // The file that was meant, before anything of it is read as an
        // archive: HTTPS alone says only where it came from.
        if (!expected.isEmpty()) {
            const QString actual = sha256Of(part);
            if (actual.isEmpty()) {
                done(QStringLiteral("The download could not be read to check it"));
                return;
            }
            if (actual != expected) {
                done(QStringLiteral("The download is not the file expected: its SHA-256 is %1, not %2")
                         .arg(actual, expected));
                return;
            }
        }
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

void ProgramInstaller::removeParts() {
    QFile::remove(archivePart());
    QDir(unpackFolder()).removeRecursively();
}
