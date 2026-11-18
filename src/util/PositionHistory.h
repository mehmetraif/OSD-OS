#pragma once

#include "AtomicFile.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>

// Where a module's videos were stopped, to start them there again: Local
// Files', the NFC Reader's and YouTube's resume points, each in a JSON file of
// its own in the data folder (local_files_history.json, …), a map from what
// was played (a path, a URL, a video's id) to the module's own entry for it
// ({ pos, plPos }; YouTube's with its title too), and when it was saved
// (lastPlayed, milliseconds since the epoch). Read afresh at each call, and
// written whole (writeFileAtomically()) at each change.
//
// With a limit, the file keeps that many entries at most: once a save goes
// over, the entries played longest ago go, an entry without a lastPlayed
// (saved before there was one) first, and never the one just saved.
class PositionHistory {
public:
    // `limit` 0 for none.
    explicit PositionHistory(const QString &path, int limit = 0) : m_path(path), m_limit(limit) {}

    QString path() const { return m_path; }

    // Every entry, by what was played; none for a file that isn't there or
    // isn't JSON.
    QVariantMap entries() const {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    }

    // One entry, as it was saved; invalid where there is none.
    QVariant entry(const QString &key) const { return entries().value(key); }

    // `entry`, played now, in place of the one `key` had, the oldest dropped
    // past the limit.
    void save(const QString &key, QVariantMap entry) {
        entry[QStringLiteral("lastPlayed")] = QDateTime::currentMSecsSinceEpoch();
        QVariantMap history = entries();
        history[key] = entry;
        if (m_limit > 0 && history.size() > m_limit) {
            QStringList others = history.keys();
            others.removeOne(key);
            std::sort(others.begin(), others.end(), [&history](const QString &a, const QString &b) {
                return history.value(a).toMap().value(QStringLiteral("lastPlayed")).toLongLong()
                     > history.value(b).toMap().value(QStringLiteral("lastPlayed")).toLongLong();
            });
            for (qsizetype i = m_limit - 1; i < others.size(); ++i)
                history.remove(others.at(i));
        }
        write(history);
    }

    // `key`'s entry gone.
    void clear(const QString &key) {
        QVariantMap history = entries();
        history.remove(key);
        write(history);
    }

    // Every entry gone, and the file with them.
    void clearAll() { QFile::remove(m_path); }

private:
    void write(const QVariantMap &history) {
        writeFileAtomically(m_path, QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
    }

    QString m_path;
    int     m_limit = 0;
};
