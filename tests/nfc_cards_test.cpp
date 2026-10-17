#include "modules/nfc_reader/NfcReaderBackend.h"
#include "util/FileNames.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

// An NFC card's file (NfcReaderBackend::writeCardFile), the module off so no
// reader is looked for: a card mapped again keeps its file until the new one
// is written, so a write that fails (a full card) leaves the card as it was,
// and an old file that can't be removed takes the new one back; a long title
// is cut to a name every filesystem takes, never in the middle of a letter.
class NfcCardsTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_data;
    const QString m_uid = QStringLiteral("04:A1:B2:C3");

    QString tags() const { return m_data.path() + QStringLiteral("/nfc_tags"); }
    QStringList entries() const {
        return QDir(tags()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    }

private slots:
    void initTestCase() { QVERIFY(m_data.isValid()); }

    void remap() {
        NfcReaderBackend nfc(m_data.path(), m_data.path(), nullptr);
        QVERIFY(nfc.writeCardFile(m_uid, QStringLiteral("Dune (2021)"), QStringLiteral("/films/dune.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Dune (2021).txt") }));
        QVERIFY(nfc.writeCardFile(m_uid, QStringLiteral("Alien"), QStringLiteral("/films/alien.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Alien.txt") }));
        QCOMPARE(nfc.mappedTitleForUid(m_uid), QStringLiteral("Alien"));
#ifdef Q_OS_UNIX
        // A full card: nothing more can be written, so the new file can't be,
        // and the card plays what it did. (A file size limit, which holds for
        // root too.)
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{};
        QCOMPARE(getrlimit(RLIMIT_FSIZE, &limit), 0);
        const rlimit full{ 1, limit.rlim_max };
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &full), 0);
        const bool written = nfc.writeCardFile(m_uid, QStringLiteral("Blade Runner"),
                                               QStringLiteral("/films/blade-runner.mkv"), QString());
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &limit), 0);
        QVERIFY(!written);
        QCOMPARE(entries(), QStringList({ QStringLiteral("Alien.txt") }));
        QCOMPARE(nfc.mappedTitleForUid(m_uid), QStringLiteral("Alien"));

        // The old file can't be removed (a folder in its place): the new one
        // is taken back, so no two files claim the card.
        QVERIFY(QFile::remove(tags() + QStringLiteral("/Alien.txt")));
        QVERIFY(QDir().mkpath(tags() + QStringLiteral("/Alien.txt/inside")));
        QVERIFY(!nfc.writeCardFile(m_uid, QStringLiteral("Blade Runner"),
                                   QStringLiteral("/films/blade-runner.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Alien.txt") }));
        QVERIFY(QDir(tags() + QStringLiteral("/Alien.txt")).removeRecursively());
#endif
    }

    void longTitle() {
        NfcReaderBackend nfc(m_data.path(), m_data.path(), nullptr);
        // Two bytes a letter, then four (a letter of two UTF-16 halves).
        for (const QString &title : { QString(300, QChar(0x00E4)), QString::fromUtf8("\xF0\x9F\x98\x80").repeated(100) }) {
            QVERIFY(nfc.writeCardFile(QStringLiteral("04:00:00:01"), title, QStringLiteral("/films/long.mkv"), QString()));
            const QStringList written = QDir(tags()).entryList({ QStringLiteral("*.txt") }, QDir::Files);
            QCOMPARE(written.size(), 1);
            const QString name = written.first();
            QVERIFY(name.toUtf8().size() <= 255);
            QCOMPARE(QString::fromUtf8(name.toUtf8()), name);
            QVERIFY(name.startsWith(title.left(10)));
            QVERIFY(QFile::remove(tags() + QLatin1Char('/') + name));
        }
        // The cut keeps to whole letters, and a name's own rules after it.
        QCOMPARE(safeFileName(QStringLiteral("ab") + QString::fromUtf8("\xF0\x9F\x98\x80"), QLatin1Char(' '), 3),
                 QStringLiteral("ab"));
        QCOMPARE(safeFileName(QStringLiteral("abc d"), QLatin1Char(' '), 0, QStringLiteral("file"), 4),
                 QStringLiteral("abc"));
    }
};

QTEST_GUILESS_MAIN(NfcCardsTest)
#include "nfc_cards_test.moc"
