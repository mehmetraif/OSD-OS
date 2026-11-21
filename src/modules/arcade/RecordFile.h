#pragma once
#include "../../util/AtomicFile.h"
#include "../../util/FileText.h"
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <algorithm>
#include <utility>

// A record kept in a folder beside what it notes, as the games' pictures
// keep theirs in the snap folder (.retroachievements.json, .arcadedb.json):
// { <name>: <entry> } in a JSON file of its own, so that it goes where they
// go. Read when first needed in a folder (load()), marked as it changes
// (markChanged()), and written whole (flush()) off the GUI thread, from a
// copy, on the thread the files it notes are written by, after them. T
// reads and writes an entry: static T fromJson(const QJsonObject &), and
// QJsonObject toJson() const.
template <typename T>
class RecordFile {
public:
    explicit RecordFile(const QString &fileName) : m_fileName(fileName) {}

    // The folder whose record is held ("" for none).
    const QString &folder() const { return m_folder; }
    // folder's record read in place of the one held, which is let go of as
    // it is (flush() it first); none for no folder, or no file there.
    void load(const QString &folder) {
        m_folder = folder;
        m_entries.clear();
        if (folder.isEmpty())
            return;
        const QString path = folder + QLatin1Char('/') + m_fileName;
        QJsonObject kept;
        QString why;
        if (!filetext::readJsonObject(path, &kept, &why) && !why.isEmpty())
            qWarning("[Arcade] %s: %s", qPrintable(path), qPrintable(why));
        for (auto it = kept.constBegin(); it != kept.constEnd(); ++it)
            m_entries.insert(it.key(), T::fromJson(it.value().toObject()));
    }

    QHash<QString, T> &entries() { return m_entries; }
    const QHash<QString, T> &entries() const { return m_entries; }

    // It has changed: written at the next flush().
    void markChanged() { m_changed = true; }
    // Written on saves, should it have changed since the last time, its
    // names in order (a QJsonObject keeps its keys sorted: inserted in
    // order, none is moved).
    void flush(QThreadPool *saves) {
        if (!std::exchange(m_changed, false) || m_folder.isEmpty())
            return;
        const QString file = m_folder + QLatin1Char('/') + m_fileName;
        const QHash<QString, T> entries = m_entries;
        saves->start([file, entries] {
            QStringList names = entries.keys();
            std::sort(names.begin(), names.end());
            QJsonObject kept;
            for (const QString &name : std::as_const(names))
                kept.insert(name, entries.value(name).toJson());
            writeFileAtomically(file, QJsonDocument(kept).toJson(QJsonDocument::Compact));
        });
    }

private:
    QString m_fileName;
    QString m_folder;
    QHash<QString, T> m_entries;
    bool m_changed = false;
};
