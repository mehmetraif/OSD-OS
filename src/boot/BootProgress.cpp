#include "BootProgress.h"
#include "util/LegacyNames.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>

namespace {

// The boot screen never outstays this, whatever the services do.
constexpr int kMaxShowMs = 60000;
constexpr int kPollMs    = 300;
// Once every step has settled the screen lingers at 100% this long, so the
// last step's result can be read before it closes.
constexpr int kLingerMs  = 1200;
constexpr int kMaxSteps  = 16;

const QString kSystemctl = QStringLiteral("systemctl");

bool systemStillBooting(const QString &state)
{
    return state == QLatin1String("initializing") || state == QLatin1String("starting");
}

} // namespace

BootProgress::BootProgress(QObject *parent)
    : QObject(parent)
{
    m_clock.start();

    const QString unitsFile = legacy::env("BOOT_UNITS_FILE");
    if (unitsFile.isEmpty() || !loadSteps(unitsFile))
        return;

    // Only while systemd is still booting: starting the app again later (an
    // in-app update, Exit to Terminal and back, systemctl restart) must not
    // replay the screen.
    // One synchronous call, before the first frame, so that frame is already
    // the right one.
    QProcess probe;
    probe.start(kSystemctl, {QStringLiteral("is-system-running")});
    if (!probe.waitForFinished(2000)) {
        probe.kill();
        probe.waitForFinished(500);
        qWarning("[boot] systemctl did not answer - no boot screen");
        return;
    }
    const QString systemState = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (!systemStillBooting(systemState)) {
        qInfo("[boot] system is %s - no boot screen",
              qPrintable(systemState.isEmpty() ? QStringLiteral("unknown") : systemState));
        return;
    }

    m_unitsProc = new QProcess(this);
    m_stateProc = new QProcess(this);
    connect(m_unitsProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        applyUnitStates(m_unitsProc->readAllStandardOutput());
    });
    connect(m_stateProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        applySystemState(m_stateProc->readAllStandardOutput());
    });
    // finished never arrives for a process that failed to start, so without
    // this a missing systemctl would leave the screen up until the timeout.
    for (QProcess *proc : {m_unitsProc, m_stateProc}) {
        connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                close("systemctl failed to start");
        });
    }

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollMs);
    connect(m_pollTimer, &QTimer::timeout, this, &BootProgress::poll);

    m_active = true;
    // The units' states before the first frame too, so that it lists what is
    // starting already: an optional step, listed only once it has started,
    // would otherwise come in under a frame drawn without it.
    QProcess first;
    first.start(kSystemctl, showArguments());
    if (first.waitForFinished(2000)) {
        applyUnitStates(first.readAllStandardOutput());
    } else {
        first.kill();
        first.waitForFinished(500);
    }
    qInfo("[boot] boot screen up, watching %lld unit(s)", static_cast<long long>(m_steps.size()));
    if (!m_closing)
        m_pollTimer->start();
    QTimer::singleShot(kMaxShowMs, this, [this] { close("timed out"); });
}

BootProgress::~BootProgress()
{
    // The app can quit mid-poll (a stop during boot); reap the query rather
    // than leave QProcess to complain about destroying a running process.
    for (QProcess *proc : {m_unitsProc, m_stateProc}) {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(500);
        }
    }
}

bool BootProgress::loadSteps(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("[boot] cannot read %s - no boot screen", qPrintable(path));
        return false;
    }

    // One "unit|LABEL" per line, or "unit|LABEL|optional" for a step listed
    // only once its unit starts; '#' starts a comment. The unit names go to
    // systemctl as plain arguments, but anything that is not a unit name is
    // still refused rather than passed along.
    static const QRegularExpression unitName(QStringLiteral("^[A-Za-z0-9:_.@\\\\-]+$"));
    while (!file.atEnd() && m_steps.size() < kMaxSteps) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList fields = line.split(QLatin1Char('|'));
        Step step;
        step.unit     = fields.value(0).trimmed();
        step.label    = fields.value(1).trimmed();
        step.optional = fields.value(2).trimmed() == QLatin1String("optional");
        if (!unitName.match(step.unit).hasMatch()) {
            qWarning("[boot] skipping bad unit name in %s: %s", qPrintable(path), qPrintable(step.unit));
            continue;
        }
        if (fields.size() > 2 && !step.optional)
            qWarning("[boot] %s: unknown flag in %s: %s", qPrintable(step.unit), qPrintable(path),
                     qPrintable(fields.mid(2).join(QLatin1Char('|'))));
        if (step.label.isEmpty())
            step.label = step.unit;
        m_steps.append(step);
    }
    return !m_steps.isEmpty();
}

void BootProgress::markReady()
{
    if (m_readyMarked)
        return;
    m_readyMarked = true;
    qInfo("[boot] first frame %lld ms after start", static_cast<long long>(m_clock.elapsed()));

    const QString path = legacy::env("READY_FILE");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[boot] cannot write %s: %s", qPrintable(path), qPrintable(file.errorString()));
        return;
    }
    file.write(QByteArray::number(QCoreApplication::applicationPid()) + '\n');
}

void BootProgress::poll()
{
    if (!m_active || m_closing)
        return;

    if (m_unitsProc->state() == QProcess::NotRunning)
        m_unitsProc->start(kSystemctl, showArguments());
    if (m_stateProc->state() == QProcess::NotRunning)
        m_stateProc->start(kSystemctl, {QStringLiteral("is-system-running")});
}

QStringList BootProgress::showArguments() const
{
    QStringList args{QStringLiteral("show"), QStringLiteral("--no-pager"),
                     QStringLiteral("--property=Id,LoadState,ActiveState,"
                                    "ConditionResult,ConditionTimestampMonotonic"),
                     QStringLiteral("--")};
    for (const Step &step : m_steps)
        args << step.unit;
    return args;
}

