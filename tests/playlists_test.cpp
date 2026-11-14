#include "AppCore.h"
#include "modules/playlists/MediaServer.h"
#include "modules/playlists/PlaylistsBackend.h"
#include "util/AtomicFile.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

// A playlist's m3u (PlaylistsBackend::prepare): owner-only, written whole,
// the one before removed once it is; one that can't be written (a full card)
// says so, plays nothing, and leaves the one before. And an offline list's
// downloads: only while the module is on, and what a server's download cut
// short by the power going left in its folder cleared as they start.

// A media server signed in to, whose downloads are asked of url.
class StandInServer : public MediaServer {
public:
    QUrl url;
    bool signedIn() const override { return true; }
    QNetworkRequest downloadRequest(const QString &itemId) const override {
        QUrl item = url;
        item.setPath(QStringLiteral("/Items/") + itemId + QStringLiteral("/Download"));
        return QNetworkRequest(item);
    }
    QString streamUrl(const QString &) const override { return {}; }
    void browse(const QString &, QObject *, std::function<void(bool, const QVariantList &)>) override {}
};

class PlaylistsTest : public QObject {
    Q_OBJECT

    static bool exists(const QString &path) { return QFileInfo::exists(path); }

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

    // An offline list's downloads only while the module is on: off, a video
    // put on one waits in the queue; turned on, the queue goes on. (No
    // yt-dlp to run here, so the download it starts fails at once, saying so.)
    void downloadsOnlyWhileOn() {
        QTemporaryDir app;
        QTemporaryDir data;
        QVERIFY(app.isValid() && data.isValid());
        QVERIFY(QDir().mkpath(app.path() + QStringLiteral("/modules/playlists")));
        QVERIFY(writeFileAtomically(app.path() + QStringLiteral("/modules/playlists/manifest.json"),
                                    R"({"id": "com.osdos.playlists", "entry_point_qml": "views/Root.qml",
                                        "settings": [{"key": "enabled", "type": "toggle", "default": "ON"}]})"));
        QVERIFY(writeFileAtomically(data.path() + QStringLiteral("/config.json"),
                                    R"({"modules": {"com.osdos.playlists": {"enabled": false}}})"));
        const QByteArray path = qgetenv("PATH");
        const auto restore = qScopeGuard([&path] { qputenv("PATH", path); });
        qputenv("PATH", data.path().toUtf8());   // no yt-dlp

        AppCore core(app.path(), data.path());
        QVERIFY(!core.is_module_enabled(QStringLiteral("com.osdos.playlists")));
        PlaylistsBackend playlists(data.path(), &core, nullptr, nullptr, {});
        connect(&core, &AppCore::moduleSettingChanged, &playlists, &PlaylistsBackend::onSettingChanged);
        const QString id = playlists.createPlaylist(QStringLiteral("Away"), QStringLiteral("offline"));
        QVERIFY(playlists.addEntry(id, QStringLiteral("com.osdos.youtube"),
                                   { { QStringLiteral("videoId"), QStringLiteral("aqz-KE-bpKQ") },
                                     { QStringLiteral("title"), QStringLiteral("Film") } })
                    .value(QStringLiteral("ok")).toBool());
        const auto state = [&]() {
            return playlists.playlist(id).value(QStringLiteral("items")).toList().value(0).toMap();
        };
        QTest::qWait(200);
        QCOMPARE(state().value(QStringLiteral("state")).toString(), QStringLiteral("queued"));

        QVERIFY(core.save_setting(QStringLiteral("com.osdos.playlists"), QStringLiteral("enabled"), true));
        QTRY_COMPARE(state().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(state().value(QStringLiteral("reason")).toString(), QStringLiteral("no yt-dlp"));
    }

    // No download beside a game: while one plays (setHeld(), which main.cpp
    // sets from the Arcade module's running) a video put on an offline list
    // waits in the queue, and the queue goes on once the game has ended.
    void downloadsWaitForAGame() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QByteArray path = qgetenv("PATH");
        const auto restore = qScopeGuard([&path] { qputenv("PATH", path); });
        qputenv("PATH", data.path().toUtf8());   // no yt-dlp: a download that starts fails at once

        PlaylistsBackend playlists(data.path(), nullptr, nullptr, nullptr, {});
        playlists.setHeld(true);
        const QString id = playlists.createPlaylist(QStringLiteral("Away"), QStringLiteral("offline"));
        QVERIFY(playlists.addEntry(id, QStringLiteral("com.osdos.youtube"),
                                   { { QStringLiteral("videoId"), QStringLiteral("aqz-KE-bpKQ") },
                                     { QStringLiteral("title"), QStringLiteral("Film") } })
                    .value(QStringLiteral("ok")).toBool());
        const auto state = [&]() {
            return playlists.playlist(id).value(QStringLiteral("items")).toList().value(0).toMap();
        };
        QTest::qWait(200);
        QCOMPARE(state().value(QStringLiteral("state")).toString(), QStringLiteral("queued"));
        playlists.setHeld(false);
        QTRY_COMPARE(state().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    }

    // What a server's download cut short by the power going left in its
    // folder, the temporary file it was writing (the film's name with six
    // letters after it, where the filesystem has no unnamed files: exFAT),
    // is deleted as the downloads start. The films themselves, a file of the
    // user's own and yt-dlp's part file, which it carries on from, stay.
    void leftoversOfADownloadCutShortAreCleared() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString folder = data.path() + QStringLiteral("/media/Playlists");
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/Jellyfin")));
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/YouTube")));
        const QString film = folder + QStringLiteral("/Jellyfin/Film [abc123].mkv");
        const QStringList left{ film + QStringLiteral(".RkYmKP"),
                                folder + QStringLiteral("/Jellyfin/Another Film [def456].mp4.MZbRcq") };
        const QStringList kept{ film,
                                folder + QStringLiteral("/Jellyfin/Holiday.mkv.backup"),
                                folder + QStringLiteral("/Jellyfin/Notes [mine].txt"),
                                folder + QStringLiteral("/YouTube/Clip [aqz-KE-bpKQ].mp4.part") };
        for (const QString &path : left + kept)
            QVERIFY(writeFileAtomically(path, "film"));

        StandInServer server;
        PlaylistsBackend playlists(data.path(), nullptr, nullptr, nullptr,
                                   { { QStringLiteral("com.osdos.jellyfin"), &server } });
        QCOMPARE(playlists.downloadFolder(), folder);
        // As 15 s after the start, or as the module is turned on.
        playlists.queueOfflineItems();
        for (const QString &path : left)
            QVERIFY2(!exists(path), qPrintable(path));
        for (const QString &path : kept)
            QVERIFY2(exists(path), qPrintable(path));
    }
};

QTEST_GUILESS_MAIN(PlaylistsTest)
#include "playlists_test.moc"
