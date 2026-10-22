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
#include <QUrlQuery>
#include <QtTest>
#include <cstring>

// The Jellyfin, Emby and Plex backends against a stand-in network, whose
// replies the test completes when it chooses: a list asked for after another
// drops the first's reply, a list kept is shown again without asking the
// server until a playback ends, a stream given up is dropped unreported, an
// auth check that changes nothing isn't announced, Plex's next item looked
// for by a player left answers no one, the next episode is found beyond a
// series' first five hundred (and the whole series asked for from a server
// that doesn't skip to the episode), what a Jellyfin server could do is
// forgotten at sign-out, Plex's PIN is polled for no longer once it expires
// or the sign-in is left, every request turned away with a 498 while one
// refresh runs is retried after it, a refused key registration is tried
// again at the next start, and Auto quality caps no transcode.

class Reply : public QNetworkReply {
public:
    explicit Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly);
    }
    void abort() override {}
    void complete(QByteArray body) { m_body = body; setFinished(true); emit readyRead(); emit finished(); }
    // The server's refusal, by its status (404, 498, 500...).
    void fail(int status) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setError(status == 404 ? ContentNotFoundError : UnknownContentError, QStringLiteral("HTTP %1").arg(status));
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }
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
    // An episode as the server describes it, and a list of them.
    struct Episode { const char *id; int season; int index; };
    static QByteArray episodes(std::initializer_list<Episode> list) {
        QJsonArray items;
        for (const Episode &e : list)
            items.append(QJsonObject{ { "Id", e.id }, { "Type", "Episode" }, { "SeriesId", "show" },
                                      { "ParentIndexNumber", e.season }, { "IndexNumber", e.index } });
        return QJsonDocument(QJsonObject{ { "Items", items } }).toJson();
    }

    // The next episode of a long series: the episodes from the current one
    // on are asked for (startItemId), so the next is found past the five
    // hundred a list of the whole series was cut at. A server that doesn't
    // skip to the episode (its page starts at the first) is asked once more
    // for the whole series.
    template <typename Backend> void nextEpisodeFoundBeyondTheFirstPage() {
        QTemporaryDir tmp;
        Backend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        QSignalSpy next(&backend, &Backend::nextEpisodeReady);
        const QByteArray current = episodes({ { "e501", 21, 1 } });
        const QByteArray detail = QJsonDocument(QJsonDocument::fromJson(current).object()["Items"]
                                                    .toArray().first().toObject()).toJson();
        backend.load_next_episode(QStringLiteral("e501"));
        network->last()->complete(detail);
        Reply *page = network->last();
        QCOMPARE(QUrlQuery(page->url()).queryItemValue(QStringLiteral("startItemId")), QStringLiteral("e501"));
        page->complete(episodes({ { "e501", 21, 1 }, { "e503", 21, 3 }, { "e502", 21, 2 } }));
        QCOMPARE(next.count(), 1);
        QCOMPARE(next.at(0).at(0).toMap().value(QStringLiteral("itemId")).toString(), QStringLiteral("e502"));

        backend.load_next_episode(QStringLiteral("e501"));
        network->last()->complete(detail);
        Reply *first = network->last();
        first->complete(episodes({ { "e1", 1, 1 }, { "e2", 1, 2 } }));
        QCOMPARE(next.count(), 1);
        Reply *whole = network->last();
        QVERIFY(whole != first);
        QVERIFY(!QUrlQuery(whole->url()).hasQueryItem(QStringLiteral("startItemId")));
        whole->complete(episodes({ { "e1", 1, 1 }, { "e501", 21, 1 }, { "e502", 21, 2 } }));
        QCOMPARE(next.count(), 2);
        QCOMPARE(next.at(1).at(0).toMap().value(QStringLiteral("itemId")).toString(), QStringLiteral("e502"));
        // The last of the series: none.
        backend.load_next_episode(QStringLiteral("e502"));
        network->last()->complete(QJsonDocument(QJsonObject{ { "Id", "e502" }, { "Type", "Episode" }, { "SeriesId", "show" },
                                                             { "ParentIndexNumber", 21 }, { "IndexNumber", 2 } }).toJson());
        network->last()->complete(episodes({ { "e502", 21, 2 } }));
        QCOMPARE(next.count(), 3);
        QVERIFY(next.at(2).at(0).toMap().isEmpty());
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
    void jellyfinNextEpisodeFoundBeyondTheFirstPage() { nextEpisodeFoundBeyondTheFirstPage<JellyfinBackend>(); }
    void embyNextEpisodeFoundBeyondTheFirstPage() { nextEpisodeFoundBeyondTheFirstPage<EmbyBackend>(); }

    // What a server could do (MediaSegments) says nothing of the next one
    // signed into: forgotten at sign-out, and asked again.
    void jellyfinCapabilitiesForgottenOnSignOut() {
        QTemporaryDir tmp;
        JellyfinBackend backend(tmp.path(), tmp.path());
        Network *network = signIn(backend);
        backend.probeCapabilities();
        network->last()->fail(400);
        QVERIFY(backend.m_capabilitiesProbed);
        QVERIFY(backend.m_hasCapability);
        const qsizetype asked = network->replies.size();
        backend.probeCapabilities();
        QCOMPARE(network->replies.size(), asked);    // known: not asked again
        backend.logout();
        QVERIFY(!backend.m_capabilitiesProbed);
        QVERIFY(!backend.m_hasCapability);
        backend.m_serverUrl = QStringLiteral("http://other");
        backend.m_accessToken = QStringLiteral("token");
        backend.m_userId = QStringLiteral("user");
        backend.probeCapabilities();
        QVERIFY(network->replies.size() > asked + 1);
    }

    // The PIN polled for every so often: no longer once plex.tv has
    // forgotten it (404), nor once the sign-in is left, whose PIN still on
    // its way is dropped.
    void plexPinPollingStopsOnceTheCodeExpires() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        backend.m_pollTimer->setInterval(10);
        QSignalSpy pin(&backend, &PlexBackend::pinReady);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        backend.start_pin_auth();
        network->last()->complete(QJsonDocument(QJsonObject{ { "id", 7 }, { "code", "ABCD" } }).toJson());
        QCOMPARE(pin.count(), 1);
        QVERIFY(backend.m_pollTimer->isActive());
        const qsizetype asked = network->replies.size();
        QTRY_VERIFY(network->replies.size() > asked);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/api/v2/pins/7")));
        network->last()->fail(404);
        QVERIFY(!backend.m_pollTimer->isActive());
        QVERIFY(backend.m_pendingPinId.isEmpty());
        QCOMPARE(errors.count(), 1);
        const qsizetype stopped = network->replies.size();
        QTest::qWait(50);
        QCOMPARE(network->replies.size(), stopped);

        // A sign-in screen replaced by another is destroyed after the new one
        // asked for its code: its cancel, of its own request, takes nothing
        // from the new one.
        const int request = backend.start_pin_auth();
        backend.cancel_pin_auth(request - 1);
        network->last()->complete(QJsonDocument(QJsonObject{ { "id", 8 }, { "code", "EFGH" } }).toJson());
        QCOMPARE(pin.count(), 2);
        QVERIFY(backend.m_pollTimer->isActive());
        // Backed out of the sign-in: the answer still on its way is dropped.
        const int gone = backend.start_pin_auth();
        Reply *late = network->last();
        backend.cancel_pin_auth(gone);
        late->complete(QJsonDocument(QJsonObject{ { "id", 9 }, { "code", "IJKL" } }).toJson());
        QCOMPARE(pin.count(), 2);
        QVERIFY(!backend.m_pollTimer->isActive());
    }

    // Two requests turned away with a 498 (the token stale) while one
    // refresh runs: one refresh, and both retried after it.
    void plex498RetriesAllWaitForOneRefresh() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        backend.generateAndSaveKeyPair(QStringLiteral("kid"));
        QJsonObject auth = backend.loadAuth();
        auth["jwt_key_id"] = QStringLiteral("kid");
        backend.saveAuth(auth);
        QSignalSpy revoked(&backend, &PlexBackend::authRevoked);
        backend.load_item_detail(QStringLiteral("1"));
        Reply *detail = network->last();
        backend.load_extras(QStringLiteral("1"));
        Reply *extras = network->last();
        detail->fail(498);
        Reply *nonce = network->last();
        QVERIFY(nonce->url().path().endsWith(QLatin1String("/api/v2/auth/nonce")));
        extras->fail(498);
        QCOMPARE(network->last(), nonce);    // one refresh
        nonce->complete(QJsonDocument(QJsonObject{ { "nonce", "n" } }).toJson());
        Reply *token = network->last();
        QVERIFY(token->url().path().endsWith(QLatin1String("/api/v2/auth/token")));
        const qsizetype before = network->replies.size();
        token->complete(QJsonDocument(QJsonObject{ { "auth_token", "jwt2" } }).toJson());
        QCOMPARE(network->replies.size(), before + 2);
        QStringList paths;
        for (qsizetype i = before; i < network->replies.size(); ++i)
            paths << network->replies.at(i)->url().path();
        paths.sort();
        QCOMPARE(paths, QStringList({ QStringLiteral("/library/metadata/1"), QStringLiteral("/library/metadata/1/extras") }));
        QCOMPARE(revoked.count(), 0);
        // A refresh that fails gives the sign-in up, once.
        network->last()->fail(498);
        network->last()->fail(500);    // the nonce
        QCOMPARE(revoked.count(), 1);
    }

    // A key registration plex.tv refused (or that never reached it): the key
    // pair goes, so the next start migrates the token again rather than
    // taking the key file for a migration done.
    void plexRefusedKeyRegistrationIsRetriedAtTheNextStart() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        int done = 0;
        backend.checkAndRefreshOnStartup([&done] { ++done; });
        Reply *registration = network->last();
        QVERIFY(registration->url().path().endsWith(QLatin1String("/api/v2/auth/jwk")));
        QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/plex_key.pem")));
        registration->fail(500);
        QCOMPARE(done, 1);
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/plex_key.pem")));
        QVERIFY(!backend.loadAuth().contains(QStringLiteral("jwt_key_id")));
        backend.checkAndRefreshOnStartup([&done] { ++done; });
        QVERIFY(network->last() != registration);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/api/v2/auth/jwk")));
    }

    // Video Quality Auto: no cap on the transcode, the server's own choice.
    void plexAutoQualityCapsNoTranscode() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        backend.request_transcode(QStringLiteral("1"), QStringLiteral("/library/parts/1"), QStringLiteral("s"),
                                  QString(), QString(), 0);
        const QUrlQuery query(network->last()->url());
        QVERIFY(query.hasQueryItem(QStringLiteral("session")));
        QVERIFY(!query.hasQueryItem(QStringLiteral("maxVideoBitrate")));
    }

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
