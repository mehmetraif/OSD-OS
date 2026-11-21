#include "AppCore.h"
#include "modules/web_player/TmdbCatalog.h"
#include "modules/web_player/WebPlayerBackend.h"
#include "modules/youtube/YouTubeBackend.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

// The subscriptions' feeds against a stand-in for YouTube's: asked for four
// at a time however many channels there are, the next as each lands, the
// channel opened ahead of those still waiting and answered as its own feed
// lands, and every channel's videos in the feed once all have been read.
// The channels and playlists typed in Settings, beside the files'.
class YouTubeTest : public QObject {
    Q_OBJECT

private slots:
    // Netflix's and Prime Video's Catalog Region where none was chosen: the
    // time zone's country when it is one of those offered, else the USA;
    // offered by name, each saved as its code, the one to start from marked.
    void catalogRegionFromTheTimeZone() {
        QCOMPARE(WebPlayerBackend::defaultRegion(QStringLiteral("Europe/Istanbul")), QStringLiteral("TR"));
        QCOMPARE(WebPlayerBackend::defaultRegion(QStringLiteral("Europe/London")), QStringLiteral("GB"));
        QCOMPARE(WebPlayerBackend::defaultRegion(QStringLiteral("America/Toronto")), QStringLiteral("CA"));
        QCOMPARE(WebPlayerBackend::defaultRegion(QStringLiteral("Asia/Tokyo")), QStringLiteral("US"));
        QCOMPARE(WebPlayerBackend::defaultRegion(QStringLiteral("UTC")), QStringLiteral("US"));

        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/etc")));
        QFile zone(root.path() + QStringLiteral("/etc/timezone"));
        QVERIFY(zone.open(QIODevice::WriteOnly));
        zone.write("Europe/Berlin\n");
        zone.close();
        qputenv("OSDOS_SYSTEM_ROOT", root.path().toUtf8());
        QTemporaryDir data;
        WebPlayerBackend::Service service{};
        service.id = QStringLiteral("netflix");
        WebPlayerBackend netflix(service, data.path(), data.path(), nullptr, nullptr);
        QSignalSpy options(&netflix, &WebPlayerBackend::dynamicOptionsReady);
        netflix.get_regions();
        qunsetenv("OSDOS_SYSTEM_ROOT");
        QCOMPARE(options.count(), 1);
        QCOMPARE(options.first().at(0).toString(), QStringLiteral("region"));
        QStringList ids, labels, marked;
        for (const QVariant &v : options.first().at(1).toList()) {
            const QVariantMap choice = v.toMap();
            ids << choice.value("id").toString();
            labels << choice.value("label").toString();
            if (choice.value("default").toBool())
                marked << choice.value("id").toString();
        }
        QCOMPARE(ids, QStringList({ "AU", "CA", "FR", "DE", "IT", "NL", "ES", "TR", "GB", "US" }));
        QCOMPARE(labels.at(8), QStringLiteral("UK"));
        QCOMPARE(marked, QStringList{ QStringLiteral("DE") });
    }

