#include "AppCore.h"
#include "modules/playlists/MediaServer.h"
#include "modules/playlists/PlaylistsBackend.h"
#include "util/AtomicFile.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>

// A playlist's m3u (PlaylistsBackend::prepare): owner-only, written whole,
// the one before removed once it is; one that can't be written (a full card)
// says so, plays nothing, and leaves the one before. And an offline list's
// downloads: only while the module is on, what a server's download cut
// short by the power going left in its folder cleared as they start, and
// one that failed for the network tried again once a while has passed.

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

        // A playlist of Local Files' own isn't put on one: mpv would play it
        // as the videos in it, and the places after it would be off.
        for (const QString &name : { QStringLiteral("mix.m3u"), QStringLiteral("Mix.M3U8") }) {
            const QString list = data.path() + QLatin1Char('/') + name;
            QVERIFY(writeFileAtomically(list, "#EXTM3U\n" + film.toUtf8() + "\n" + film.toUtf8() + "\n"));
            QCOMPARE(playlists.addEntry(id, QStringLiteral("com.osdos.local_files"),
                                        { { QStringLiteral("path"), list }, { QStringLiteral("name"), name } })
                         .value(QStringLiteral("reason")).toString(),
                     QStringLiteral("unsupported"));
        }
        QCOMPARE(playlists.prepare(id).value(QStringLiteral("count")).toInt(), 1);
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

    // A YouTube download's part files are cleared where it ran, though the
    // Download Folder has been changed since: as a download that failed is
    // taken off its lists, and as one is started again in the new folder,
    // which it can't carry on from the old one in. A file of the user's own
    // there stays. (No yt-dlp to run here, so each download fails at once.)
    void leftoversInAnEarlierDownloadFolderAreCleared() {
        QTemporaryDir app;
        QTemporaryDir data;
        QVERIFY(app.isValid() && data.isValid());
        QVERIFY(QDir().mkpath(app.path() + QStringLiteral("/modules/playlists")));
        QVERIFY(writeFileAtomically(app.path() + QStringLiteral("/modules/playlists/manifest.json"),
                                    R"({"id": "com.osdos.playlists", "entry_point_qml": "views/Root.qml",
                                        "settings": [{"key": "enabled", "type": "toggle", "default": "ON"},
                                                     {"key": "download_folder", "type": "directory_browser", "default": ""}]})"));
        const QString before = data.path() + QStringLiteral("/before");
        const QString after = data.path() + QStringLiteral("/after");
        QVERIFY(writeFileAtomically(data.path() + QStringLiteral("/config.json"),
                                    QStringLiteral(R"({"modules": {"com.osdos.playlists": {"download_folder": "%1"}}})")
                                        .arg(before).toUtf8()));
        const QByteArray path = qgetenv("PATH");
        const auto restore = qScopeGuard([&path] { qputenv("PATH", path); });
        qputenv("PATH", data.path().toUtf8());   // no yt-dlp

        AppCore core(app.path(), data.path());
        PlaylistsBackend playlists(data.path(), &core, nullptr, nullptr, {});
        QCOMPARE(playlists.downloadFolder(), before);
        const auto add = [&](const QString &list, const QString &videoId) {
            return playlists.addEntry(list, QStringLiteral("com.osdos.youtube"),
                                      { { QStringLiteral("videoId"), videoId },
                                        { QStringLiteral("title"), videoId } })
                .value(QStringLiteral("ok")).toBool();
        };
        const auto state = [&](const QString &list) {
            return playlists.playlist(list).value(QStringLiteral("items")).toList().value(0)
                .toMap().value(QStringLiteral("state")).toString();
        };
        const QString gone = playlists.createPlaylist(QStringLiteral("Gone"), QStringLiteral("offline"));
        const QString again = playlists.createPlaylist(QStringLiteral("Again"), QStringLiteral("offline"));
        QVERIFY(add(gone, QStringLiteral("aqz-KE-bpKQ")));
        QVERIFY(add(again, QStringLiteral("dQw4w9WgXcQ")));
        QTRY_COMPARE(state(gone), QStringLiteral("failed"));
        QTRY_COMPARE(state(again), QStringLiteral("failed"));
        // What yt-dlp got through, as it would have left it.
        const QStringList left{ before + QStringLiteral("/YouTube/aqz-KE-bpKQ [aqz-KE-bpKQ].mp4.part"),
                                before + QStringLiteral("/YouTube/dQw4w9WgXcQ [dQw4w9WgXcQ].f137.mp4") };
        const QString mine = before + QStringLiteral("/YouTube/Notes [mine].txt");
        QVERIFY(QDir().mkpath(before + QStringLiteral("/YouTube")));
        for (const QString &file : left + QStringList{ mine })
            QVERIFY(writeFileAtomically(file, "film"));

        QVERIFY(core.save_setting(QStringLiteral("com.osdos.playlists"), QStringLiteral("download_folder"), after));
        QCOMPARE(playlists.downloadFolder(), after);
        playlists.deletePlaylist(gone);
        QVERIFY2(!exists(left.at(0)), qPrintable(left.at(0)));
        QVERIFY(exists(left.at(1)));
        playlists.queueOfflineItems();
        QTRY_VERIFY2(!exists(left.at(1)), qPrintable(left.at(1)));
        QTRY_COMPARE(state(again), QStringLiteral("failed"));
        QVERIFY(exists(mine));
    }

    // A download that fails for the network (nothing answers: the router not
    // back yet after a power cut) is tried again after a while, a longer
    // while each time, and is on the device once the network is back, with
    // no start of the app in between. One the server refuses is not tried
    // again by itself.
    void downloadThatFailedForTheNetworkIsTriedAgain() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        // The network: down at first (whoever connects is dropped), then the
        // server, which refuses one of its items.
        QTcpServer network;
        QVERIFY(network.listen(QHostAddress::LocalHost));
        bool up = false;
        int dropped = 0;
        QStringList answered;
        connect(&network, &QTcpServer::newConnection, &network, [&]() {
            while (QTcpSocket *socket = network.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                if (!up) {
                    ++dropped;
                    socket->abort();
                    continue;
                }
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &answered]() {
                    if (socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    const QString path = QString::fromLatin1(socket->readAll().split(' ').value(1));
                    answered << path;
                    socket->write(path.contains(QLatin1String("/refused/"))
                                      ? QByteArray("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                                  : path.contains(QLatin1String("/missing/"))
                                      ? QByteArray("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                                      : QByteArray("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: 4\r\n"
                                                   "Connection: close\r\n\r\nfilm"));
                    socket->disconnectFromHost();
                });
            }
        });
        StandInServer server;
        server.url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(network.serverPort()));
        PlaylistsBackend playlists(data.path(), nullptr, nullptr, nullptr,
                                   { { QStringLiteral("com.osdos.jellyfin"), &server } });
        // A tenth of a second in place of the minute.
        playlists.m_retryMs = 100;
        const QString id = playlists.createPlaylist(QStringLiteral("Away"), QStringLiteral("offline"));
        const auto add = [&](const char *itemId, const char *title) {
            return playlists.addEntry(id, QStringLiteral("com.osdos.jellyfin"),
                                      { { QStringLiteral("itemId"), QString::fromLatin1(itemId) },
                                        { QStringLiteral("title"), QString::fromLatin1(title) } })
                .value(QStringLiteral("ok")).toBool();
        };
        const auto item = [&](int at, const char *field) {
            return playlists.playlist(id).value(QStringLiteral("items")).toList().value(at).toMap()
                .value(QString::fromLatin1(field)).toString();
        };
        QVERIFY(add("abc123", "Film"));
        QVERIFY(add("refused", "Refused"));
        QVERIFY(add("missing", "Missing"));

        // Nothing answers: both fail, saying what the network did.
        QTRY_COMPARE_WITH_TIMEOUT(item(0, "state"), QStringLiteral("failed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(item(1, "state"), QStringLiteral("failed"), 10000);
        QVERIFY(item(0, "reason") != QLatin1String("not allowed"));
        // In a few words, never the address asked: shown, saved and logged.
        QVERIFY2(!item(0, "reason").isEmpty() && !item(0, "reason").contains(QLatin1String("127.0.0.1"))
                     && !item(0, "reason").contains(QLatin1String("abc123")),
                 qPrintable(item(0, "reason")));
        QVERIFY(answered.isEmpty());
        // Tried again a while later, the network still down: failed again,
        // and the next wait is a longer one.
        const int before = dropped;
        QTRY_VERIFY_WITH_TIMEOUT(dropped > before, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(playlists.m_retryMs > 100, 10000);
        QVERIFY(answered.isEmpty());

        // The network back: the film is on the device at the next try.
        up = true;
        QTRY_COMPARE_WITH_TIMEOUT(item(0, "state"), QStringLiteral("ready"), 30000);
        QFile film(data.path() + QStringLiteral("/media/Playlists/Jellyfin/Film [abc123].mp4"));
        QVERIFY(film.open(QIODevice::ReadOnly));
        QCOMPARE(film.readAll(), QByteArray("film"));
        // What the server refuses is asked for once, says so, and is left
        // for Retry Downloads: nothing waits to be tried again.
        QTRY_COMPARE_WITH_TIMEOUT(item(1, "reason"), QStringLiteral("not allowed"), 10000);
        QCOMPARE(answered.count(QStringLiteral("/Items/refused/Download")), 1);
        // What the server hasn't: its status, not Qt's text with the address.
        QTRY_COMPARE_WITH_TIMEOUT(item(2, "reason"), QStringLiteral("HTTP 404 Not Found"), 10000);
        QCOMPARE(answered.count(QStringLiteral("/Items/missing/Download")), 1);
        QVERIFY(playlists.m_retryKeys.isEmpty());
        QVERIFY(!playlists.m_retryTimer->isActive());
        // A download made: the next failure waits the first while again.
        QCOMPARE(playlists.m_retryMs, 60 * 1000);
        QCOMPARE(playlists.m_downloads.value(QStringLiteral("jellyfin:abc123")).keys(),
                 QStringList({ QStringLiteral("path"), QStringLiteral("state") }));
    }

    // A playlists.json an older version wrote, a finished download's size
    // beside its path: the video is on the device and plays from its file,
    // never fetched again; one whose file has gone is queued again.
    void downloadsOfAnOlderFile() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString film = data.path() + QStringLiteral("/Film [abc123].mp4");
        QVERIFY(writeFileAtomically(film, "film"));
        const QByteArray file = QStringLiteral(R"({
            "downloads": {
                "jellyfin:abc123": { "bytes": 4, "path": "%1", "state": "done" },
                "jellyfin:gone": { "bytes": 4, "path": "%2", "state": "done" }
            },
            "playlists": [ { "id": "list", "name": "Away", "kind": "offline", "order": "inorder", "items": [
                { "id": "one", "key": "jellyfin:abc123", "module": "com.osdos.jellyfin",
                  "source": { "itemId": "abc123" }, "title": "Film" },
                { "id": "two", "key": "jellyfin:gone", "module": "com.osdos.jellyfin",
                  "source": { "itemId": "gone" }, "title": "Gone" } ] } ]
        })").arg(film, data.path() + QStringLiteral("/Gone [gone].mp4")).toUtf8();
        QVERIFY(writeFileAtomically(data.path() + QStringLiteral("/playlists.json"), file));
        PlaylistsBackend playlists(data.path(), nullptr, nullptr, nullptr, {});
        const QVariantList items = playlists.playlist(QStringLiteral("list")).value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.value(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(items.value(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("queued"));
        QCOMPARE(playlists.prepare(QStringLiteral("list")).value(QStringLiteral("count")).toInt(), 1);
        QVERIFY(!playlists.enqueue(QStringLiteral("jellyfin:abc123")));
        QVERIFY(playlists.enqueue(QStringLiteral("jellyfin:gone")));
        QCOMPARE(playlists.m_queue, QStringList({ QStringLiteral("jellyfin:gone") }));
    }
};

QTEST_GUILESS_MAIN(PlaylistsTest)
#include "playlists_test.moc"
