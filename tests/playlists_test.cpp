#include "modules/playlists/PlaylistsBackend.h"
#include "util/AtomicFile.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

// A playlist's m3u (PlaylistsBackend::prepare): owner-only, written whole,
// the one before removed once it is; one that can't be written (a full card)
// says so, plays nothing, and leaves the one before.
class PlaylistsTest : public QObject {
    Q_OBJECT

private slots:
    void m3u() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString film = data.path() + QStringLiteral("/film.mkv");
        QVERIFY(writeFileAtomically(film, "film"));
        PlaylistsBackend playlists(data.path(), nullptr, nullptr, nullptr, {});
        const QString id = playlists.createPlaylist(QStringLiteral("Films"), QStringLiteral("online"));
        QVERIFY(playlists.addEntry(id, QStringLiteral("com.osdos.local_files"),
                                   { { QStringLiteral("path"), film }, { QStringLiteral("name"), QStringLiteral("Film") } })
                    .value(QStringLiteral("ok")).toBool());
        const QDir folder(data.path() + QStringLiteral("/playlists"));

        const QVariantMap first = playlists.prepare(id);
        QCOMPARE(first.value(QStringLiteral("count")).toInt(), 1);
        const QString path = first.value(QStringLiteral("file")).toString();
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("#EXTM3U\n#EXTINF:-1,Film\n") + film.toUtf8() + '\n');
        QCOMPARE(QFileInfo(path).permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        // Played again: a new one, the one before gone.
        const QString second = playlists.prepare(id).value(QStringLiteral("file")).toString();
        QVERIFY(second != path);
        QCOMPARE(folder.entryList({ QStringLiteral("*.m3u") }, QDir::Files),
                 QStringList({ QFileInfo(second).fileName() }));

        // A full card: nothing can be written, so nothing is played, and the
        // one before stays. (A file size limit, which holds for root too.)
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{};
        QCOMPARE(getrlimit(RLIMIT_FSIZE, &limit), 0);
        const rlimit full{ 1, limit.rlim_max };
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &full), 0);
        const QVariantMap failed = playlists.prepare(id);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &limit), 0);
        QVERIFY(!failed.contains(QStringLiteral("file")));
        QVERIFY(failed.value(QStringLiteral("error")).toString().startsWith(QStringLiteral("The list could not be written")));
        QCOMPARE(folder.entryList({ QStringLiteral("*.m3u") }, QDir::Files),
                 QStringList({ QFileInfo(second).fileName() }));
    }
};

QTEST_GUILESS_MAIN(PlaylistsTest)
#include "playlists_test.moc"
