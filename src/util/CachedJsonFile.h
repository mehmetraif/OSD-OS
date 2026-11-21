#pragma once

#include "AtomicFile.h"
#include "FileText.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

// A JSON file's object as last read or written (AppCore's config.json and
// lists.json), good for as long as the file keeps its time and size: such a
// file is asked for by the dozen (every setting as a video starts, a list at
// every page of the Arcade screen's games) and is a few dozen kilobytes to
// parse each time. A write of its own is kept as what the next read would
// find; an edit by hand changes the time or the size, and is read anew.
class CachedJsonFile {
public:
    explicit CachedJsonFile(const QString &path) : m_path(path) {}

    QString path() const { return m_path; }

    // The file's object. False when there is none to be had: no file, an
    // empty one, or one that isn't a JSON object, which is `broken` (there,
    // and not to be written over: every value in it would be lost).
    bool read(QJsonObject *object, bool *broken = nullptr) const {
        const QFileInfo info(m_path);
        if (m_valid && info.exists() && info.lastModified() == m_modified && info.size() == m_size) {
            if (broken)
                *broken = false;
            *object = m_object;
            return true;
        }
        QString why;
        const bool ok = filetext::readJsonObject(m_path, object, &why);
        if (broken)
            *broken = !ok && !why.isEmpty();
        m_valid = ok && info.exists();
        if (m_valid)
            remember(*object, info);
        return ok;
    }

    // Written whole, atomically (writeFileAtomically()); false when it
    // couldn't be, the file then as it was, and read anew.
    bool write(const QJsonObject &object, QJsonDocument::JsonFormat format) {
        m_valid = false;
        if (!writeFileAtomically(m_path, QJsonDocument(object).toJson(format)))
            return false;
        const QFileInfo info(m_path);
        m_valid = info.exists();
        if (m_valid)
            remember(object, info);
        return true;
    }

private:
    void remember(const QJsonObject &object, const QFileInfo &info) const {
        m_object = object;
        m_modified = info.lastModified();
        m_size = info.size();
    }

    QString m_path;
    mutable QJsonObject m_object;
    mutable QDateTime m_modified;
    mutable qint64 m_size = -1;
    mutable bool m_valid = false;
};
