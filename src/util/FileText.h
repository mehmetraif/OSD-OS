#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

// The small files the app reads whole: a value the system keeps in a file of
// its own (sysfs, /proc, the boot partition's), and the app's own JSON
// (config.json, a module's manifest, a theme's theme.json, a marker).
namespace filetext {

// A file's bytes, at most maxBytes of them (all of them for -1): empty when
// there is no such file or it can't be read. As they are: sysfs's and
// /proc's values end with a newline, which a caller trims.
inline QByteArray readText(const QString &path, qint64 maxBytes = -1) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return maxBytes < 0 ? file.readAll() : file.read(maxBytes);
}

// A file's JSON read as an object. False when there is no such file, or it
// isn't a JSON object: `why` then says what is wrong with one that is there
// (empty for one that isn't, or is empty), for the log. A caller that writes
// the file back writes nothing over one with a `why` (config.json, a stray
// comma from editing it by hand): every value in it would be lost.
inline bool readJsonObject(const QString &path, QJsonObject *out, QString *why = nullptr) {
    if (why)
        why->clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    if (raw.trimmed().isEmpty())
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (why)
            *why = err.error != QJsonParseError::NoError ? err.errorString()
                                                         : QStringLiteral("not a JSON object");
        return false;
    }
    *out = doc.object();
    return true;
}

} // namespace filetext
