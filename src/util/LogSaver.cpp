#include "LogSaver.h"
#include "LegacyNames.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

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
    // A second or two on a card, and more for a journal a flood of lines
    // has filled (save-logs.sh copies the small files first and writes them
    // to the card before it reads the journal); one that hangs (a card gone
    // bad) is ended rather than waited on for good.
    auto *limit = new QTimer(saver);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, saver, [saver] { saver->kill(); });
    const auto done = [this, saver](bool ok, const QString &detail) {
        if (m_process != saver)
            return;
        m_process = nullptr;
        if (ok)
            qInfo("[logs] Saved to %s", qPrintable(detail));
        else
            qWarning("[logs] Not saved: %s", qPrintable(detail));
        emit saved(ok, detail);
        saver->deleteLater();
    };
    connect(saver, &QProcess::errorOccurred, this, [saver, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false, QStringLiteral("save-logs.sh could not be run: ") + saver->errorString());
    });
    connect(saver, &QProcess::finished, this, [saver, card, limit, done](int code, QProcess::ExitStatus status) {
        const bool tooLong = !limit->isActive();
        limit->stop();
        const QString folder = QString::fromUtf8(saver->readAllStandardOutput()).trimmed();
        const QString error = QString::fromUtf8(saver->readAllStandardError()).trimmed();
        if (status != QProcess::NormalExit)
            done(false, tooLong ? QStringLiteral("Copying them took over 90 seconds") : QStringLiteral("save-logs.sh ended unexpectedly"));
        else if (code == 0 && folder.isEmpty())
            // Nothing written: the partition not mounted, or read-only.
            done(false, QStringLiteral("The card's OSD-OS partition isn't there or can't be written"));
        else if (code == 0)
            done(true, QDir(card).relativeFilePath(folder));
        else
            done(false, error.isEmpty() ? QStringLiteral("save-logs.sh ended with %1").arg(code) : error);
    });
    limit->start(90000);
    saver->start(QStringLiteral("/bin/sh"),
                 { m_appRoot + QStringLiteral("/scripts/save-logs.sh"), m_dataRoot, card });
    return true;
}
