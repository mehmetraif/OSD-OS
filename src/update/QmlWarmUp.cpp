#include "QmlWarmUp.h"
#include "../util/AtomicFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#ifdef Q_OS_LINUX
#include <sched.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QmlWarmUp::QmlWarmUp(const QString &program, const QStringList &args, const QString &marker, const QString &stamp,
                     QObject *parent)
    : QObject(parent), m_program(program), m_args(args), m_marker(marker), m_stamp(stamp) {}

QmlWarmUp::~QmlWarmUp() {
    // The app going: what it compiled stays in the cache, the rest is
    // compiled next run.
    stopProcess();
}

QString QmlWarmUp::stamp(const QString &binary, const QString &appRoot) {
    const QFileInfo bin(binary);
    const QFileInfo main(appRoot + QStringLiteral("/Main.qml"));
    const QJsonObject o{ { QStringLiteral("binary"), bin.absoluteFilePath() },
                         { QStringLiteral("size"), double(bin.size()) },
                         { QStringLiteral("modified"), double(bin.lastModified().toMSecsSinceEpoch()) },
                         { QStringLiteral("qml"), main.absolutePath() },
                         { QStringLiteral("qmlModified"), double(main.lastModified().toMSecsSinceEpoch()) },
                         { QStringLiteral("qt"), QString::fromLatin1(qVersion()) } };
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString QmlWarmUp::markerPath() {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cache.isEmpty() ? QString() : cache + QStringLiteral("/qmlcache-built.json");
}

bool QmlWarmUp::built(const QString &marker, const QString &stamp) {
    QFile f(marker);
    return !marker.isEmpty() && f.open(QIODevice::ReadOnly)
           && QJsonDocument::fromJson(f.readAll()) == QJsonDocument::fromJson(stamp.toUtf8());
}

bool QmlWarmUp::markBuilt(const QString &marker, const QString &stamp) {
    if (marker.isEmpty() || !QDir().mkpath(QFileInfo(marker).absolutePath()))
        return false;
    return writeFileAtomically(marker, stamp.toUtf8() + '\n');
}

void QmlWarmUp::start(int delayMs) {
    if (m_done || m_poll)
        return;
    if (built(m_marker, m_stamp)) {
        m_done = true;
        return;
    }
    m_poll = new QTimer(this);
    m_poll->setInterval(m_pollMs);
    connect(m_poll, &QTimer::timeout, this, &QmlWarmUp::poll);
    QTimer::singleShot(delayMs, this, [this] {
        m_poll->start();
        poll();
    });
}

void QmlWarmUp::poll() {
    if (m_done)
        return;
    const bool busy = m_busy && m_busy();
    if (busy && m_process) {
        qInfo("[QmlWarmUp] Something plays: the QML's compile stopped, to go on once the menus are idle");
        stopProcess();
    } else if (!busy && !m_process) {
        launch();
    }
}

void QmlWarmUp::launch() {
    auto *process = new QProcess(this);
    m_process = process;
    process->setProgram(m_program);
    process->setArguments(m_args);
    process->setProcessChannelMode(QProcess::MergedChannels);
    // The lowest priority there is: the menus, and whatever they start, come
    // first.
#if defined(Q_OS_UNIX)
    process->setChildProcessModifier([] {
        ::setpriority(PRIO_PROCESS, 0, 19);
#ifdef Q_OS_LINUX
        sched_param param {};
        ::sched_setscheduler(0, SCHED_IDLE, &param);
#endif
    });
#elif defined(Q_OS_WIN)
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= IDLE_PRIORITY_CLASS;
    });
#endif
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString said = QString::fromUtf8(process->readAll()).trimmed().section(QLatin1Char('\n'), -1);
        process->deleteLater();
        if (m_process != process)
            return;  // stopped for something busy: started again once idle
        m_process = nullptr;
        if (m_timeout)
            m_timeout->stop();
        m_done = true;
        m_poll->stop();
        const bool compiled = status == QProcess::NormalExit && code == 0;
        if (compiled && !markBuilt(m_marker, m_stamp))
            qWarning("[QmlWarmUp] %s could not be written: the QML is compiled again next run", qPrintable(m_marker));
        if (compiled)
            qInfo("[QmlWarmUp] This build's QML compiled ahead: %s", qPrintable(said));
        else
            qWarning("[QmlWarmUp] This build's QML wasn't compiled ahead (%s): each page is compiled as it opens",
                     qPrintable(said.isEmpty() ? QStringLiteral("it ended with %1").arg(code) : said));
        emit finished(compiled);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_process != process)
            return;
        m_process = nullptr;
        process->deleteLater();
        m_done = true;
        m_poll->stop();
        qWarning("[QmlWarmUp] The QML's compile could not be started: %s", qPrintable(process->errorString()));
        emit finished(false);
    });
    if (!m_timeout) {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this] {
            if (!m_process)
                return;
            qWarning("[QmlWarmUp] The QML's compile took longer than %d s: given up on for this run", m_timeoutMs / 1000);
            stopProcess();
            m_done = true;
            m_poll->stop();
            emit finished(false);
        });
    }
    m_timeout->setInterval(m_timeoutMs);
    m_timeout->start();
    qInfo("[QmlWarmUp] A new build: its QML compiled ahead, at the lowest priority");
    process->start();
}

void QmlWarmUp::stopProcess() {
    QProcess *process = m_process;
    m_process = nullptr;
    if (m_timeout)
        m_timeout->stop();
    if (!process)
        return;
    process->kill();
    process->waitForFinished(2000);
    process->deleteLater();
}
