// A process over (util/ProcessOver.h's whenOver()), told once each time,
// against /bin/sh: one that exits with a code, one killed, one that can't be
// started at all, one started again, and one still running at the time limit,
// told at once with what it wrote by then, then ended and deleted, nothing
// more told; one that ignores being asked to end killed all the same.
#include "util/ProcessOver.h"
#include <QPointer>
#include <QtTest>

class ProcessOverTest : public QObject {
    Q_OBJECT

    // What a whenOver() told, in order.
    struct Told {
        QList<ProcessEnd> ends;
        QStringList output;   // what the process had written, read as it was told
    };

    static QProcess *sh(QObject *parent, const QString &script) {
        auto *process = new QProcess(parent);
        process->setProgram(QStringLiteral("/bin/sh"));
        process->setArguments({ QStringLiteral("-c"), script });
        return process;
    }

    static void follow(QProcess *process, QObject *context, Told *told, int timeoutMs = 0) {
        whenOver(process, context, [process, told](const ProcessEnd &end) {
            told->ends.append(end);
            told->output.append(QString::fromUtf8(process->readAllStandardOutput()).trimmed());
        }, timeoutMs);
    }

private slots:
    // Exited: its code.
    void exited() {
        QObject context;
        Told told;
        QProcess *process = sh(&context, QStringLiteral("echo done; exit 3"));
        follow(process, &context, &told);
        process->start();
        QTRY_COMPARE(told.ends.size(), 1);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::Exited);
        QCOMPARE(told.ends.at(0).code, 3);
        QCOMPARE(told.output.at(0), QStringLiteral("done"));
        QTest::qWait(100);
        QCOMPARE(told.ends.size(), 1);
    }

    // Killed by a signal: crashed, no code.
    void crashed() {
        QObject context;
        Told told;
        QProcess *process = sh(&context, QStringLiteral("kill -9 $$"));
        follow(process, &context, &told);
        process->start();
        QTRY_COMPARE(told.ends.size(), 1);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::Crashed);
        QCOMPARE(told.ends.at(0).code, -1);
    }

    // No such program: told, though QProcess emits no finished() for it.
    void notStarted() {
        QObject context;
        Told told;
        auto *process = new QProcess(&context);
        follow(process, &context, &told, 5000);
        process->start(QStringLiteral("/nonexistent/osdos-no-such-program"), {});
        QTRY_COMPARE(told.ends.size(), 1);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::NotStarted);
        QCOMPARE(told.ends.at(0).code, -1);
        QTest::qWait(100);
        QCOMPARE(told.ends.size(), 1);
    }

    // Connected once, a process started again is told of each time.
    void startedAgain() {
        QObject context;
        Told told;
        QProcess *process = sh(&context, QStringLiteral("exit 0"));
        follow(process, &context, &told);
        process->start();
        QTRY_COMPARE(told.ends.size(), 1);
        process->setArguments({ QStringLiteral("-c"), QStringLiteral("exit 1") });
        process->start();
        QTRY_COMPARE(told.ends.size(), 2);
        QCOMPARE(told.ends.at(0).code, 0);
        QCOMPARE(told.ends.at(1).code, 1);
    }

    // Still running at the time limit: told at once, its output so far there
    // to read, then ended and deleted, and nothing told as it goes.
    void timedOut() {
        QObject context;
        Told told;
        QPointer<QProcess> process = sh(&context, QStringLiteral("echo early; exec sleep 30"));
        follow(process, &context, &told, 300);
        process->start();
        QTRY_COMPARE_WITH_TIMEOUT(told.ends.size(), 1, 3000);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::TimedOut);
        QCOMPARE(told.output.at(0), QStringLiteral("early"));
        QTRY_VERIFY_WITH_TIMEOUT(process.isNull(), 3000);
        QCOMPARE(told.ends.size(), 1);
    }

    // One that won't end when asked is killed a moment later.
    void timedOutStubborn() {
        QObject context;
        Told told;
        QPointer<QProcess> process = sh(&context, QStringLiteral("trap '' TERM; echo ready; while :; do sleep 1; done"));
        follow(process, &context, &told, 300);
        process->start();
        QTRY_COMPARE_WITH_TIMEOUT(told.ends.size(), 1, 3000);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::TimedOut);
        QTRY_VERIFY_WITH_TIMEOUT(process.isNull(), 5000);
        QCOMPARE(told.ends.size(), 1);
    }

    // Over in time: the limit tells nothing more.
    void overInTime() {
        QObject context;
        Told told;
        QProcess *process = sh(&context, QStringLiteral("exit 0"));
        follow(process, &context, &told, 300);
        process->start();
        QTRY_COMPARE(told.ends.size(), 1);
        QTest::qWait(600);
        QCOMPARE(told.ends.size(), 1);
        QCOMPARE(told.ends.at(0).how, ProcessEnd::Exited);
    }

    // The one told gone before the limit: nothing told, nothing done.
    void contextGone() {
        Told told;
        QProcess process;
        auto *context = new QObject;
        process.setProgram(QStringLiteral("/bin/sh"));
        process.setArguments({ QStringLiteral("-c"), QStringLiteral("exec sleep 30") });
        follow(&process, context, &told, 300);
        process.start();
        QVERIFY(process.waitForStarted());
        delete context;
        QTest::qWait(600);
        QVERIFY(told.ends.isEmpty());
        QCOMPARE(process.state(), QProcess::Running);
        process.kill();
        process.waitForFinished();
    }
};

QTEST_GUILESS_MAIN(ProcessOverTest)
#include "process_over_test.moc"