    // Subtitles' choices in every module's order, With Auto named for what it
    // adds; and the subtitles yt-dlp is asked for: those in Subtitle
    // Language, or with Any (unset, as an install starts) whatever the
    // video has, its automatic captions only in its own language.
    void subtitlesAndAnyLanguage() {
        QTemporaryDir data;
        YouTubeBackend youtube(data.path(), data.path(), nullptr, nullptr);
        QSignalSpy options(&youtube, &YouTubeBackend::dynamicOptionsReady);
        youtube.get_subtitle_options();
        youtube.get_subtitle_languages();
        QCOMPARE(options.count(), 2);
        QStringList labels, ids;
        for (const QVariant &v : options.at(0).at(1).toList()) {
            labels << v.toMap().value("label").toString();
            ids << v.toMap().value("id").toString();
        }
        QCOMPARE(labels, QStringList({ "Off", "On", "On + Auto Captions" }));
        QCOMPARE(ids, QStringList({ "Off", "On", "With Auto" }));
        const QVariantMap any = options.at(1).at(1).toList().first().toMap();
        QCOMPARE(any.value("id").toString(), QStringLiteral("-"));
        QCOMPARE(any.value("label").toString(), QStringLiteral("Any"));
        QCOMPARE(youtube.playbackSettings().value("subtitleLanguage").toString(), QStringLiteral("-"));

        const auto raw = [&youtube](const QString &subtitles, const QString &language) {
            const QStringList args = youtube.playbackArgs({ { "subtitles", subtitles }, { "subtitleLanguage", language } });
            for (const QString &arg : args) {
                if (arg.startsWith(QLatin1String("--ytdl-raw-options=")))
                    return arg.mid(19);
            }
            return QString();
        };
        QCOMPARE(raw("Off", "-"), QString());
        QCOMPARE(raw("On", "fr"), QStringLiteral("write-subs=,sub-langs=fr.*"));
        QCOMPARE(raw("With Auto", "fr"), QStringLiteral("write-subs=,sub-langs=fr.*,write-auto-subs="));
        QCOMPARE(raw("On", "-"), QStringLiteral("write-subs=,sub-langs=all,-live_chat"));
        QCOMPARE(raw("On", ""), QStringLiteral("write-subs=,sub-langs=all,-live_chat"));
        QCOMPARE(raw("With Auto", "-"), QStringLiteral("write-subs=,sub-langs=.*-orig,write-auto-subs="));
    }

