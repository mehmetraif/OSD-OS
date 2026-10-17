#include "modules/emby/EmbyBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/plex/PlexBackend.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

// The Jellyfin, Emby and Plex backends against a stand-in network, whose
// replies the test completes when it chooses: a list asked for after another
// drops the first's reply, a list kept is shown again without asking the
// server until a playback ends, a stream given up is dropped unreported, an
// auth check that changes nothing isn't announced, and Plex's next item looked
// for by a player left answers no one.

class Reply : public QNetworkReply {
public:
    explicit Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly);
    }
    void abort() override {}
    void complete(QByteArray body) { m_body = body; setFinished(true); emit readyRead(); emit finished(); }
    qint64 bytesAvailable() const override { return m_body.size() + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const qint64 size = qMin(max, qint64(m_body.size()));
        if (!size) return -1;
        std::memcpy(data, m_body.constData(), size); m_body.remove(0, size); return size;
    }
private:
    QByteArray m_body;
};
class Network : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QList<Reply *> replies;
    Reply *last() const { return replies.last(); }
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        auto *reply = new Reply(request, this); replies << reply; return reply;
    }
};

class MediaServersTest : public QObject {
    Q_OBJECT

    // A backend signed in to a server that is the stand-in network.
    template <typename Backend> static Network *signIn(Backend &backend) {
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        backend.m_serverUrl = QStringLiteral("http://server");
        backend.m_accessToken = QStringLiteral("token");
        backend.m_userId = QStringLiteral("user");
        backend.m_lastAuthState = QStringLiteral("authed");
        return network;
    }
    // A list of films, as /Users/{id}/Items answers.
    static QByteArray films(const QStringList &names) {
        QJsonArray items;
        for (const QString &name : names)
            items.append(QJsonObject{ { "Id", name }, { "Name", name }, { "Type", "Movie" } });
        return QJsonDocument(QJsonObject{ { "Items", items } }).toJson();
    }
    // Plex signed in to a server that is the stand-in network.
    static Network *signInPlex(PlexBackend &backend, const QString &dataRoot) {
        QFile auth(dataRoot + QStringLiteral("/plex_auth.json"));
        if (!auth.open(QIODevice::WriteOnly))
            return nullptr;
        auth.write(QJsonDocument(QJsonObject{ { "auth_token", "token" },
                                              { "active_server_uri", "http://server" } }).toJson());
        auth.close();
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        return network;
    }
    // Plex's answer: films by their ratingKeys, each with a part to play.
    static QJsonArray plexFilms(const QStringList &keys) {
        QJsonArray items;
        for (const QString &key : keys) {
            const QJsonObject part{ { "key", "/library/parts/" + key }, { "id", key.toInt() } };
            items.append(QJsonObject{ { "ratingKey", key }, { "title", key }, { "type", "movie" },
                                      { "Media", QJsonArray{ QJsonObject{ { "Part", QJsonArray{ part } } } } } });
        }
        return items;
    }
    static QByteArray plexContainer(const QJsonObject &container) {
        return QJsonDocument(QJsonObject{ { "MediaContainer", container } }).toJson();
    }
    static QStringList titles(const QSignalSpy &spy, int at) {
        QStringList out;
        for (const QVariant &row : spy.at(at).at(0).toList())
            out << row.toMap().value(QStringLiteral("title")).toString();
        return out;
    }

    // Library A asked for, then library B (back, and another opened): A's
    // reply, coming last, is dropped; B's is the one shown.
    template <typename Backend> void listAskedForLaterWins() {
        QTemporaryDir tmp;
        Backend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        QSignalSpy shown(&backend, &Backend::itemsLoaded);
        backend.load_items(QStringLiteral("A"), QStringLiteral("Movie"), QStringLiteral("SortName"));
        Reply *a = network->last();
        backend.load_items(QStringLiteral("B"), QStringLiteral("Movie"), QStringLiteral("SortName"));
        Reply *b = network->last();
        QVERIFY(a != b);
        b->complete(films({ "B1", "B2" }));
        QTRY_COMPARE(shown.count(), 1);
        a->complete(films({ "A1" }));
        QTest::qWait(50);
        QCOMPARE(shown.count(), 1);
        QCOMPARE(titles(shown, 0), QStringList({ "B1", "B2" }));
    }

