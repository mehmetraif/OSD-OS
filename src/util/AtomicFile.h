#pragma once

#include <QByteArray>
#include <QDebug>
#include <QFileDevice>
#include <QSaveFile>
#include <QString>

// Writes a file whole, the way every file the app keeps its state in is
// written: into a new file beside it, renamed over it once complete and on
// the disk (QSaveFile). A crash, a power cut or a full disk on the way leaves
// the file as it was, never cut short, and a reader never sees half of it.
// That matters most for config.json, whose readers take a file they can't
// read for none at all: the next save would write the defaults over every
// setting. `permissions`, when given, are set before anything is written
// (owner-only, for a token or a key); otherwise the file keeps those of the
// one it replaces. False, with the reason in the log, when it couldn't be
// written: the file is then as it was.
inline bool writeFileAtomically(const QString &path, const QByteArray &data,
                                QFileDevice::Permissions permissions = {}) {
    QSaveFile file(path);
    // flush() first: Qt 6.4's commit() takes a write that fails as the file
    // closes (the last of it, on a full card) for one that succeeded, and
    // renames the file cut short over the whole one.
    const bool written = file.open(QIODevice::WriteOnly)
                         && (!permissions || file.setPermissions(permissions))
                         && file.write(data) == data.size()
                         && file.flush()
                         && file.commit();
    if (!written)
        qWarning("[AtomicFile] Could not write %s: %s", qPrintable(path), qPrintable(file.errorString()));
    return written;
}
