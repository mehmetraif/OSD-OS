#include "AppCore.h"
#include "modules/youtube/YouTubeBackend.h"

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
};

QTEST_GUILESS_MAIN(YouTubeTest)
#include "youtube_test.moc"
