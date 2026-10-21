#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class QProcess;
class QTimer;

// Boot screen state for the OSD/OS image (os/). The image starts the app
// ahead of the rest of the system and holds a list of services back until the
// app's first frame is on screen; this object releases them (markReady) and
// reports how far they have got, so Main.qml can show it.
//
// Inert everywhere else: unless OSDOS_BOOT_UNITS_FILE names a unit list and
// systemd says the boot is still in progress, `active` stays false and nothing
// is polled. markReady only writes OSDOS_READY_FILE when that is set.
class BootProgress : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    // [{ unit, label, state }] in list order; state is one of
    // "pending", "running", "done", "failed", "skipped". An optional step
    // only once it has started.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
    // Label of the step currently starting, or "" once every step is settled.
    Q_PROPERTY(QString currentLabel READ currentLabel NOTIFY stepsChanged)

public:
    explicit BootProgress(QObject *parent = nullptr);
    ~BootProgress() override;

    bool active() const { return m_active; }
    qreal progress() const { return m_progress; }
    QVariantList steps() const;
    QString currentLabel() const;

    // The first frame is on screen: write OSDOS_READY_FILE so the held-back
    // services start. Safe to call more than once; only the first call acts.
    void markReady();

signals:
    void activeChanged();
    void progressChanged();
    void stepsChanged();

private:
    enum class State { Pending, Running, Done, Failed, Skipped };
    struct Step {
        QString unit;
        QString label;
        // Listed only once it starts: a unit that runs at some starts alone
        // (OSD/OS Setup's, with its file on the card).
        bool    optional = false;
        State   state = State::Pending;
    };

    bool loadSteps(const QString &path);
    QStringList showArguments() const;
    void poll();
    void applyUnitStates(const QByteArray &output);
    void applySystemState(const QByteArray &output);
    State stateFor(const QHash<QString, QString> &unit) const;
    void recompute();
    void close(const char *reason);

    static QString stateName(State s);
    static bool isSettled(State s) { return s == State::Done || s == State::Failed || s == State::Skipped; }
    static bool isShown(const Step &step)
    {
        return !step.optional || step.state == State::Running || step.state == State::Done
            || step.state == State::Failed;
    }

    QVector<Step>  m_steps;
    bool           m_active        = false;
    bool           m_closing       = false;
    bool           m_readyMarked   = false;
    bool           m_systemBooting = true;
    bool           m_warnedOutput  = false;
    qreal          m_progress      = 0;
    QElapsedTimer  m_clock;
    QTimer        *m_pollTimer = nullptr;
    QProcess      *m_unitsProc = nullptr;
    QProcess      *m_stateProc = nullptr;
};
