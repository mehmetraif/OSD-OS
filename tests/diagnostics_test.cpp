// The crash and stall reports (util/Diagnostics), each from a child process
// that crashes or freezes on purpose: this program itself, run with --crash,
// --stall or --freeze and a folder to write its report in.
#include "util/Diagnostics.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <csignal>

static int runChild(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QByteArray mode(argv[1]);
    const QString folder = QString::fromLocal8Bit(argv[2]);
    if (mode == "--crash") {
        diagnostics::installCrashHandlers(folder);
        std::raise(SIGSEGV);
        return 0;
    }
    // --stall: stuck 1.5 s, reported at 0.3 s, and answering again after.
    // --freeze: stuck for good, so it ends at 1 s, as for a restart.
    // A thread of the app's own, waiting meanwhile, is in the report too.
    const bool freeze = mode == "--freeze";
    QThread helper;
    helper.setObjectName(QStringLiteral("diag-helper"));
    helper.start();
    diagnostics::startStallWatchdog(folder, 300, freeze ? 1000 : 0);
    QTimer::singleShot(100, &app, [&app, freeze]() {
        QThread::msleep(freeze ? 10000 : 1500);
        QTimer::singleShot(800, &app, &QCoreApplication::quit);
    });
    const int code = app.exec();
    diagnostics::stopStallWatchdog();
    helper.quit();
    helper.wait();
    return code;
}

class DiagnosticsTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    struct Run {
        QString output;
        int exitCode = -1;
        QProcess::ExitStatus status = QProcess::NormalExit;
    };
    Run run(const char *mode) {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), { QString::fromLatin1(mode), m_dir.path() });
        child.waitForFinished(20000);
        return { QString::fromLocal8Bit(child.readAll()), child.exitCode(), child.exitStatus() };
    }
    QString report(const char *name) const {
        QFile file(m_dir.path() + QLatin1Char('/') + QLatin1String(name));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    // The app's memory and the system's, as the journal shows them.
    void memoryLine() {
        const QString line = diagnostics::memoryLine();
        QVERIFY2(line.startsWith(QStringLiteral("app ")) && line.contains(QStringLiteral(" MB")),
                 qPrintable(line));
    }

    // A crash says so in the journal and in crash-report.txt, with where it
    // was, and still ends as a crash, for the service to start it again.
    void crashIsReported() {
        const Run r = run("--crash");
        QCOMPARE(r.status, QProcess::CrashExit);
        const QString text = report("crash-report.txt");
        QVERIFY2(text.contains(QStringLiteral("ended by SIGSEGV")), qPrintable(text));
        QVERIFY2(text.count(QLatin1Char('\n')) >= 3, qPrintable(text));
        QVERIFY2(r.output.contains(QStringLiteral("[crash]")), qPrintable(r.output));
    }

    // A main thread stuck for a while: reported with where it is (asleep, in
    // QThread's sleep and libc's) and where the other threads are, and again
    // once it answers; the app goes on.
    void stallIsReported() {
        const Run r = run("--stall");
        QCOMPARE(r.status, QProcess::NormalExit);
        QCOMPARE(r.exitCode, 0);
        const QString text = report("stall-report.txt");
        QVERIFY2(text.contains(QStringLiteral("hasn't answered")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("The main thread is here")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("sleep")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral(", diag-helper:\n")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("answer)")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("answered again")), qPrintable(text));
        QVERIFY2(r.output.contains(QStringLiteral("[watchdog]")), qPrintable(r.output));
    }

    // Stuck for good: the app ends with the code that has the service start
    // it again, and the report says so.
    void freezeEndsTheApp() {
        const Run r = run("--freeze");
        QCOMPARE(r.status, QProcess::NormalExit);
        QCOMPARE(r.exitCode, diagnostics::kStallExitCode);
        const QString text = report("stall-report.txt");
        QVERIFY2(text.contains(QStringLiteral("starts again")), qPrintable(text));
    }
};

int main(int argc, char **argv) {
    if (argc > 2 && (qstrcmp(argv[1], "--crash") == 0 || qstrcmp(argv[1], "--stall") == 0
                     || qstrcmp(argv[1], "--freeze") == 0))
        return runChild(argc, argv);
    QCoreApplication app(argc, argv);
    DiagnosticsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "diagnostics_test.moc"
