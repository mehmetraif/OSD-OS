#include "JammaPins.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {

// How long osdos-jamma has to end once asked, before it is killed.
constexpr int kStopMs = 2000;
// No line of osdos-jamma's is this long: one that is, is dropped.
constexpr int kLineMax = 256;

} // namespace

std::optional<JammaPins::Change> JammaPins::parse(const QByteArray &line) {
    static const QRegularExpression format(
        QStringLiteral("^P([12]) 0x[0-9a-f]{2} ([AB][0-7]) +(pressed|released) +([a-z0-9]+)$"));
    const QRegularExpressionMatch m = format.match(QString::fromLatin1(line.trimmed()));
    if (!m.hasMatch())
        return std::nullopt;
    return Change{ m.captured(1).toInt(), m.captured(2), m.captured(3) == QLatin1String("pressed"), m.captured(4) };
}

JammaPins::JammaPins(QObject *parent)
    : QObject(parent),
      m_program(QCoreApplication::applicationDirPath() + QStringLiteral("/osdos-jamma"))
{
}

JammaPins::~JammaPins() {
    // Its process goes with it (QProcess kills it), saying nothing here.
    if (m_process)
        m_process->disconnect(this);
}

void JammaPins::watch(bool on) {
    if (!on) {
        stopProcess();
        return;
    }
    if (m_process)
        return;
    if (!QFileInfo(m_program).isExecutable()) {
        emit failed(QStringLiteral("osdos-jamma isn't installed"));
        return;
    }
    m_process = new QProcess(this);
    m_process->setProgram(m_program);
    m_process->setArguments({ QStringLiteral("--test") });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &JammaPins::readOutput);
    connect(m_process, &QProcess::finished, this, &JammaPins::ended);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            ended();
    });
    m_process->start();
}

void JammaPins::readOutput() {
    if (!m_process)
        return;
    m_partial += m_process->readAllStandardOutput();
    for (qsizetype end = m_partial.indexOf('\n'); end >= 0; end = m_partial.indexOf('\n')) {
        const std::optional<Change> change = parse(m_partial.left(end));
        m_partial.remove(0, end + 1);
        // What is done with it may stop the watch, and clear what is left.
        if (change && change->pressed)
            emit pinPressed(change->player, change->pin, change->input);
    }
    if (m_partial.size() > kLineMax)
        m_partial.clear();
}

// It ended while watched: what it said last is why ("osdos-jamma: no MCP23017
// at 0x20 or 0x21 on /dev/i2c-0: nothing to read").
void JammaPins::ended() {
    if (!m_process)
        return;
    QString reason = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed().section('\n', -1);
    reason.remove(QRegularExpression(QStringLiteral("^osdos-jamma: ")));
    if (reason.isEmpty())
        reason = m_process->error() == QProcess::FailedToStart ? m_process->errorString()
                                                               : QStringLiteral("osdos-jamma ended");
    stopProcess();
    emit failed(reason);
}

void JammaPins::stopProcess() {
    QProcess *process = m_process;
    m_process = nullptr;
    m_partial.clear();
    if (!process)
        return;
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
        return;
    }
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->terminate();
    QTimer::singleShot(kStopMs, process, &QProcess::kill);
}
