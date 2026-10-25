#include "util/SevenZip.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

// The 7z extractor (sevenzip: the LZMA SDK's decoder, thirdparty/lzma) on
// archives made with py7zr (data/arcade): a stand-in for RetroArch's build
// for Windows (retroarch.7z: RetroArch-Win64/retroarch.exe behind the BCJ
// filter and LZMA2, a config, a folder with a file and an empty one),
// one with nothing of RetroArch's (other.7z), one for MAME's self-extracting
// release (mame-sfx.bin: a stub, then the archive with mame.exe at its top),
// and one whose first entry is named to leave the folder (escapes.7z,
// written with a plain header and the name changed in it).
class SevenZipTest : public QObject {
    Q_OBJECT

    static QString fixture(const char *name) {
        return QStringLiteral(TESTS_DIR "/data/arcade/") + QLatin1String(name);
    }
    static QByteArray contents(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    // retroarch.7z's retroarch.exe: a line, then every byte value 64 times.
    static QByteArray program() {
        QByteArray bytes("MZ a stand-in for RetroArch for Windows\n");
        for (int round = 0; round < 64; ++round) {
            for (int value = 0; value < 256; ++value)
                bytes.append(char(value));
        }
        return bytes;
    }
    static constexpr qint64 kPlenty = qint64(1) << 40;

private slots:
    // Where an entry's name puts it: under the folder, or nowhere.
    void safePaths() {
        QCOMPARE(sevenzip::safeRelativePath(QStringLiteral("RetroArch-Win64/retroarch.exe")),
                 QStringLiteral("RetroArch-Win64/retroarch.exe"));
        QCOMPARE(sevenzip::safeRelativePath(QStringLiteral("RetroArch-Win64\\assets\\readme.txt")),
                 QStringLiteral("RetroArch-Win64/assets/readme.txt"));
        QCOMPARE(sevenzip::safeRelativePath(QStringLiteral("retroarch.exe")), QStringLiteral("retroarch.exe"));
        QCOMPARE(sevenzip::safeRelativePath(QStringLiteral("a folder/")), QStringLiteral("a folder"));
        for (const char *bad : { "", "/", "/etc/passwd", "\\\\server\\share\\x", "C:/x", "C:\\x", "..", "../x", "a/../x",
                                 "a/../../x", "./a", "a/./b", "a//b", "a/\\", "a\\..\\b", "con:x" })
            QVERIFY2(sevenzip::safeRelativePath(QString::fromLatin1(bad)).isEmpty(), bad);
    }

    // What an archive holds, and what isn't one.
    void listing() {
        QString error;
        QCOMPARE(sevenzip::list(fixture("retroarch.7z"), &error),
                 QStringList({ QStringLiteral("RetroArch-Win64/retroarch.exe"), QStringLiteral("RetroArch-Win64/retroarch.cfg"),
                               QStringLiteral("RetroArch-Win64/assets/readme.txt"),
                               QStringLiteral("RetroArch-Win64/assets/empty.txt") }));
        QVERIFY(error.isEmpty());
        QCOMPARE(sevenzip::list(fixture("other.7z")), QStringList({ QStringLiteral("Something/readme.txt") }));
        QVERIFY(sevenzip::list(fixture("fbneo-arcade.dat"), &error).isEmpty());
        QCOMPARE(error, QStringLiteral("The file is not a 7z archive"));
        QVERIFY(sevenzip::list(fixture("none.7z"), &error).isEmpty());
        QVERIFY(error.endsWith(QStringLiteral("none.7z could not be opened")));
    }

    // Every file out, whole, with its folders, the progress as they come,
    // and a file already there written over.
    void extraction() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = dir.path() + QStringLiteral("/out");
        QList<QPair<qint64, qint64>> steps;
        QCOMPARE(sevenzip::extract(fixture("retroarch.7z"), folder, kPlenty, kPlenty, [&steps](qint64 written, qint64 total) {
            steps.append({ written, total });
            return true;
        }), QString());
        const QByteArray config("# RetroArch's own settings, as it comes\n");
        const QByteArray readme("the assets' readme\n");
        QCOMPARE(contents(folder + QStringLiteral("/RetroArch-Win64/retroarch.exe")), program());
        QCOMPARE(contents(folder + QStringLiteral("/RetroArch-Win64/retroarch.cfg")), config);
        QCOMPARE(contents(folder + QStringLiteral("/RetroArch-Win64/assets/readme.txt")), readme);
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/RetroArch-Win64/assets/empty.txt")));
        QCOMPARE(QFileInfo(folder + QStringLiteral("/RetroArch-Win64/assets/empty.txt")).size(), 0);
        const qint64 total = program().size() + config.size() + readme.size();
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps.first(), qMakePair(qint64(program().size()), total));
        QCOMPARE(steps.last(), qMakePair(total, total));
        // Again, over what is there.
        QFile changed(folder + QStringLiteral("/RetroArch-Win64/retroarch.cfg"));
        QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        changed.write("changed\n");
        changed.close();
        QCOMPARE(sevenzip::extract(fixture("retroarch.7z"), folder, kPlenty, kPlenty), QString());
        QCOMPARE(contents(folder + QStringLiteral("/RetroArch-Win64/retroarch.cfg")),
                 QByteArray("# RetroArch's own settings, as it comes\n"));
        // Stopped from the progress: what was written stays.
        const QString other = dir.path() + QStringLiteral("/stopped");
        QCOMPARE(sevenzip::extract(fixture("retroarch.7z"), other, kPlenty, kPlenty, [](qint64, qint64) { return false; }),
                 QStringLiteral("Stopped"));
        QVERIFY(QFileInfo::exists(other + QStringLiteral("/RetroArch-Win64/retroarch.exe")));
        QVERIFY(!QFileInfo::exists(other + QStringLiteral("/RetroArch-Win64/retroarch.cfg")));
    }

    // An archive bigger than allowed, unpacked or in one block, is refused
    // before anything is written.
    void limits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = dir.path() + QStringLiteral("/out");
        QCOMPARE(sevenzip::extract(fixture("retroarch.7z"), folder, 1000, kPlenty),
                 QStringLiteral("The 7z archive's files come to 0 MB, more than the 0 MB allowed"));
        QVERIFY(!QFileInfo::exists(folder));
        QCOMPARE(sevenzip::extract(fixture("retroarch.7z"), folder, kPlenty, 1000),
                 QStringLiteral("A block of the 7z archive is 0 MB, too big to unpack in memory"));
        QVERIFY(!QFileInfo::exists(folder));
    }

    // One cut short, and one with a byte changed in its data.
    void damaged() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray whole = contents(fixture("retroarch.7z"));
        QVERIFY(whole.size() > 200);
        const QString cut = dir.path() + QStringLiteral("/cut.7z");
        QFile cutFile(cut);
        QVERIFY(cutFile.open(QIODevice::WriteOnly));
        cutFile.write(whole.left(whole.size() / 2));
        cutFile.close();
        QString error;
        QVERIFY(sevenzip::list(cut, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("The 7z archive is damaged"));
        QCOMPARE(sevenzip::extract(cut, dir.path() + QStringLiteral("/cut"), kPlenty, kPlenty), error);

        QByteArray flipped = whole;
        flipped[40] = char(~flipped[40]);
        const QString flip = dir.path() + QStringLiteral("/flip.7z");
        QFile flipFile(flip);
        QVERIFY(flipFile.open(QIODevice::WriteOnly));
        flipFile.write(flipped);
        flipFile.close();
        QCOMPARE(sevenzip::extract(flip, dir.path() + QStringLiteral("/flip"), kPlenty, kPlenty),
                 QStringLiteral("The 7z archive is damaged"));

        // A byte of the start header itself: still an archive from its
        // start, damaged, not a file that holds none.
        QByteArray header = whole;
        header[20] = char(~header[20]);
        const QString head = dir.path() + QStringLiteral("/header.7z");
        QFile headFile(head);
        QVERIFY(headFile.open(QIODevice::WriteOnly));
        headFile.write(header);
        headFile.close();
        QVERIFY(sevenzip::list(head, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("The 7z archive is damaged"));
    }

    // MAME's release is a self-extracting archive, a program's stub with the
    // archive behind it (mame-sfx.bin, the stub carrying a decoy signature
    // of its own): read from where the archive begins. The stub alone, cut
    // before the archive, is none.
    void selfExtracting() {
        QString error;
        QCOMPARE(sevenzip::list(fixture("mame-sfx.bin"), &error),
                 QStringList({ QStringLiteral("mame.exe"), QStringLiteral("plugins/hiscore/init.lua"),
                               QStringLiteral("plugins/hiscore/plugin.json"), QStringLiteral("bgfx/shaders/readme.txt"),
                               QStringLiteral("hash/readme.txt") }));
        QVERIFY(error.isEmpty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = dir.path() + QStringLiteral("/mame");
        QCOMPARE(sevenzip::extract(fixture("mame-sfx.bin"), folder, kPlenty, kPlenty), QString());
        QByteArray mame("MZ a stand-in for MAME for Windows\n");
        for (int round = 0; round < 32; ++round) {
            for (int value = 0; value < 256; ++value)
                mame.append(char(value));
        }
        QCOMPARE(contents(folder + QStringLiteral("/mame.exe")), mame);
        QCOMPARE(contents(folder + QStringLiteral("/plugins/hiscore/init.lua")), QByteArray("-- the hiscore plugin\n"));
        QCOMPARE(contents(folder + QStringLiteral("/hash/readme.txt")), QByteArray("<softwarelists/>\n"));
        const QByteArray whole = contents(fixture("mame-sfx.bin"));
        const QString stub = dir.path() + QStringLiteral("/stub.bin");
        QFile stubFile(stub);
        QVERIFY(stubFile.open(QIODevice::WriteOnly));
        stubFile.write(whole.left(1600));
        stubFile.close();
        QVERIFY(sevenzip::list(stub, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("The file is not a 7z archive"));
    }

    // An entry named to leave the folder is skipped; the rest come out.
    void escapes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(sevenzip::list(fixture("escapes.7z")),
                 QStringList({ QStringLiteral("../../../win64x/retroarch.exe"), QStringLiteral("RetroArch-Win64/kept.txt") }));
        const QString folder = dir.path() + QStringLiteral("/a/b/c");
        QCOMPARE(sevenzip::extract(fixture("escapes.7z"), folder, kPlenty, kPlenty), QString());
        QCOMPARE(contents(folder + QStringLiteral("/RetroArch-Win64/kept.txt")), QByteArray("kept\n"));
        QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/win64x")));
        QVERIFY(!QFileInfo::exists(folder + QStringLiteral("/win64x")));
        QCOMPARE(QDir(dir.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries), QStringList({ QStringLiteral("a") }));
    }
};

QTEST_GUILESS_MAIN(SevenZipTest)
#include "sevenzip_test.moc"
