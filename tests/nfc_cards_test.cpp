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

        // A file named by hand with its extension in capitals: mapped again,
        // it is the one replaced, under its own name for the same title, so
        // no second file claims the card.
        QVERIFY(QFile::rename(tags() + QStringLiteral("/Alien.txt"), tags() + QStringLiteral("/Alien.TXT")));
        nfc.reloadMapping();
        QVERIFY(nfc.writeCardFile(m_uid, QStringLiteral("Alien"), QStringLiteral("/films/aliens.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Alien.TXT") }));
        QVERIFY(nfc.writeCardFile(m_uid, QStringLiteral("Aliens"), QStringLiteral("/films/aliens.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Aliens.txt") }));
        QVERIFY(nfc.writeCardFile(m_uid, QStringLiteral("Alien"), QStringLiteral("/films/alien.mkv"), QString()));
        QCOMPARE(entries(), QStringList({ QStringLiteral("Alien.txt") }));
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

    // Auto Show Subtitles saved in its old form, On or Off (true or false),
    // shows as On and Forced Only, as Local Files' does.
    void oldSubtitleSetting() {
        NfcReaderBackend nfc(m_data.path(), m_data.path(), nullptr);
        QSignalSpy options(&nfc, &NfcReaderBackend::dynamicOptionsReady);
        nfc.get_auto_subtitles_options();
        QCOMPARE(options.size(), 1);
        QCOMPARE(options.first().at(0).toString(), QStringLiteral("auto_subtitles"));
        QHash<QString, QVariant> old;
        for (const QVariant &option : options.first().at(1).toList())
            old.insert(option.toMap().value("id").toString(), option.toMap().value("old"));
        QCOMPARE(old.value("on"), QVariant(true));
        QCOMPARE(old.value("forced"), QVariant(false));
        QVERIFY(!old.value("off").isValid());

        // And its player plays it so (autoSubtitlesChoice()): the old forms
        // as On and Forced Only, the new as they are, nothing saved (or
        // something no choice is) as the default, Forced Only.
        QCOMPARE(nfc.autoSubtitlesChoice(QVariant(true)), QStringLiteral("on"));
        QCOMPARE(nfc.autoSubtitlesChoice(QVariant(false)), QStringLiteral("forced"));
        for (const QString &choice : { QStringLiteral("forced"), QStringLiteral("on"), QStringLiteral("off") })
            QCOMPARE(nfc.autoSubtitlesChoice(choice), choice);
        QCOMPARE(nfc.autoSubtitlesChoice(QVariant()), QStringLiteral("forced"));
        QCOMPARE(nfc.autoSubtitlesChoice(QString()), QStringLiteral("forced"));
        QCOMPARE(nfc.autoSubtitlesChoice(QStringLiteral("sometimes")), QStringLiteral("forced"));
    }

    // Subtitle Language's names come from the one list the app has, in Local
    // Files' folder.
    void subtitleLanguagesFromLocalFilesList() {
        QTemporaryDir app;
        QVERIFY(QDir().mkpath(app.path() + QStringLiteral("/modules/local_files")));
        QFile list(app.path() + QStringLiteral("/modules/local_files/iso639-1.json"));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write(R"([{"id":"de","label":"German"}])");
        list.close();
        NfcReaderBackend nfc(app.path(), m_data.path(), nullptr);
        QSignalSpy options(&nfc, &NfcReaderBackend::dynamicOptionsReady);
        nfc.get_subtitle_languages();
        QCOMPARE(options.size(), 1);
        QCOMPARE(options.first().at(0).toString(), QStringLiteral("sub_lang"));
        const QVariantList languages = options.first().at(1).toList();
        QCOMPARE(languages.size(), 2);
        QCOMPARE(languages.at(1).toMap().value("label").toString(), QStringLiteral("German"));
    }
};

QTEST_GUILESS_MAIN(NfcCardsTest)
#include "nfc_cards_test.moc"
