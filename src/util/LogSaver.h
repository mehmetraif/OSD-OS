#pragma once

#include <QObject>
#include <QString>

class QProcess;

// Quit → Save Logs on the OSD/OS image (AppCore::saveLogs()): the logs a bug
// report wants, copied to the Logs folder of the card's OSD-OS partition
// (OSDOS_MEDIA_DIR, which a computer reading the card shows as a drive) by
// scripts/save-logs.sh beside the app, in a process of its own (Linux only).
class LogSaver : public QObject {
    Q_OBJECT
public:
    LogSaver(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // Whether they can be saved here: the partition named, and the script there.
    bool available() const;
    // The copy started: saved() says how it went. False, and nothing done,
    // while a copy runs or where it can't.
    bool save();

signals:
    // ok with the folder written, relative to the partition
    // ("Logs/2026-10-31_16-05-12"), else why not.
    void saved(bool ok, const QString &detail);

private:
    QString m_appRoot;
    QString m_dataRoot;
    // The copy while it runs.
    QProcess *m_process = nullptr;
};
