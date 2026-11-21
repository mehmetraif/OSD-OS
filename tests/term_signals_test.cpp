#include "util/TermSignals.h"

#include <QCoreApplication>
#include <QObject>
#include <QSocketNotifier>
#include <QTest>
#include <QTimer>
#include <csignal>

// The signals that end the app (util/TermSignals), handled from the event
// loop: each one comes to the handler there, once, whichever it is, and
// nothing polls for them meanwhile (no timer wakes the app ten times a
// second, as main()'s once did).
class TermSignalsTest : public QObject {
    Q_OBJECT

private slots:
    void eachSignalReachesTheEventLoop() {
        QObject context;
        QList<int> heard;
        termsignals::install(&context, [&heard](int sig) { heard << sig; });
        QVERIFY(context.findChildren<QTimer *>().isEmpty());
        QCOMPARE(context.findChildren<QSocketNotifier *>().size(), 1);

        std::raise(SIGTERM);
        // Recorded where it came, handled as the loop turns.
        QVERIFY(heard.isEmpty());
        QTRY_COMPARE(heard, QList<int>({ SIGTERM }));
        std::raise(SIGINT);
        std::raise(SIGHUP);
        QTRY_COMPARE(heard, QList<int>({ SIGTERM, SIGINT, SIGHUP }));
        QTest::qWait(200);
        QCOMPARE(heard.size(), 3);
    }
};

QTEST_GUILESS_MAIN(TermSignalsTest)
#include "term_signals_test.moc"