    // SIGN IN's state: signed in while SIGN IN's browser keeps its profile,
    // signed out once Sign Out has deleted it, which says so.
    void signedInWhileTheProfileIsKept() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        YouTubeBackend youtube(data.path(), data.path(), nullptr, nullptr);
        QSignalSpy changed(&youtube, &YouTubeBackend::authStateChanged);
        QCOMPARE(youtube.get_auth_state(), QStringLiteral("none"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/youtube/browser/Default")));
        QCOMPARE(youtube.get_auth_state(), QStringLiteral("authed"));
        youtube.signOut();
        QCOMPARE(youtube.get_auth_state(), QStringLiteral("none"));
        QCOMPARE(changed.count(), 1);
    }

    void channelsFourAtOnce() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QStringList ids;
        for (int i = 0; i < 10; ++i)
            ids << QStringLiteral("UCchannel%1").arg(i);
        QFile subscriptions(data.path() + QStringLiteral("/youtube_subscriptions.txt"));
        QVERIFY(subscriptions.open(QIODevice::WriteOnly));
        subscriptions.write(ids.join(QLatin1Char('\n')).toUtf8() + '\n');
        subscriptions.close();

        // The stand-in: each request held until answered, the channel it asks
        // for noted, and the most held at once.
        struct Held {
            QPointer<QTcpSocket> socket;
            QString channel;
        };
        QList<Held> held;
        QStringList asked;
        qsizetype most = 0;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n"))
                    return;
                static const QRegularExpression channel(QStringLiteral("channel_id=([A-Za-z0-9_-]+)"));
                const QString id = channel.match(QString::fromLatin1(request)).captured(1);
                asked << id;
                held << Held{ socket, id };
                most = std::max(most, held.size());
            });
        });
        const auto answer = [&held](const QString &id) {
            const auto it = std::find_if(held.begin(), held.end(), [&id](const Held &h) { return h.channel == id; });
            if (it == held.end())
                return false;
            const QByteArray feed = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<feed xmlns:yt=\"http://www.youtube.com/xml/schemas/2015\" xmlns=\"http://www.w3.org/2005/Atom\">\n"
                " <title>Channel %1</title>\n"
                " <entry>\n"
                "  <yt:videoId>v-%1</yt:videoId>\n"
                "  <title>Video of %1</title>\n"
                "  <link rel=\"alternate\" href=\"https://www.youtube.com/watch?v=v-%1\"/>\n"
                "  <published>2026-10-01T10:00:00+00:00</published>\n"
                " </entry>\n"
                "</feed>\n").arg(id).toUtf8();
            if (QTcpSocket *socket = it->socket) {
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/atom+xml\r\nContent-Length: "
                              + QByteArray::number(feed.size()) + "\r\nConnection: close\r\n\r\n" + feed);
                socket->disconnectFromHost();
            }
            held.erase(it);
            return true;
        };

        YouTubeBackend youtube(data.path(), data.path(), nullptr, nullptr);
        youtube.setFeedUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/feeds/videos.xml").arg(server.serverPort())));
        QSignalSpy ready(&youtube, &YouTubeBackend::listingReady);
        QVERIFY(!youtube.listing(QStringLiteral("subscriptions")).isValid());

        // Four asked for, no more while none has landed.
        QTRY_COMPARE_WITH_TIMEOUT(held.size(), 4, 5000);
        QTest::qWait(300);
        QCOMPARE(held.size(), 4);
        QCOMPARE(asked, ids.mid(0, 4));

        // The last channel opened meanwhile: asked for as soon as a feed
        // lands, and answered as its own does, before the others have.
        QVERIFY(!youtube.listing(QStringLiteral("channel/UCchannel9")).isValid());
        QVERIFY(answer(ids.at(0)));
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 5, 5000);
        QCOMPARE(asked.last(), QStringLiteral("UCchannel9"));
        QVERIFY(answer(QStringLiteral("UCchannel9")));
        QTRY_VERIFY_WITH_TIMEOUT(ready.contains(QVariantList{ QStringLiteral("channel/UCchannel9") }), 5000);
        QVERIFY(!ready.contains(QVariantList{ QStringLiteral("subscriptions") }));
        QCOMPARE(youtube.listing(QStringLiteral("channel/UCchannel9")).toList().value(0).toMap()
                     .value(QStringLiteral("videoId")).toString(),
                 QStringLiteral("v-UCchannel9"));

        // The rest, each answered as it is asked for: never more than four.
        while (asked.size() < ids.size() || !held.isEmpty()) {
            QTRY_VERIFY_WITH_TIMEOUT(!held.isEmpty(), 5000);
            QVERIFY(answer(held.first().channel));
            QTest::qWait(20);
        }
        QCOMPARE(most, 4);
        QCOMPARE(asked.size(), ids.size());
        QStringList sorted = asked;
        sorted.sort();
        QStringList all = ids;
        all.sort();
        QCOMPARE(sorted, all);
        QTRY_VERIFY_WITH_TIMEOUT(ready.contains(QVariantList{ QStringLiteral("subscriptions") }), 5000);
        QCOMPARE(youtube.listing(QStringLiteral("subscriptions")).toList().size(), ids.size());
        QVERIFY(youtube.problem().isEmpty());
    }

    // The two lists typed in Settings (modules.<id>.subscriptions and
    // playlists, lists of lines): Home has their folders with no file there,
    // and the channels are Settings' first, then the file's, each once, a
    // link's channel taken from it.
    void listsFromSettings() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const auto write = [&data](const QString &name, const QByteArray &text) {
            QFile file(data.path() + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text);
        };
        const auto names = [](const QVariant &listing) {
            QStringList out;
            for (const QVariant &entry : listing.toList())
                out << entry.toMap().value(QStringLiteral("name")).toString();
            return out;
        };
        write(QStringLiteral("config.json"), "{}");
        AppCore core(data.path(), data.path());
        YouTubeBackend youtube(data.path(), data.path(), &core, nullptr);
        QCOMPARE(names(youtube.listing(QStringLiteral("home"))), QStringList({ QStringLiteral("Search") }));

        // (Built, not written out as a raw string: moc reads a link's two
        // slashes in one as a comment's, and finds no class in the file.)
        const QJsonObject module{
            { QStringLiteral("subscriptions"),
              QJsonArray{ QStringLiteral("UCtyped"), QStringLiteral(" "),
                          QStringLiteral("https://www.youtube.com/channel/UClinked/videos") } },
            { QStringLiteral("playlists"),
              QJsonArray{ QStringLiteral("Mixes | https://www.youtube.com/playlist?list=PLexample123") } } };
        write(QStringLiteral("config.json"),
              QJsonDocument(QJsonObject{ { QStringLiteral("modules"),
                                           QJsonObject{ { QStringLiteral("com.osdos.youtube"), module } } } }).toJson());
        QCOMPARE(names(youtube.listing(QStringLiteral("home"))),
                 QStringList({ QStringLiteral("Search"), QStringLiteral("Subscriptions"), QStringLiteral("Channels"),
                               QStringLiteral("Playlists") }));

        write(QStringLiteral("youtube_subscriptions.txt"), "# mine\nUCfile\nUCtyped\n");
        QStringList asked;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&asked, socket] {
                const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n"))
                    return;
                static const QRegularExpression channel(QStringLiteral("channel_id=([A-Za-z0-9_-]+)"));
                asked << channel.match(QString::fromLatin1(request)).captured(1);
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        youtube.setFeedUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/feeds/videos.xml").arg(server.serverPort())));
        youtube.listing(QStringLiteral("subscriptions"));
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 3, 5000);
        QCOMPARE(asked, QStringList({ QStringLiteral("UCtyped"), QStringLiteral("UClinked"), QStringLiteral("UCfile") }));
    }

    // Shorts are left out with DISPLAY SHORTS off, in either form it is
    // saved in (false, or an older config's "OFF"); unset, or on, they stay.
    void shortsWithDisplayShortsOff() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        YouTubeBackend youtube(data.path(), data.path(), &core, nullptr);
        QVERIFY(core.add_to_list(QStringLiteral("com.osdos.youtube"), QStringLiteral("favorites"),
                                 QVariantMap{ { "name", "A Talk" }, { "path", "video/a" } }, 100));
        QVERIFY(core.add_to_list(QStringLiteral("com.osdos.youtube"), QStringLiteral("favorites"),
                                 QVariantMap{ { "name", "A Short" }, { "path", "video/s" }, { "isShort", true } }, 100));
        const auto count = [&youtube] { return youtube.entries(QStringLiteral("favorites")).toList().size(); };
        QCOMPARE(count(), 2);
        for (const QVariant &off : { QVariant(false), QVariant(QStringLiteral("OFF")) }) {
            QVERIFY(core.save_setting(QStringLiteral("com.osdos.youtube"), QStringLiteral("display_shorts"), off));
            QCOMPARE(count(), 1);
        }
        for (const QVariant &on : { QVariant(true), QVariant(QStringLiteral("ON")) }) {
            QVERIFY(core.save_setting(QStringLiteral("com.osdos.youtube"), QStringLiteral("display_shorts"), on));
            QCOMPARE(count(), 2);
        }
    }

    // A search's matches, against a stand-in yt-dlp that answers each run
    // with another video: kept while the folder is browsed (and MORE loads
    // onto them), and asked for afresh once forgetSearches() has let them go,
    // as it does before a search typed in is opened. A search on its way
    // isn't dropped, and lands where it was asked for.
    void searchesAskedAfresh() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir(data.path()).mkpath(QStringLiteral("bin")));
        const QString bin = data.path() + QStringLiteral("/bin/yt-dlp");
        QFile script(bin);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\n"
                     "here=$(dirname \"$0\")\n"
                     "n=$(cat \"$here/runs\" 2>/dev/null || echo 0)\n"
                     "n=$((n + 1))\n"
                     "echo $n > \"$here/runs\"\n"
                     "[ -f \"$here/slow\" ] && sleep 1\n"
                     "printf '{\"id\":\"run%s\",\"title\":\"Run %s\"}\\n' $n $n\n");
        script.close();
        QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeOwner));

        const auto ids = [](const QVariant &listing) {
            QStringList out;
            for (const QVariant &entry : listing.toList())
                out << entry.toMap().value(QStringLiteral("videoId")).toString();
            return out;
        };
        const QString cats = QStringLiteral("search/cats");
        YouTubeBackend youtube(data.path(), data.path(), nullptr, nullptr);
        QSignalSpy ready(&youtube, &YouTubeBackend::listingReady);
        QVERIFY(!youtube.listing(cats).isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ids(youtube.listing(cats)), QStringList({ QStringLiteral("run1") }));
        QCOMPARE(ids(youtube.listing(cats)), QStringList({ QStringLiteral("run1") }));

        // The same words typed in again: YouTube asked once more.
        youtube.forgetSearches();
        QVERIFY(!youtube.listing(cats).isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        QCOMPARE(ids(youtube.listing(cats)), QStringList({ QStringLiteral("run2") }));

        // Let go while on its way: it lands all the same, and only once.
        QFile slow(data.path() + QStringLiteral("/bin/slow"));
        QVERIFY(slow.open(QIODevice::WriteOnly));
        slow.close();
        const QString dogs = QStringLiteral("search/dogs");
        QVERIFY(!youtube.listing(dogs).isValid());
        youtube.forgetSearches();
        QVERIFY(!youtube.listing(dogs).isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 3, 5000);
        QCOMPARE(ids(youtube.listing(dogs)), QStringList({ QStringLiteral("run3") }));
        QTest::qWait(1500);
        QCOMPARE(ready.size(), 3);
    }

    // Where a web player's title opens, against a stand-in for TMDB and
    // Wikidata: the service's page for the id Wikidata gives, but its
    // search for the title's name when that id isn't of the plain form a
    // service's id takes (anyone can edit Wikidata, and the address goes to
    // the browser).
    void webTitleIdsOfPlainFormOnly() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QFile key(data.path() + QStringLiteral("/tmdb_api_key.txt"));
        QVERIFY(key.open(QIODevice::WriteOnly));
        key.write("fake-key\n");
        key.close();

        QString wikidataId;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&wikidataId, socket] {
                const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n"))
                    return;
                const QByteArray body = request.startsWith("GET /sparql")
                    ? QJsonDocument(QJsonObject{ { QStringLiteral("results"), QJsonObject{
                          { QStringLiteral("bindings"), QJsonArray{ QJsonObject{
                              { QStringLiteral("id"), QJsonObject{ { QStringLiteral("value"), wikidataId } } } } } } } } })
                          .toJson(QJsonDocument::Compact)
                    : QByteArray(R"({"imdb_id":"tt0000001"})");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        const QByteArray base = QByteArrayLiteral("http://127.0.0.1:") + QByteArray::number(server.serverPort());
        qputenv("OSDOS_TMDB_URL", base + "/3");
        qputenv("OSDOS_WIKIDATA_URL", base + "/sparql");
        TmdbCatalog::Service service;
        service.providerName       = QStringLiteral("Stand-in");
        service.fallbackProviderId = 1;
        service.homeLabel          = QStringLiteral("Home");
        service.titleUrl           = QStringLiteral("https://service.invalid/title/%1");
        service.searchUrl          = QStringLiteral("https://service.invalid/search?q=%1");
        service.wikidataProperty   = QStringLiteral("P1874");
        TmdbCatalog catalog(data.path(), nullptr, service);
        qunsetenv("OSDOS_TMDB_URL");
        qunsetenv("OSDOS_WIKIDATA_URL");
        QSignalSpy ready(&catalog, &TmdbCatalog::titleUrlReady);
        const QVariantMap title{ { QStringLiteral("path"), QStringLiteral("title/movie/7") },
                                 { QStringLiteral("title"), QStringLiteral("Dune") },
                                 { QStringLiteral("mediaType"), QStringLiteral("movie") },
                                 { QStringLiteral("tmdbId"), 7 } };
        const auto opened = [&](const QString &id) {
            wikidataId = id;
            ready.clear();
            catalog.resolveTitleUrl(title);
            if (!ready.wait(5000))
                return QString();
            return ready.first().at(1).toString();
        };
        QCOMPARE(opened(QStringLiteral("80100172")), QStringLiteral("https://service.invalid/title/80100172"));
        QCOMPARE(opened(QStringLiteral("amzn1.dv-gti_x~y")), QStringLiteral("https://service.invalid/title/amzn1.dv-gti_x~y"));
        const QString search = QStringLiteral("https://service.invalid/search?q=Dune");
        QCOMPARE(opened(QStringLiteral("1\" \"--kiosk-printing")), search);
        QCOMPARE(opened(QStringLiteral("1/../../elsewhere")), search);
        QCOMPARE(opened(QStringLiteral("1?next=evil")), search);
        QCOMPARE(opened(QString()), search);
    }

    // A web player's catalogue with no TMDB key says so, and says that it
    // is the key (keyMissing), for the browser to offer the module's
    // settings; a visit starting again clears both.
    void webCatalogSaysTheKeyIsMissing() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        TmdbCatalog::Service service;
        service.providerName = QStringLiteral("Stand-in");
        service.homeLabel    = QStringLiteral("Home");
        TmdbCatalog catalog(data.path(), nullptr, service);
        QVERIFY(!catalog.keyMissing());
        QCOMPARE(catalog.listing(QStringLiteral("movie")).toList().size(), 0);
        QVERIFY(catalog.keyMissing());
        QVERIFY(catalog.problem().startsWith(QStringLiteral("No TMDB API key")));
        QCOMPARE(catalog.property("keyMissing").toBool(), true);
        catalog.listing(QStringLiteral("home"));
        QVERIFY(!catalog.keyMissing());
        QVERIFY(catalog.problem().isEmpty());
    }

    // The playlists' videos from a stand-in yt-dlp (the data folder's
    // bin/yt-dlp, which writes down the playlists it is asked for): fetched
    // as one is opened, every listed playlist at once, a removed video's
    // tombstone left out, the name yt-dlp reports given to the folder the
    // file doesn't name, and kept: opened again, nothing is asked for again.
    void playlistsFromYtDlp() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const auto write = [&data](const QString &name, const QByteArray &text) {
            QFile file(data.path() + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text);
        };
        const auto names = [](const QVariant &listing) {
            QStringList out;
            for (const QVariant &entry : listing.toList())
                out << entry.toMap().value(QStringLiteral("name")).toString();
            return out;
        };
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        write(QStringLiteral("bin/yt-dlp"),
              "#!/bin/sh\n"
              "for last; do :; done\n"
              "echo \"$last\" >> \"$(dirname \"$0\")/asked\"\n"
              "case \"$last\" in *PLbroken) exit 1 ;; esac\n"
              "id=${last##*=}\n"
              "printf '{\"id\":\"one-%s\",\"title\":\"First of %s\",\"channel\":\"Chan\",\"playlist_title\":\"Title %s\"}\\n' \"$id\" \"$id\" \"$id\"\n"
              "printf '{\"id\":\"gone\",\"title\":\"[Deleted video]\"}\\n'\n"
              "printf '{\"id\":\"two-%s\",\"title\":\"Second of %s\",\"uploader\":\"Up\"}\\n' \"$id\" \"$id\"\n");
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/yt-dlp"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(QStringLiteral("config.json"), "{}");
        write(QStringLiteral("youtube_playlists.txt"), "PLfirst\nMine | PLsecond\nPLbroken\n");
        const auto asked = [&data]() {
            QFile file(data.path() + QStringLiteral("/bin/asked"));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                                  : QStringList();
        };
        AppCore core(data.path(), data.path());
        YouTubeBackend youtube(data.path(), data.path(), &core, nullptr);
        QSignalSpy ready(&youtube, &YouTubeBackend::listingReady);
        QCOMPARE(names(youtube.listing(QStringLiteral("playlists"))),
                 QStringList({ QStringLiteral("PLfirst"), QStringLiteral("Mine"), QStringLiteral("PLbroken") }));
        // Only a branch's preview: not fetched.
        QVERIFY(!youtube.listing(QStringLiteral("playlist/PLfirst"), true).isValid());
        QTest::qWait(200);
        QVERIFY(asked().isEmpty());

        QVERIFY(!youtube.listing(QStringLiteral("playlist/PLsecond")).isValid());
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 2, 10000);
        QStringList answered;
        for (const QList<QVariant> &signal : ready)
            answered << signal.value(0).toString();
        QCOMPARE(answered, QStringList({ QStringLiteral("playlist/PLsecond"), QStringLiteral("playlists") }));
        QStringList all = asked();
        all.sort();
        QCOMPARE(all, QStringList({ QStringLiteral("https://www.youtube.com/playlist?list=PLbroken"),
                                    QStringLiteral("https://www.youtube.com/playlist?list=PLfirst"),
                                    QStringLiteral("https://www.youtube.com/playlist?list=PLsecond") }));
        QCOMPARE(names(youtube.listing(QStringLiteral("playlist/PLsecond"))),
                 QStringList({ QStringLiteral("First of PLsecond"), QStringLiteral("Second of PLsecond") }));
        QCOMPARE(names(youtube.listing(QStringLiteral("playlist/PLfirst"))),
                 QStringList({ QStringLiteral("First of PLfirst"), QStringLiteral("Second of PLfirst") }));
        QCOMPARE(names(youtube.listing(QStringLiteral("playlist/PLbroken"))), QStringList());
        QCOMPARE(names(youtube.listing(QStringLiteral("playlists"))),
                 QStringList({ QStringLiteral("Title PLfirst"), QStringLiteral("Mine"), QStringLiteral("PLbroken") }));
        QCOMPARE(youtube.problem(), QString());
        QTest::qWait(200);
        QCOMPARE(asked().size(), 3);
    }

    // The format each ADVANCED choice hands yt-dlp, character for character:
    // H.264 ahead of any codec, a dub's language ahead of the codec, and the
    // original track and plain best last.
    void formats() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QFile config(data.path() + QStringLiteral("/config.json"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("{}");
        config.close();
        AppCore core(data.path(), data.path());
        YouTubeBackend youtube(data.path(), data.path(), &core, nullptr);
        const QString plain480 = QStringLiteral(
            "bestvideo[height<=?480][vcodec^=avc1]+bestaudio/bestvideo[height<=?480]+bestaudio/"
            "best[height<=?480]/best");
        QCOMPARE(youtube.ytdlFormat(QStringLiteral("480p"), QStringLiteral("H.264"), QStringLiteral("Any"),
                                    QStringLiteral("original")), plain480);
        // A resolution it doesn't know is 480p; "Original" in any case is no language.
        QCOMPARE(youtube.ytdlFormat(QStringLiteral("576i"), QStringLiteral("H.264"), QStringLiteral("Any"),
                                    QStringLiteral("ORIGINAL")), plain480);
        QCOMPARE(youtube.ytdlFormat(QStringLiteral("1080p"), QStringLiteral("Any"), QStringLiteral("30"),
                                    QStringLiteral("pt-BR")),
                 QStringLiteral("bestvideo[height<=?1080][fps<=?30]+bestaudio[language^=pt-br]/"
                                "bestvideo[height<=?1080][fps<=?30]+bestaudio/"
                                "best[height<=?1080][fps<=?30]/best"));
        QCOMPARE(youtube.ytdlFormat(QStringLiteral("2160p"), QStringLiteral("H.264"), QStringLiteral("60"),
                                    QStringLiteral(" ES ")),
                 QStringLiteral("bestvideo[height<=?2160][vcodec^=avc1]+bestaudio[language^=es]/"
                                "bestvideo[height<=?2160]+bestaudio[language^=es]/"
                                "bestvideo[height<=?2160][vcodec^=avc1]+bestaudio/"
                                "bestvideo[height<=?2160]+bestaudio/best[height<=?2160]/best"));
        QCOMPARE(youtube.ytdlFormat(QStringLiteral("240p"), QStringLiteral("H.264"), QStringLiteral("Any"), QString()),
                 QStringLiteral("bestvideo[height<=?240][vcodec^=avc1]+bestaudio/bestvideo[height<=?240]+bestaudio/"
                                "best[height<=?240]/best"));
        // What mpv and a download are given, from the settings' defaults.
        QCOMPARE(youtube.playbackArgs(youtube.playbackSettings()),
                 QStringList({ QStringLiteral("--ytdl=yes"), QStringLiteral("--ytdl-format=") + plain480 }));
        QCOMPARE(youtube.downloadArgs(true),
                 QStringList({ QStringLiteral("-f"), plain480, QStringLiteral("--merge-output-format"),
                               QStringLiteral("mp4") }));
        QCOMPARE(youtube.downloadArgs(false),
                 QStringList({ QStringLiteral("-f"),
                               QStringLiteral("best[height<=?480][vcodec^=avc1]/best[height<=?480]/best") }));
    }
};

QTEST_GUILESS_MAIN(YouTubeTest)
#include "youtube_test.moc"
