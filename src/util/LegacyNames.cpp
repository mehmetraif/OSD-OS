#include "LegacyNames.h"
#include "AtomicFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

namespace {

const QByteArray kPrefix = QByteArrayLiteral("OSDOS_");
const QByteArray kOldPrefix = QByteArrayLiteral("MP240_");
const QString kModulePrefix = QStringLiteral("com.osdos.");
const QString kOldModulePrefix = QStringLiteral("com.240mp.");
const QString kOldDataFolder = QStringLiteral("240-MP");

QString renamed(const QString &s) {
    return s.startsWith(kOldModulePrefix) ? kModulePrefix + s.mid(kOldModulePrefix.size()) : s;
}

QJsonValue renameIds(const QJsonValue &value) {
    if (value.isObject()) {
        const QJsonObject in = value.toObject();
        QJsonObject out;
        for (auto it = in.begin(); it != in.end(); ++it)
            out.insert(renamed(it.key()), renameIds(it.value()));
        return out;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : value.toArray())
            out.append(renameIds(item));
        return out;
    }
    if (value.isString())
        return renamed(value.toString());
    return value;
}

void migrateFile(const QString &path) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = in.readAll();
    in.close();
    if (!raw.contains(kOldModulePrefix.toUtf8()))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError)
        return;
    const QJsonDocument out = doc.isArray() ? QJsonDocument(renameIds(doc.array()).toArray())
                                            : QJsonDocument(renameIds(doc.object()).toObject());
    if (writeFileAtomically(path, out.toJson(QJsonDocument::Indented)))
        qInfo("[legacy] module ids renamed in %s", qPrintable(path));
}

void migrateDir(const QString &dir) {
    const QDir d(dir);
    for (const QString &name : d.entryList({QStringLiteral("*.json")}, QDir::Files))
        migrateFile(d.filePath(name));
}

} // namespace

namespace legacy {

QString env(const char *name, const QString &fallback) {
    const QByteArray now = kPrefix + name;
    if (qEnvironmentVariableIsSet(now.constData()))
        return qEnvironmentVariable(now.constData());
    const QByteArray old = kOldPrefix + name;
    if (qEnvironmentVariableIsSet(old.constData()))
        return qEnvironmentVariable(old.constData());
    return fallback;
}

bool envIsSet(const char *name) {
    return qEnvironmentVariableIsSet((kPrefix + name).constData())
        || qEnvironmentVariableIsSet((kOldPrefix + name).constData());
}

int envInt(const char *name) {
    return env(name).toInt();
}

QString migrateDataFolder(const QString &dataRoot) {
    const QString old = QFileInfo(dataRoot).dir().filePath(kOldDataFolder);
    if (QDir::cleanPath(old) == QDir::cleanPath(dataRoot) || !QFileInfo(old).isDir())
        return dataRoot;
    QDir now(dataRoot);
    if (now.exists() && !now.isEmpty())
        return dataRoot;
    // An empty folder in the way (made by an earlier start) gives way to it.
    if (now.exists())
        QDir().rmdir(dataRoot);
    if (QDir().rename(old, dataRoot)) {
        qInfo("[legacy] moved the 240-MP data folder %s to %s", qPrintable(old), qPrintable(dataRoot));
        return dataRoot;
    }
    qWarning("[legacy] could not move %s to %s; using it where it is",
             qPrintable(old), qPrintable(dataRoot));
    return old;
}

void migrateModuleIds(const QString &dataRoot) {
    migrateDir(dataRoot);
    migrateDir(QDir(dataRoot).filePath(QStringLiteral("nfc_tags")));
}

} // namespace legacy
