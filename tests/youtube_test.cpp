#include "AppCore.h"
#include "modules/web_player/TmdbCatalog.h"
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
};

QTEST_GUILESS_MAIN(YouTubeTest)
#include "youtube_test.moc"