    // A library shown, then asked for again (back from a film's detail
    // screen): it comes without a request, as it was; a playback ending
    // forgets it, and the next asks the server.
    template <typename Backend> void listKeptUntilPlaybackEnds() {
        QTemporaryDir tmp;
        Backend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        QSignalSpy shown(&backend, &Backend::itemsLoaded);
        backend.load_items(QStringLiteral("A"), QStringLiteral("Movie"), QStringLiteral("SortName"));
        network->last()->complete(films({ "A1", "A2" }));
        QTRY_COMPARE(shown.count(), 1);
        const qsizetype asked = network->replies.size();
        backend.load_items(QStringLiteral("A"), QStringLiteral("Movie"), QStringLiteral("SortName"));
        QCOMPARE(shown.count(), 1);    // told once the view has done asking
        QTRY_COMPARE(shown.count(), 2);
        QCOMPARE(network->replies.size(), asked);
        QCOMPARE(titles(shown, 1), QStringList({ "A1", "A2" }));
        backend.report_playback_stopped(QStringLiteral("A1"), QStringLiteral("A1"), 0);
        const qsizetype reported = network->replies.size();
        backend.load_items(QStringLiteral("A"), QStringLiteral("Movie"), QStringLiteral("SortName"));
        QCOMPARE(network->replies.size(), reported + 1);
    }

    // A stream asked for and given up (back on the launch screen): its
    // reply neither tells a view nor reports a playback to the server; the
    // next one asked for does both.
    template <typename Backend> void streamGivenUpIsDropped() {
        QTemporaryDir tmp;
        Backend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        QSignalSpy ready(&backend, &Backend::streamUrlReady);
        const QByteArray info = QJsonDocument(QJsonObject{
            { "PlaySessionId", "session" },
            { "MediaSources", QJsonArray{ QJsonObject{ { "Id", "film" }, { "SupportsDirectPlay", true } } } }
        }).toJson();
        backend.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1);
        Reply *givenUp = network->last();
        backend.cancel_playback_url();
        const qsizetype asked = network->replies.size();
        givenUp->complete(info);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(network->replies.size(), asked);    // no playback start reported
        backend.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1);
        network->last()->complete(info);
        QCOMPARE(ready.count(), 1);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/Sessions/Playing")));
    }

private slots:
    void jellyfinListAskedForLaterWins() { listAskedForLaterWins<JellyfinBackend>(); }
    void embyListAskedForLaterWins() { listAskedForLaterWins<EmbyBackend>(); }
    void jellyfinListKeptUntilPlaybackEnds() { listKeptUntilPlaybackEnds<JellyfinBackend>(); }
    void embyListKeptUntilPlaybackEnds() { listKeptUntilPlaybackEnds<EmbyBackend>(); }
    void jellyfinStreamGivenUpIsDropped() { streamGivenUpIsDropped<JellyfinBackend>(); }
    void embyStreamGivenUpIsDropped() { streamGivenUpIsDropped<EmbyBackend>(); }

    // The stored token checked and still good, as every time the module
    // opens: nothing changed, so nothing is announced (Root.qml would show
    // Libraries again over the library opened meanwhile). Signing out is.
    void jellyfinAuthCheckThatChangesNothingIsQuiet() {
        QTemporaryDir tmp;
        JellyfinBackend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        QSignalSpy changed(&backend, &JellyfinBackend::authStateChanged);
        backend.check_auth();
        network->replies.first()->complete(QJsonDocument(QJsonObject{ { "Id", "user" } }).toJson());
        QCOMPARE(changed.count(), 0);
        backend.logout();
        QCOMPARE(changed.count(), 1);
    }

    // Plex's Continue Watching is a list like the others: a library left
    // before it came, answering after Continue Watching was opened, is
    // dropped rather than shown in its place.
    void plexContinueWatchingSupersedesAList() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy items(&backend, &PlexBackend::itemsLoaded);
        QSignalSpy continuing(&backend, &PlexBackend::continueWatchingLoaded);
        backend.load_library_all(QStringLiteral("1"));
        Reply *library = network->last();
        backend.load_continue_watching();
        Reply *continueWatching = network->last();
        QVERIFY(library != continueWatching);
        continueWatching->complete(plexContainer(
            { { "Hub", QJsonArray{ QJsonObject{ { "Metadata", plexFilms({ "7" }) } } } } }));
        QTRY_COMPARE(continuing.count(), 1);
        library->complete(plexContainer({ { "Metadata", plexFilms({ "1", "2" }) } }));
        QTest::qWait(50);
        QCOMPARE(items.count(), 0);
    }

    // The item a queue's player looked for as it was left (cancel_stream())
    // answers no one, though it comes before the queue played again asks
    // for its own first; that one is told.
    void plexNextItemOfAPlayerLeftIsDropped() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy next(&backend, &PlexBackend::nextEpisodeReady);
        backend.load_queue_item(QStringLiteral("4"));
        Reply *fourth = network->last();
        backend.cancel_stream();
        fourth->complete(plexContainer({ { "Metadata", plexFilms({ "4" }) } }));
        QCOMPARE(next.count(), 0);
        backend.load_queue_item(QStringLiteral("1"));
        network->last()->complete(plexContainer({ { "Metadata", plexFilms({ "1" }) } }));
        QCOMPARE(next.count(), 1);
        QCOMPARE(next.first().at(0).toMap().value(QStringLiteral("ratingKey")).toString(), QStringLiteral("1"));
    }
};

QTEST_GUILESS_MAIN(MediaServersTest)
#include "media_servers_test.moc"
