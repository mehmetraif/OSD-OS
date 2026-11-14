// The menu music (MenuMusic) against a stand-in for mpv: a shell script first
// on PATH that writes down when it starts and what, and stays until killed
// (or, told to fail, ends at once, as mpv does on a file it can't play, and
// with no sound card to play through). And stand-ins for FluidSynth and
// openmpt123, which write down what they were asked to make and make a file
// of it, and for ALSA's list of sound cards (OSDOS_ASOUND_DIR).
#include "audio/MenuMusic.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

static const char kStandIn[] = R"(#!/bin/sh
log="$FAKE_MPV_LOG"
media=""
for a in "$@"; do media="$a"; done
echo "start $(basename "$media") $$" >> "$log"
if [ "$FAKE_MPV_MODE" = fail ]; then exit 2; fi
if [ "$FAKE_MPV_MODE" = card ] && ! grep -q '^ *[0-9]' "$OSDOS_ASOUND_DIR/cards"; then exit 2; fi
case "$media" in *.xm) if [ "$FAKE_MPV_MODULE" = fail ]; then exit 2; fi ;; esac
i=0
while [ $i -lt 600 ]; do sleep 0.05; i=$((i+1)); done
)";

// FluidSynth's (-F file) and openmpt123's (-o file) stand-in: the file it
// writes follows OUT_FLAG.
static QByteArray renderer(const char *name, const char *outFlag) {
    return QByteArray(R"(#!/bin/sh
out=""
prev=""
for a in "$@"; do
  if [ "$prev" = ")") + outFlag + R"(" ]; then out="$a"; fi
  prev="$a"
done
echo "render )" + name + R"( $*" >> "$FAKE_MPV_LOG"
if [ -n "$FAKE_RENDER_DELAY" ]; then sleep "$FAKE_RENDER_DELAY"; fi
if [ "$FAKE_RENDER_MODE" = fail ]; then exit 1; fi
printf 'RIFF0000WAVEfmt ' > "$out"
)";
}

class MenuMusicTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_log;
    QString m_tune;
    MenuMusic *m_music = nullptr;

    QStringList logged(const QString &what) const {
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return lines.filter(QRegularExpression(QLatin1Char('^') + what));
    }
    QStringList starts() const { return logged(QStringLiteral("start ")); }
    QStringList renders() const { return logged(QStringLiteral("render ")); }
    static bool writeFile(const QString &path, const QByteArray &data) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    static bool writeScript(const QString &path, const QByteArray &script) {
        QFile f(path);
        return writeFile(path, script)
            && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QString cache() const { return m_dir.path() + QStringLiteral("/cache"); }
    // ALSA's list of cards, as /proc/asound has it: a USB sound card that
    // can play, or none.
    bool soundCard(bool there) const {
        const QString asound = m_dir.path() + QStringLiteral("/asound");
        return QDir().mkpath(asound + QStringLiteral("/card1/pcm0p"))
            && writeFile(asound + QStringLiteral("/cards"),
                         there ? QByteArray(" 1 [Device         ]: USB-Audio - USB Audio Device\n"
                                            "                      USB Audio Device at usb-xhci-hcd.0-1, full speed\n")
                               : QByteArray("--- no soundcards ---\n"));
    }
    // What the cache holds of the menu music's.
    QStringList made() const {
        QStringList files;
        QDirIterator it(cache(), { QStringLiteral("*.wav") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << QFileInfo(it.next()).fileName();
        return files;
    }
    // Whether the last stand-in started is still there.
    bool lastAlive() const {
        const QStringList all = starts();
        if (all.isEmpty())
            return false;
        const QString pid = all.last().section(QLatin1Char(' '), 2, 2);
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        return stat.open(QIODevice::ReadOnly) && !QString::fromLatin1(stat.readAll()).contains(QStringLiteral(") Z "));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        const QString bin = m_dir.path() + QStringLiteral("/bin");
        QVERIFY(QDir().mkpath(bin));
        QVERIFY(writeScript(bin + QStringLiteral("/mpv"), kStandIn));
        QVERIFY(writeScript(bin + QStringLiteral("/fluidsynth"), renderer("fluidsynth", "-F")));
        QVERIFY(writeScript(bin + QStringLiteral("/openmpt123"), renderer("openmpt123", "-o")));
        qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
        qputenv("TMPDIR", m_dir.path().toUtf8());
        qputenv("XDG_CACHE_HOME", cache().toUtf8());
        // The sound cards are the test's own, whatever the machine has, with
        // no sound server to pick one in ALSA's place.
        qputenv("OSDOS_ASOUND_DIR", (m_dir.path() + QStringLiteral("/asound")).toUtf8());
        qputenv("XDG_RUNTIME_DIR", m_dir.path().toUtf8());
        qunsetenv("PULSE_SERVER");
        m_log = m_dir.path() + QStringLiteral("/mpv.log");
        qputenv("FAKE_MPV_LOG", m_log.toUtf8());
        m_tune = m_dir.path() + QStringLiteral("/tune.ogg");
        QFile tune(m_tune);
        QVERIFY(tune.open(QIODevice::WriteOnly));
    }

    void init() {
        QFile::remove(m_log);
        QDir(cache()).removeRecursively();
        qputenv("FAKE_MPV_MODE", "play");
        qputenv("FAKE_MPV_MODULE", "play");
        qputenv("FAKE_RENDER_MODE", "make");
        qputenv("FAKE_RENDER_DELAY", "");
        QVERIFY(soundCard(true));
        m_music = new MenuMusic(m_dir.path() + QStringLiteral("/data"));
    }

    void cleanup() {
        delete m_music;
        m_music = nullptr;
    }

    // Wanted, with a tune: it plays, a moment later.
    void playsWhenWanted() {
        m_music->setSource(QUrl::fromLocalFile(m_tune).toString());
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QVERIFY(starts().first().startsWith(QStringLiteral("start tune.ogg")));
        QVERIFY(m_music->playing());
        m_music->setWanted(false);
        QVERIFY(!m_music->playing());
        QTRY_VERIFY_WITH_TIMEOUT(!lastAlive(), 1000);
    }

    // Something about to play sound holds it off: gone by the time hold()
    // returns, so the sound card is free; back once released.
    void holdStopsAtOnce() {
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_music->playing(), 3000);
        QElapsedTimer held;
        held.start();
        MenuMusic::hold(QStringLiteral("video"));
        QVERIFY(held.elapsed() < 350);
        QVERIFY(!m_music->playing());
        QVERIFY(!lastAlive());
        // Held, it stays quiet however long.
        QTest::qWait(1200);
        QCOMPARE(starts().size(), 1);
        MenuMusic::release(QStringLiteral("video"));
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 2, 3000);
        QVERIFY(m_music->playing());
    }

    // A hold and its release in quick turns don't start it in between, and
    // two holders keep it off until both let go.
    void holdsAddUp() {
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_music->playing(), 3000);
        MenuMusic::hold(QStringLiteral("a"));
        MenuMusic::hold(QStringLiteral("b"));
        MenuMusic::release(QStringLiteral("a"));
        QTest::qWait(1200);
        QVERIFY(!m_music->playing());
        QCOMPARE(starts().size(), 1);
        MenuMusic::release(QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(m_music->playing(), 3000);
    }

    // A file mpv can't play isn't tried again and again.
    void failedFileNotRetried() {
        qputenv("FAKE_MPV_MODE", "fail");
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_music->playing(), 2000);
        MenuMusic::hold(QStringLiteral("x"));
        MenuMusic::release(QStringLiteral("x"));
        QTest::qWait(1500);
        QCOMPARE(starts().size(), 1);
        // Another source is tried.
        qputenv("FAKE_MPV_MODE", "play");
        m_music->setSource(m_dir.path() + QStringLiteral("/bin/../tune.ogg"));
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 2, 3000);
    }

    // No sound card to play through (a USB card unplugged, or not up yet as
    // the Pi starts): mpv ends at once, as on a file it can't play, but the
    // file isn't given up on, nor started over and over; it plays once a
    // card is there.
    void waitsForASoundCard() {
        QVERIFY(soundCard(false));
        qputenv("FAKE_MPV_MODE", "card");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
                                               "^\\[MenuMusic\\] no sound card to play .*/tune\\.ogg through")));
        QTest::failOnWarning(QRegularExpression(QStringLiteral("couldn't play")));
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_music->playing(), 2000);
        QTest::qWait(2500);
        QCOMPARE(starts().size(), 1);
        QVERIFY(soundCard(true));
        QTRY_VERIFY_WITH_TIMEOUT(m_music->playing(), 5000);
        QCOMPARE(starts().size(), 2);
    }

    // A tune given up on is tried once more on another sound card (Settings →
    // Sound → Audio Output): the card before may be what couldn't play it.
    void anotherCardTriesAgain() {
        qputenv("FAKE_MPV_MODE", "fail");
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(m_music->findChildren<QProcess *>().isEmpty(), 3000);
        qputenv("FAKE_MPV_MODE", "play");
        m_music->restart();
        QTRY_VERIFY_WITH_TIMEOUT(m_music->playing(), 3000);
        QCOMPARE(starts().size(), 2);
    }

    // A MIDI file: FluidSynth makes it into a WAV with the SoundFont beside
    // it, and mpv plays that; the next time, the WAV made then.
    void midiMadeIntoWav() {
        const QString midi = m_dir.path() + QStringLiteral("/song.mid");
        QVERIFY(writeFile(midi, "MThd"));
        QVERIFY(writeFile(m_dir.path() + QStringLiteral("/song.sf2"), "sfbk"));
        m_music->setSource(midi);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QCOMPARE(renders().size(), 1);
        QVERIFY(renders().first().startsWith(QStringLiteral("render fluidsynth ")));
        QVERIFY(renders().first().contains(QStringLiteral("/song.sf2 ")));
        QVERIFY(renders().first().endsWith(QStringLiteral("/song.mid")));
        QVERIFY(starts().first().contains(QStringLiteral(".wav ")));
        QCOMPARE(made().size(), 1);
        m_music->setWanted(false);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 2, 3000);
        QCOMPARE(renders().size(), 1);
    }

    // No SoundFont beside it: the first in the data folder's soundfonts.
    void dataFolderSoundFont() {
        const QString fonts = m_dir.path() + QStringLiteral("/data/soundfonts");
        QVERIFY(QDir().mkpath(fonts));
        QVERIFY(writeFile(fonts + QStringLiteral("/b.sf2"), "sfbk"));
        QVERIFY(writeFile(fonts + QStringLiteral("/a.sf2"), "sfbk"));
        const QString midi = m_dir.path() + QStringLiteral("/lone.mid");
        QVERIFY(writeFile(midi, "MThd"));
        m_music->setSource(midi);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QVERIFY(renders().first().contains(QStringLiteral("/soundfonts/a.sf2 ")));
    }

    // FluidSynth failing (a moment after it starts): nothing played, and not
    // tried again until the source changes.
    void renderFails() {
        qputenv("FAKE_RENDER_MODE", "fail");
        qputenv("FAKE_RENDER_DELAY", "0.3");
        const QString midi = m_dir.path() + QStringLiteral("/bad.mid");
        QVERIFY(writeFile(midi, "MThd"));
        QVERIFY(writeFile(m_dir.path() + QStringLiteral("/bad.sf2"), "sfbk"));
        m_music->setSource(midi);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(renders().size(), 1, 3000);
        // Its failure taken in (the process gone) first: held while it still
        // ran, it would be stopped, not failed, and made again on release.
        QTRY_VERIFY_WITH_TIMEOUT(m_music->findChildren<QProcess *>().isEmpty(), 3000);
        MenuMusic::hold(QStringLiteral("x"));
        MenuMusic::release(QStringLiteral("x"));
        QTest::qWait(1500);
        QCOMPARE(renders().size(), 1);
        QVERIFY(starts().isEmpty());
        QVERIFY(made().isEmpty());
    }

    // Held while FluidSynth works: it is stopped, nothing half made is kept,
    // and it is made again once nothing holds the music.
    void holdStopsRendering() {
        qputenv("FAKE_RENDER_DELAY", "1");
        const QString midi = m_dir.path() + QStringLiteral("/slow.mid");
        QVERIFY(writeFile(midi, "MThd"));
        QVERIFY(writeFile(m_dir.path() + QStringLiteral("/slow.sf2"), "sfbk"));
        m_music->setSource(midi);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(renders().size(), 1, 3000);
        MenuMusic::hold(QStringLiteral("video"));
        QTest::qWait(1500);
        QVERIFY(starts().isEmpty());
        QVERIFY(made().isEmpty());
        MenuMusic::release(QStringLiteral("video"));
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 5000);
        QCOMPARE(renders().size(), 2);
    }

    // A tracker's module plays as it is; one mpv can't play, openmpt123
    // makes into a WAV for it.
    void modules() {
        const QString xm = m_dir.path() + QStringLiteral("/tune.xm");
        QVERIFY(writeFile(xm, "Extended Module: "));
        m_music->setSource(xm);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QVERIFY(starts().first().startsWith(QStringLiteral("start tune.xm ")));
        QVERIFY(renders().isEmpty());

        qputenv("FAKE_MPV_MODULE", "fail");
        const QString other = m_dir.path() + QStringLiteral("/other.xm");
        QVERIFY(writeFile(other, "Extended Module: "));
        m_music->setSource(other);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 3, 5000);
        QVERIFY(starts().at(1).startsWith(QStringLiteral("start other.xm ")));
        QCOMPARE(renders().size(), 1);
        QVERIFY(renders().first().startsWith(QStringLiteral("render openmpt123 ")));
        QVERIFY(renders().first().endsWith(QStringLiteral("-- ") + other));
        QVERIFY(starts().at(2).contains(QStringLiteral(".wav ")));
        QVERIFY(m_music->playing());
    }

    // A name with a ? or a # in it, which a URL would take for its query or
    // fragment, and a %: played as it is, given as a path or as a URL.
    void oddNames() {
        const QString odd = m_dir.path() + QStringLiteral("/why?#1%25.ogg");
        QVERIFY(writeFile(odd, "OggS"));
        m_music->setSource(odd);
        m_music->setWanted(true);
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        QVERIFY(starts().at(0).startsWith(QStringLiteral("start why?#1%25.ogg ")));
        m_music->setSource(QUrl::fromLocalFile(odd).toString());
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 2, 3000);
        QVERIFY(starts().at(1).startsWith(QStringLiteral("start why?#1%25.ogg ")));
    }

    // An mpv that can't even be started (FailedToStart, no finished()): said
    // once, nothing of it left behind, and not tried again until the source
    // changes.
    void unstartable() {
        const QString mpv = m_dir.path() + QStringLiteral("/bin/mpv");
        QVERIFY(writeScript(mpv, "#!/nonexistent/sh\n"));
        const auto restore = qScopeGuard([mpv] { writeScript(mpv, kStandIn); });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^\\[MenuMusic\\] mpv couldn't play .*/tune\\.ogg")));
        QTest::failOnWarning(QRegularExpression(QStringLiteral("couldn't play")));
        m_music->setSource(m_tune);
        m_music->setWanted(true);
        QTest::qWait(1500);
        QVERIFY(!m_music->playing());
        MenuMusic::hold(QStringLiteral("x"));
        MenuMusic::release(QStringLiteral("x"));
        QTest::qWait(1500);
        QVERIFY(!m_music->playing());
        QTRY_VERIFY_WITH_TIMEOUT(m_music->findChildren<QProcess *>().isEmpty(), 1000);
    }

    // No file there: nothing started.
    void missingFile() {
        m_music->setSource(m_dir.path() + QStringLiteral("/gone.ogg"));
        m_music->setWanted(true);
        QTest::qWait(1200);
        QVERIFY(starts().isEmpty());
    }
};

QTEST_GUILESS_MAIN(MenuMusicTest)
#include "menu_music_test.moc"