void BootProgress::applySystemState(const QByteArray &output)
{
    if (!m_active)
        return;
    m_systemBooting = systemStillBooting(QString::fromUtf8(output).trimmed());
}

BootProgress::State BootProgress::stateFor(const QHash<QString, QString> &unit) const
{
    const QString loadState   = unit.value(QStringLiteral("LoadState"));
    const QString activeState = unit.value(QStringLiteral("ActiveState"));
    if (loadState == QLatin1String("not-found") || loadState == QLatin1String("masked")
        || loadState == QLatin1String("bad-setting") || loadState == QLatin1String("error"))
        return State::Skipped;
    // A unit whose Condition*= failed (bluetooth.service on a board without
    // Bluetooth) stays inactive for good. ConditionResult reads "no" before
    // any check too, hence the timestamp.
    if (activeState == QLatin1String("inactive")
        && unit.value(QStringLiteral("ConditionResult")) == QLatin1String("no")
        && unit.value(QStringLiteral("ConditionTimestampMonotonic"), QStringLiteral("0")) != QLatin1String("0"))
        return State::Skipped;
    if (activeState == QLatin1String("active") || activeState == QLatin1String("reloading"))
        return State::Done;
    if (activeState == QLatin1String("failed"))
        return State::Failed;
    if (activeState == QLatin1String("activating") || activeState == QLatin1String("deactivating"))
        return State::Running;
    // Inactive: still queued while the boot goes on; afterwards it is not
    // going to start this boot at all.
    return m_systemBooting ? State::Pending : State::Skipped;
}

void BootProgress::applyUnitStates(const QByteArray &output)
{
    if (!m_active || m_closing)
        return;

    // `systemctl show` prints one Key=Value block per unit, in argument order,
    // separated by blank lines.
    QVector<QHash<QString, QString>> blocks;
    QHash<QString, QString> current;
    for (const QByteArray &raw : output.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) {
            if (!current.isEmpty()) {
                blocks.append(current);
                current.clear();
            }
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            current.insert(line.left(eq), line.mid(eq + 1));
    }
    if (!current.isEmpty())
        blocks.append(current);
    if (blocks.size() != m_steps.size()) {
        if (!m_warnedOutput) {
            m_warnedOutput = true;
            qWarning("[boot] unexpected systemctl output (%lld blocks for %lld units)",
                     static_cast<long long>(blocks.size()), static_cast<long long>(m_steps.size()));
        }
        return;
    }

    bool changed = false;
    for (int i = 0; i < m_steps.size(); ++i) {
        Step &step = m_steps[i];
        if (isSettled(step.state))
            continue;   // a settled step never goes back
        const State next = stateFor(blocks[i]);
        if (next == step.state)
            continue;
        step.state = next;
        changed = true;
        if (isSettled(next))
            qInfo("[boot] %s %s at %lld ms", qPrintable(step.unit), qPrintable(stateName(next)),
                  static_cast<long long>(m_clock.elapsed()));
    }
    if (changed)
        recompute();
}

void BootProgress::recompute()
{
    int settled = 0;
    int running = 0;
    int shown = 0;
    bool allSettled = true;
    for (const Step &step : m_steps) {
        allSettled = allSettled && isSettled(step.state);
        // An optional step that hasn't started counts for nothing.
        if (!isShown(step))
            continue;
        ++shown;
        if (isSettled(step.state))
            ++settled;
        else if (step.state == State::Running)
            ++running;
    }
    // A step that has started counts for half, so the bar moves on every
    // change; and never back, though a step listed late adds to the whole.
    const qreal progress = qMax(m_progress, shown ? (settled + 0.5 * running) / shown : qreal(1));
    if (progress != m_progress) {
        m_progress = progress;
        emit progressChanged();
    }
    emit stepsChanged();

    if (allSettled && !m_closing) {
        m_closing = true;
        m_pollTimer->stop();
        QTimer::singleShot(kLingerMs, this, [this] { close("all services settled"); });
    }
}

void BootProgress::close(const char *reason)
{
    if (!m_active)
        return;
    m_active = false;
    m_pollTimer->stop();
    QStringList unsettled;
    for (const Step &step : m_steps) {
        if (!isSettled(step.state))
            unsettled << step.unit;
    }
    qInfo("[boot] boot screen closed at %lld ms (%s)%s%s", static_cast<long long>(m_clock.elapsed()), reason,
          unsettled.isEmpty() ? "" : " - still starting: ", qPrintable(unsettled.join(QLatin1Char(' '))));
    emit activeChanged();
}

QVariantList BootProgress::steps() const
{
    QVariantList list;
    for (const Step &step : m_steps) {
        if (!isShown(step))
            continue;
        list.append(QVariantMap{
            {QStringLiteral("unit"),  step.unit},
            {QStringLiteral("label"), step.label},
            {QStringLiteral("state"), stateName(step.state)},
        });
    }
    return list;
}

QString BootProgress::currentLabel() const
{
    for (const Step &step : m_steps) {
        if (step.state == State::Running)
            return step.label;
    }
    for (const Step &step : m_steps) {
        if (step.state == State::Pending && !step.optional)
            return step.label;
    }
    return {};
}

QString BootProgress::stateName(State s)
{
    switch (s) {
    case State::Pending: return QStringLiteral("pending");
    case State::Running: return QStringLiteral("running");
    case State::Done:    return QStringLiteral("done");
    case State::Failed:  return QStringLiteral("failed");
    case State::Skipped: return QStringLiteral("skipped");
    }
    return QStringLiteral("pending");
}
