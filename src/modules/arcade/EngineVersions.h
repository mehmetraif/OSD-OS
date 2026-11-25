#pragma once
#include "../../util/AtomicFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QThreadPool>
#include <optional>

// The engines' versions online play matches rooms against (RetroArch's
// `--version`, the FBNeo cores' through `osdos --core-info`), kept from one
// run to the next in the arcade folder's engine-versions.json, so that a
// run asks none of them again until a file changes:
//
//     { "version": 1, "retroarch": { "stamp": "<path>:<size>:<time>", "version": "1.21.0" },
//       "core": { ... }, "lanCore": { ... } }
//
// Each by its role, with the stamp of the file it was read from
// (ArcadeBackend's fileStamp()). A file that can't be read, of another
// version, or whose stamp for a role isn't the file's now, counts for none:
// that version is asked again. Written whole (writeFileAtomically()), off
// the app's thread, one write at a time.
namespace engineversions {

constexpr int kVersion = 1;

inline QString file(const QString &arcadeHome) {
    return arcadeHome + QStringLiteral("/engine-versions.json");
}

inline QJsonObject read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    return o.value(QLatin1String("version")).toInt() == kVersion ? o : QJsonObject();
}

// The version kept for `role`, read from the file of this stamp; none if
// there isn't one.
inline std::optional<QString> kept(const QString &path, const QString &role, const QString &stamp) {
    if (stamp.isEmpty())
        return std::nullopt;
    const QJsonObject entry = read(path).value(role).toObject();
    const QString version = entry.value(QLatin1String("version")).toString();
    if (entry.value(QLatin1String("stamp")).toString() != stamp || version.isEmpty())
        return std::nullopt;
    return version;
}

inline QMutex &writing() {
    static QMutex mutex;
    return mutex;
}

// `version`, read from the file of `stamp`, kept for `role` in place of any
// other; the others as they are.
inline void keep(const QString &path, const QString &role, const QString &stamp, const QString &version) {
    if (stamp.isEmpty() || version.isEmpty())
        return;
    QThreadPool::globalInstance()->start([path, role, stamp, version] {
        const QMutexLocker one(&writing());
        QJsonObject o = read(path);
        o.insert(QStringLiteral("version"), kVersion);
        o.insert(role, QJsonObject{ { QStringLiteral("stamp"), stamp }, { QStringLiteral("version"), version } });
        QDir().mkpath(QFileInfo(path).absolutePath());
        writeFileAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Indented));
    });
}

} // namespace engineversions
