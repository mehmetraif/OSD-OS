#include "LogSaver.h"
#include "LegacyNames.h"
#include "ProcessOver.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace {
// A second or two on a card, and more for a journal a flood of lines has
// filled (save-logs.sh copies the small files first and writes them to the
// card before it reads the journal); one that hangs (a card gone bad) is
// ended rather than waited on for good.
constexpr int kLimitMs = 90 * 1000;
} // namespace

LogSaver::LogSaver(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot) {}

bool LogSaver::available() const {
#ifdef Q_OS_LINUX
    return !legacy::env("MEDIA_DIR").isEmpty()
        && QFileInfo::exists(m_appRoot + QStringLiteral("/scripts/save-logs.sh"));
#else
    return false;
#endif
}

bool LogSaver::save() {
    if (m_process || !available())
        return false;
    const QString card = legacy::env("MEDIA_DIR");
    auto *saver = new QProcess(this);
    m_process = saver;
    whenOver(saver, this, [this, saver, card](const ProcessEnd &end) {
        m_process = nullptr;
        const QString folder = QString::fromUtf8(saver->readAllStandardOutput()).trimmed();
        const QString error = QString::fromUtf8(saver->readAllStandardError()).trimmed();
        const bool ok = end.how == ProcessEnd::Exited && end.code == 0 && !folder.isEmpty();
        QString detail;
        if (ok)
            detail = QDir(card).relativeFilePath(folder);
        else if (end.how == ProcessEnd::NotStarted)
            detail = QStringLiteral("save-logs.sh could not be run: ") + saver->errorString();
        else if (end.how == ProcessEnd::TimedOut)
            detail = QStringLiteral("Copying them took over %1 seconds").arg(kLimitMs / 1000);
        else if (end.how == ProcessEnd::Crashed)
            detail = QStringLiteral("save-logs.sh ended unexpectedly");
        else if (end.code == 0)
            // Nothing written: the partition not mounted, or read-only.
            detail = QStringLiteral("The card's OSD-OS partition isn't there or can't be written");
        else
            detail = error.isEmpty() ? QStringLiteral("save-logs.sh ended with %1").arg(end.code) : error;
        if (ok)
            qInfo("[logs] Saved to %s", qPrintable(detail));
        else
            qWarning("[logs] Not saved: %s", qPrintable(detail));
        emit saved(ok, detail);
        // One that ran over the limit is ended, and deleted once it has gone.
        if (end.how != ProcessEnd::TimedOut)
            saver->deleteLater();
    }, kLimitMs);
    saver->start(QStringLiteral("/bin/sh"),
                 { m_appRoot + QStringLiteral("/scripts/save-logs.sh"), m_dataRoot, card });
    return true;
}
