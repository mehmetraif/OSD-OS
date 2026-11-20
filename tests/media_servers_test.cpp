#include "AppCore.h"
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
// drops the first's reply (and so do a season's episodes, a show's seasons
// and its next episode on Jellyfin and Emby), a list kept is shown again without asking the
// server until a playback ends, a stream given up is dropped unreported, an
// auth check that changes nothing isn't announced, Plex's next item looked
// for by a player left answers no one, the next episode is found beyond a
// series' first five hundred (and the whole series asked for from a server
// that doesn't skip to the episode), what a Jellyfin server could do is
// forgotten at sign-out, Plex's PIN is polled for no longer once it expires
// or the sign-in is left, every request turned away with a 498 while one
// refresh runs is retried after it, a refused key registration is tried
// again at the next start, Auto quality caps no transcode, a live channel
// left while it tuned plays in no one's player, a Plex server that moved is
// found again at the address it has now, Emby Connect tries a server's
// address from outside when its LAN address gives no sign-in, Settings'
// Libraries are told why there are none when the server can't be reached,
// Jellyfin's Quick Connect says a server's refusal by its status, an error
// says a refusal's status and never the address asked, a
// Server Address changed while signed in to Jellyfin or Emby signs out of
// the server that issued the token, which goes to no other host, and
// Jellyfin's and Emby's backends, one code, ask each in its own way where
// their servers differ: PlaybackInfo with their own device profiles, the
// URLs mpv is handed, Emby's Dolby Vision transcoded. A Plex sign-in left
// with a token and no account asks plex.tv for it again. An NFC card's
// lookup left for another card's answers no one. Plex's shuffle asks for a
// show's episodes once for each bag it makes of them.

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
    // The server's refusal in Qt's own words for it, which have the address
    // asked whole (the user's id in it), and the phrase the server sent.
    void refuse(int status, const char *phrase) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QByteArray(phrase));
        setError(status == 404 ? ContentNotFoundError : UnknownContentError,
                 QStringLiteral("Error transferring %1 - server replied: %2")
                     .arg(url().toString(), QLatin1String(phrase)));
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }
    // No answer at all: nothing at the address (the server moved, the
    // network down), so no status either.
    void drop() {
        setError(ConnectionRefusedError, QStringLiteral("Connection refused"));
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() + QNetworkReply::bytesAvailable(); }
    QByteArray sent;    // the body the request sent (a POST's)
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
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *data) override {
        auto *reply = new Reply(request, this); replies << reply;
        if (data)
            reply->sent = data->readAll();
        return reply;
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
        Backend backend(tmp.path());
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
        Backend backend(tmp.path());
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
        Backend backend(tmp.path());
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
        Backend backend(tmp.path());
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

    // Settings → Libraries (getLibraries()) with the server out of reach, or
    // answering with an error: no choices, and why, as the one entry the page
    // says in their place ({ problem }); answered, its libraries.
    static QVariantList libraries(const QSignalSpy &spy) {
        for (qsizetype i = spy.count() - 1; i >= 0; --i) {
            if (spy.at(i).at(0).toString() == QLatin1String("libraries"))
                return spy.at(i).at(1).toList();
        }
        return { QStringLiteral("not answered") };
    }
    template <typename Backend> void librariesSayWhyThereAreNone(Backend &backend, Network *network,
                                                                 const QByteArray &answer) {
        QSignalSpy options(&backend, &Backend::dynamicOptionsReady);
        backend.getLibraries();
        network->last()->drop();
        QVariantList got = libraries(options);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().toMap().value(QStringLiteral("problem")).toString(),
                 QStringLiteral("The server could not be reached"));
        QVERIFY(!got.first().toMap().contains(QStringLiteral("id")));
        backend.getLibraries();
        network->last()->fail(500);
        got = libraries(options);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().toMap().value(QStringLiteral("problem")).toString(),
                 QStringLiteral("The server answered with an error (HTTP 500)"));
        backend.getLibraries();
        network->last()->complete(answer);
        got = libraries(options);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("FILMS"));
        QVERIFY(!got.first().toMap().contains(QStringLiteral("problem")));
    }
    template <typename Backend> void librariesSayWhyThereAreNone() {
        QTemporaryDir tmp;
        Backend backend(tmp.path());
        Network *network = signIn(backend);
        const QJsonObject films{ { "Id", "1" }, { "Name", "Films" }, { "CollectionType", "movies" } };
        librariesSayWhyThereAreNone(backend, network,
                                    QJsonDocument(QJsonObject{ { "Items", QJsonArray{ films } } }).toJson());
    }

    // The warnings written to the log while `what` runs, each as it is
    // written.
    static inline QStringList *s_logged = nullptr;
    static void keepWarning(QtMsgType type, const QMessageLogContext &, const QString &message) {
        if (type == QtWarningMsg && s_logged)
            s_logged->append(message);
    }
    template <typename What> static QStringList logged(What what) {
        QStringList lines;
        s_logged = &lines;
        const QtMessageHandler previous = qInstallMessageHandler(keepWarning);
        what();
        qInstallMessageHandler(previous);
        s_logged = nullptr;
        return lines;
    }

    // A detail, show or season page shows nothing when what it asked the
    // server for doesn't come, so each such load that fails is one line in
    // the log: what was asked for, by the item's id, the server's host, and
    // the status it answered with or the network's own word. Neither the
    // token nor the address asked, the user's id in it, is ever written. An
    // answer that isn't what was asked for (a proxy's sign-in page in its
    // place) is said too; one that is, an empty list among them, writes
    // nothing. The page is told as before.
    template <typename Backend> void failedPageLoadsAreLogged(const char *tag) {
        QTemporaryDir tmp;
        Backend backend(tmp.path());
        Network *network = signIn(backend);
        backend.m_serverUrl = QStringLiteral("http://nas.example:8096");
        backend.m_accessToken = QStringLiteral("made-up-token");
        backend.m_userId = QStringLiteral("made-up-user");
        QSignalSpy errors(&backend, &Backend::errorOccurred);
        QSignalSpy item(&backend, &Backend::itemLoaded);
        QSignalSpy seasons(&backend, &Backend::seasonsLoaded);
        const auto line = [tag](const char *what, const char *why) {
            return QStringList{ QStringLiteral("[%1] %2 could not be loaded from nas.example: %3")
                                    .arg(QLatin1String(tag), QLatin1String(what), QLatin1String(why)) };
        };
        QCOMPARE(logged([&] { backend.load_item_detail(QStringLiteral("film")); network->last()->fail(404); }),
                 line("item film", "HTTP 404"));
        QCOMPARE(logged([&] { backend.load_seasons(QStringLiteral("show")); network->last()->drop(); }),
                 line("the seasons of show show", "Connection refused"));
        QCOMPARE(logged([&] {
                     backend.load_episodes(QStringLiteral("show"), QStringLiteral("season"));
                     network->last()->fail(500);
                 }),
                 line("the episodes of season season", "HTTP 500"));
        QCOMPARE(logged([&] { backend.load_series_next_up(QStringLiteral("show")); network->last()->fail(502); }),
                 line("the next episode of show show", "HTTP 502"));
        QCOMPARE(errors.count(), 4);
        QVERIFY(errors.at(0).at(0).toString().startsWith(QLatin1String("LOAD ITEM DETAIL FAILED: ")));

        const QByteArray signInPage = "<html><body>Sign in to go on</body></html>";
        QCOMPARE(logged([&] { backend.load_item_detail(QStringLiteral("film")); network->last()->complete(signInPage); }),
                 line("item film", "the answer is not the item"));
        QCOMPARE(logged([&] { backend.load_seasons(QStringLiteral("show")); network->last()->complete(signInPage); }),
                 line("the seasons of show show", "the answer is not a list"));
        QCOMPARE(logged([&] {
                     backend.load_episodes(QStringLiteral("show"), QStringLiteral("season"));
                     network->last()->complete(signInPage);
                 }),
                 line("the episodes of season season", "the answer is not a list"));
        QCOMPARE(errors.count(), 4);

        // Answered: nothing to say, a show without seasons included.
        QCOMPARE(logged([&] {
                     backend.load_item_detail(QStringLiteral("film"));
                     network->last()->complete(QJsonDocument(QJsonObject{ { "Id", "film" }, { "Name", "Film" } }).toJson());
                     backend.load_seasons(QStringLiteral("show"));
                     network->last()->complete(films({}));
                     backend.load_episodes(QStringLiteral("show"), QStringLiteral("season"));
                     network->last()->complete(films({ "e1" }));
                 }),
                 QStringList());
        QCOMPARE(item.last().at(0).toMap().value(QStringLiteral("itemId")).toString(), QStringLiteral("film"));
        QVERIFY(seasons.last().at(0).toList().isEmpty());
    }

    // What the screen says of a request the server turned down (errorOccurred,
    // which the views write to the log too) is the server's status, never
    // Qt's words for it, which have the address asked whole, the user's id
    // in it; where nothing answered, the network's own word.
    static void errorsSayNoAddress(const QSignalSpy &errors) {
        QVERIFY(!errors.isEmpty());
        for (const QList<QVariant> &error : errors) {
            const QString said = error.at(0).toString();
            QVERIFY2(said.endsWith(QLatin1String(": HTTP 404 Not Found")), qPrintable(said));
            QVERIFY2(!said.contains(QLatin1String("made-up-user")) && !said.contains(QLatin1String("://")),
                     qPrintable(said));
        }
    }
    template <typename Backend> void errorsShowNoAddress() {
        QTemporaryDir tmp;
        Backend backend(tmp.path());
        Network *network = signIn(backend);
        backend.m_serverUrl = QStringLiteral("http://nas.example:8096");
        backend.m_userId = QStringLiteral("made-up-user");
        QSignalSpy errors(&backend, &Backend::errorOccurred);
        const QList<std::function<void()>> asks{
            [&] { backend.check_auth(); },
            [&] { backend.load_item_detail(QStringLiteral("film")); },
            [&] { backend.load_seasons(QStringLiteral("show")); },
            [&] { backend.load_episodes(QStringLiteral("show"), QStringLiteral("season")); },
            [&] { backend.load_series_next_up(QStringLiteral("show")); },
            [&] { backend.load_continue_watching(); },
            [&] { backend.load_up_next(); },
            [&] { backend.load_items(QStringLiteral("library"), QStringLiteral("Movie"), QStringLiteral("SortName")); },
            [&] { backend.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1); },
        };
        for (const auto &ask : asks) {
            ask();
            network->last()->refuse(404, "Not Found");
        }
        QCOMPARE(errors.count(), asks.size());
        errorsSayNoAddress(errors);
        backend.load_seasons(QStringLiteral("show"));
        network->last()->drop();
        QCOMPARE(errors.last().at(0).toString(), QStringLiteral("LOAD SEASONS FAILED: Connection refused"));
    }

    // A season opened and left before its episodes came, another opened:
    // the first's, answering last, are dropped, and so are a show's seasons
    // left the same way. A show's PLAY left for another show's: the first
    // show's next episode, answering last, opens nothing, nor does its
    // failure cancel the second's.
    template <typename Backend> void showOrSeasonLeftIsDropped() {
        QTemporaryDir tmp;
        Backend backend(tmp.path());
        Network *network = signIn(backend);
        QSignalSpy seasons(&backend, &Backend::seasonsLoaded);
        QSignalSpy episodes(&backend, &Backend::episodesLoaded);
        QSignalSpy nextUp(&backend, &Backend::seriesNextUpReady);
        QSignalSpy errors(&backend, &Backend::errorOccurred);

        backend.load_episodes(QStringLiteral("show"), QStringLiteral("season1"));
        Reply *season1 = network->last();
        backend.load_episodes(QStringLiteral("show"), QStringLiteral("season2"));
        Reply *season2 = network->last();
        season2->complete(films({ "S2E1", "S2E2" }));
        season1->complete(films({ "S1E1" }));
        QCOMPARE(episodes.count(), 1);
        QCOMPARE(titles(episodes, 0), QStringList({ "S2E1", "S2E2" }));

        backend.load_seasons(QStringLiteral("showA"));
        Reply *showA = network->last();
        backend.load_seasons(QStringLiteral("showB"));
        Reply *showB = network->last();
        showA->complete(films({ "A1" }));
        showB->complete(films({ "B1" }));
        QCOMPARE(seasons.count(), 1);
        QCOMPARE(titles(seasons, 0), QStringList({ "B1" }));

        backend.load_series_next_up(QStringLiteral("showA"));
        Reply *playA = network->last();
        backend.load_series_next_up(QStringLiteral("showB"));
        Reply *playB = network->last();
        playB->complete(films({ "B-next" }));
        playA->complete(films({ "A-next" }));
        QCOMPARE(nextUp.count(), 1);
        QCOMPARE(nextUp.at(0).at(0).toMap().value(QStringLiteral("itemId")).toString(), QStringLiteral("B-next"));
        backend.load_series_next_up(QStringLiteral("showA"));
        playA = network->last();
        backend.load_series_next_up(QStringLiteral("showB"));
        playB = network->last();
        playA->fail(500);
        QCOMPARE(errors.count(), 0);
        playB->complete(films({ "B-next" }));
        QCOMPARE(nextUp.count(), 2);
    }

    // Settings → Server Address changed while signed in: the token goes to
    // no host but the one that issued it. The backend signs out (the revoke
    // sent to the server signed into), and what is asked after goes nowhere;
    // the same address again, as the sign-in saves it, changes nothing.
    template <typename Backend> void serverAddressChangedSignsOut(const QString &moduleId) {
        QTemporaryDir tmp;
        Backend backend(tmp.path());
        Network *network = signIn(backend);
        QSignalSpy auth(&backend, &Backend::authStateChanged);
        backend.onSettingChanged(moduleId, QStringLiteral("server_url"), QStringLiteral("http://server/"));
        QVERIFY(backend.has_auth());
        QCOMPARE(auth.count(), 0);
        QVERIFY(network->replies.isEmpty());

        backend.onSettingChanged(moduleId, QStringLiteral("server_url"), QStringLiteral("http://elsewhere:8096"));
        QVERIFY(!backend.has_auth());
        QCOMPARE(auth.count(), 1);
        // Emby's revoke follows its device's deauth.
        for (qsizetype i = 0; i < network->replies.size(); ++i)
            network->replies.at(i)->complete(QByteArray());
        QVERIFY(!network->replies.isEmpty());
        backend.load_seasons(QStringLiteral("show"));
        backend.load_item_detail(QStringLiteral("film"));
        for (const Reply *reply : std::as_const(network->replies)) {
            QCOMPARE(reply->url().host(), QStringLiteral("server"));
            QVERIFY(!reply->url().path().contains(QLatin1String("/Items/")));
        }
    }

private slots:
    void jellyfinErrorsShowNoAddress() { errorsShowNoAddress<JellyfinBackend>(); }
    void embyErrorsShowNoAddress() { errorsShowNoAddress<EmbyBackend>(); }
    void plexErrorsShowNoAddress() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        backend.load_item_detail(QStringLiteral("10"));
        network->last()->refuse(404, "Not Found");
        backend.load_children(QStringLiteral("20"));
        network->last()->refuse(404, "Not Found");
        backend.load_continue_watching();
        network->last()->refuse(404, "Not Found");
        backend.request_transcode(QStringLiteral("10"), QStringLiteral("/library/parts/10"), QStringLiteral("session"),
                                  QString(), QStringLiteral("0"), 0);
        network->last()->refuse(404, "Not Found");
        QCOMPARE(errors.count(), 4);
        errorsSayNoAddress(errors);
    }
    void jellyfinShowOrSeasonLeftIsDropped() { showOrSeasonLeftIsDropped<JellyfinBackend>(); }
    void embyShowOrSeasonLeftIsDropped() { showOrSeasonLeftIsDropped<EmbyBackend>(); }
    void jellyfinServerAddressChangedSignsOut() {
        serverAddressChangedSignsOut<JellyfinBackend>(QStringLiteral("com.osdos.jellyfin"));
    }
    void embyServerAddressChangedSignsOut() {
        serverAddressChangedSignsOut<EmbyBackend>(QStringLiteral("com.osdos.emby"));
    }
    void jellyfinLibrariesSayWhyThereAreNone() { librariesSayWhyThereAreNone<JellyfinBackend>(); }
    void embyLibrariesSayWhyThereAreNone() { librariesSayWhyThereAreNone<EmbyBackend>(); }
    void plexLibrariesSayWhyThereAreNone() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        const QJsonObject films{ { "key", "1" }, { "title", "Films" }, { "type", "movie" } };
        librariesSayWhyThereAreNone(backend, network, plexContainer({ { "Directory", QJsonArray{ films } } }));
    }
    void jellyfinFailedPageLoadsAreLogged() { failedPageLoadsAreLogged<JellyfinBackend>("Jellyfin"); }
    void embyFailedPageLoadsAreLogged() { failedPageLoadsAreLogged<EmbyBackend>("Emby"); }
    // Plex's: an item, what is under it (a show's seasons, a season's
    // episodes) and its extras, whose failure the page isn't told of. An
    // item the server has nothing to play of leaves its page as empty as
    // one that didn't come, and is said as well.
    void plexFailedPageLoadsAreLogged() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        QSignalSpy item(&backend, &PlexBackend::itemLoaded);
        QSignalSpy extras(&backend, &PlexBackend::extrasLoaded);
        const auto line = [](const char *what, const char *why) {
            return QStringList{ QStringLiteral("[Plex] %1 could not be loaded from server: %2")
                                    .arg(QLatin1String(what), QLatin1String(why)) };
        };
        QCOMPARE(logged([&] { backend.load_item_detail(QStringLiteral("10")); network->last()->fail(404); }),
                 line("item 10", "HTTP 404"));
        QCOMPARE(logged([&] { backend.load_children(QStringLiteral("20")); network->last()->drop(); }),
                 line("the seasons or episodes of item 20", "Connection refused"));
        QCOMPARE(errors.count(), 2);
        QCOMPARE(logged([&] { backend.load_extras(QStringLiteral("10")); network->last()->fail(500); }),
                 line("the extras of item 10", "HTTP 500"));
        QCOMPARE(errors.count(), 2);
        QCOMPARE(extras.count(), 1);
        QVERIFY(extras.first().at(0).toList().isEmpty());

        QCOMPARE(logged([&] {
                     backend.load_item_detail(QStringLiteral("10"));
                     network->last()->complete(plexContainer({ { "Metadata", QJsonArray() } }));
                 }),
                 line("item 10", "the answer has no such item"));
        QCOMPARE(errors.count(), 3);
        const QJsonObject bare{ { "ratingKey", "10" }, { "title", "Film" }, { "type", "movie" } };
        QCOMPARE(logged([&] {
                     backend.load_item_detail(QStringLiteral("10"));
                     network->last()->complete(plexContainer({ { "Metadata", QJsonArray{ bare } } }));
                 }),
                 line("item 10", "the server has nothing of it to play"));
        QCOMPARE(logged([&] {
                     backend.load_children(QStringLiteral("20"));
                     network->last()->complete("<html><body>Sign in to go on</body></html>");
                 }),
                 line("the seasons or episodes of item 20", "the answer is not a list"));

        // Answered: nothing to say, an item without extras included.
        QCOMPARE(logged([&] {
                     backend.load_item_detail(QStringLiteral("10"));
                     network->last()->complete(plexContainer({ { "Metadata", plexFilms({ "10" }) } }));
                     backend.load_children(QStringLiteral("20"));
                     network->last()->complete(plexContainer({ { "Metadata", QJsonArray() } }));
                     backend.load_extras(QStringLiteral("10"));
                     network->last()->complete(plexContainer({ { "size", 0 } }));
                 }),
                 QStringList());
        QCOMPARE(item.last().at(0).toMap().value(QStringLiteral("ratingKey")).toString(), QStringLiteral("10"));
    }
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
        JellyfinBackend backend(tmp.path());
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

    // The settings page learns what the server can do as it opens, with the
    // choices it asks for: Intro Skip and Credit Skip are usable there, on
    // every opening, the server asked once.
    void jellyfinSettingsPageLearnsCapabilities() {
        QTemporaryDir tmp;
        JellyfinBackend backend(tmp.path());
        Network *network = signIn(backend);
        QSignalSpy options(&backend, &JellyfinBackend::dynamicOptionsReady);
        const auto capabilities = [&options]() {
            QVariantList last;
            int times = 0;
            for (const QList<QVariant> &sent : options) {
                if (sent.at(0).toString() == QLatin1String("_capabilities")) {
                    last = sent.at(1).toList();
                    ++times;
                }
            }
            return qMakePair(times, last);
        };
        backend.getVideoQualities();
        QCOMPARE(capabilities().first, 0);          // not known yet: asked
        network->last()->fail(400);
        QCOMPARE(capabilities().first, 1);
        QCOMPARE(capabilities().second, QVariantList{QStringLiteral("mediasegments")});
        const qsizetype asked = network->replies.size();
        backend.getVideoQualities();                // the page opened again
        QCOMPARE(network->replies.size(), asked);
        QCOMPARE(capabilities().first, 2);
    }

    // The PIN polled for every so often: no longer once plex.tv has
    // forgotten it (404), nor once the sign-in is left, whose PIN still on
    // its way is dropped.
    // A profile switch plex.tv doesn't answer changes nothing: the profile
    // that was active stays, whatever PIN was typed, and nothing more is
    // asked. The profile already active is refreshed as before.
    void plexAFailedSwitchChangesNoProfile() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString file = tmp.path() + QStringLiteral("/plex_auth.json");
        const auto write = [&](const QString &active) {
            QFile auth(file);
            QVERIFY(auth.open(QIODevice::WriteOnly | QIODevice::Truncate));
            auth.write(QJsonDocument(QJsonObject{
                { "auth_token", "token" }, { "active_server_uri", "http://server" },
                { "account_user_id", "1" }, { "active_user_id", active },
                { "users", QJsonArray{ QJsonObject{ { "id", "1" }, { "uuid", "owner" }, { "protected", true } },
                                       QJsonObject{ { "id", "2" }, { "uuid", "kid" } } } },
                { "user_server_tokens", QJsonObject{ { "m", "owner-token" } } },
                { "managed_user_tokens", QJsonObject{ { "m", "kid-token" } } } }).toJson());
        };
        const auto active = [&] {
            QFile auth(file);
            if (!auth.open(QIODevice::ReadOnly))
                return QString();
            return QJsonDocument::fromJson(auth.readAll()).object().value("active_user_id").toString();
        };
        write(QStringLiteral("2"));
        // The module's settings are kept by AppCore, as in the app.
        AppCore core(tmp.path(), tmp.path());
        PlexBackend backend(tmp.path(), tmp.path(), &core);
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        QSignalSpy loaded(&backend, &PlexBackend::serversLoaded);

        // The kid's profile is on; the owner's is asked for with some PIN,
        // and plex.tv is out of reach.
        backend.select_user(QStringLiteral("1"), QStringLiteral("0000"));
        QCOMPARE(network->replies.size(), 1);
        QVERIFY(network->last()->url().path().endsWith(QStringLiteral("/owner/switch")));
        network->last()->fail(500);
        QCOMPARE(errors.count(), 1);
        QVERIFY2(errors.first().first().toString().startsWith(QStringLiteral("COULD NOT SWITCH PROFILE")),
                 qPrintable(errors.first().first().toString()));
        QCOMPARE(loaded.count(), 0);
        QCOMPARE(network->replies.size(), 1);
        QCOMPARE(active(), QStringLiteral("2"));

        // The other way: from the owner's to the kid's, the same.
        write(QStringLiteral("1"));
        QTest::qWait(20);
        errors.clear();
        backend.select_user(QStringLiteral("2"), QStringLiteral("0000"));
        network->last()->fail(500);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(active(), QStringLiteral("1"));

        // From Settings, whose Current User row is saved before the switch is
        // asked for: the setting is put back, and the choices sent again for
        // the row to show it.
        const auto setting = [&] { return backend.plexSetting("current_user_id").toString(); };
        backend.savePlexSetting("current_user_id", QStringLiteral("2"));
        QSignalSpy options(&backend, &PlexBackend::dynamicOptionsReady);
        errors.clear();
        backend.applyCurrentUserSetting();
        network->last()->fail(500);     // the profiles asked for anew: the kept ones do
        network->last()->fail(500);     // the switch itself
        QCOMPARE(errors.count(), 1);
        QCOMPARE(active(), QStringLiteral("1"));
        QCOMPARE(setting(), QStringLiteral("1"));
        QCOMPARE(options.count(), 1);
        QCOMPARE(options.first().first().toString(), QStringLiteral("current_user_id"));

        // A config.json that can't be read is left as it is: one key written
        // over it would be all the settings there are.
        const QString config = tmp.path() + QStringLiteral("/config.json");
        const QByteArray broken = "{ \"app\": { \"theme\": \"orb\" }, }";
        {
            QFile f(config);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(broken);
        }
        backend.savePlexSetting("current_user_id", QStringLiteral("2"));
        {
            QFile f(config);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), broken);
        }
        QVERIFY(QFile::remove(config));

        // The profile that is active already goes on, on what is kept: its
        // servers' tokens are asked for next.
        const int asked = network->replies.size();
        errors.clear();
        backend.select_user(QStringLiteral("1"), QStringLiteral("0000"));
        network->last()->fail(500);
        QCOMPARE(network->replies.size(), asked + 2);
        QVERIFY(network->last()->url().path().endsWith(QStringLiteral("/resources")));
        network->last()->fail(500);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(active(), QStringLiteral("1"));
    }

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
        JellyfinBackend backend(tmp.path());
        Network *network = signIn(backend);
        QSignalSpy changed(&backend, &JellyfinBackend::authStateChanged);
        backend.check_auth();
        network->replies.first()->complete(QJsonDocument(QJsonObject{ { "Id", "user" } }).toJson());
        QCOMPARE(changed.count(), 0);
        backend.logout();
        QCOMPARE(changed.count(), 1);
    }

    // Jellyfin's and Emby's backends are one code (EmbyApiBackend) but where
    // their servers differ: the header the token goes in, Emby's Continue
    // Watching asked for videos by name, Jellyfin's episodes asked for in air
    // order, the token in a subtitle's URL for Emby, and Emby's device
    // dropped as it signs out, before the token is revoked. The settings
    // page's choices, asked for by name as AppCore does, reach Jellyfin's own
    // (which asks what the server can do too).
    void jellyfinAndEmbyAskEachInItsOwnWay() {
        QTemporaryDir tmp;
        JellyfinBackend jellyfin(tmp.path());
        EmbyBackend emby(tmp.path());
        Network *jf = signIn(jellyfin);
        Network *em = signIn(emby);

        jellyfin.load_continue_watching();
        emby.load_continue_watching();
        const QNetworkRequest jfAsked = jf->last()->request(), emAsked = em->last()->request();
        QVERIFY(jfAsked.rawHeader("Authorization").contains("Token=\"token\""));
        QVERIFY(!jfAsked.hasRawHeader("X-Emby-Token"));
        QVERIFY(emAsked.rawHeader("X-Emby-Authorization").contains("Token=\"token\""));
        QCOMPARE(emAsked.rawHeader("X-Emby-Token"), QByteArray("token"));
        QVERIFY(!emAsked.hasRawHeader("Authorization"));
        QVERIFY(!QUrlQuery(jfAsked.url()).hasQueryItem(QStringLiteral("mediaTypes")));
        QCOMPARE(QUrlQuery(emAsked.url()).queryItemValue(QStringLiteral("mediaTypes")), QStringLiteral("Video"));

        jellyfin.load_episodes(QStringLiteral("show"), QStringLiteral("season"));
        emby.load_episodes(QStringLiteral("show"), QStringLiteral("season"));
        QCOMPARE(QUrlQuery(jf->last()->url()).queryItemValue(QStringLiteral("sortBy")),
                 QStringLiteral("AiredEpisodeOrder"));
        QVERIFY(!QUrlQuery(em->last()->url()).hasQueryItem(QStringLiteral("sortBy")));

        const QJsonObject subtitle{ { "Type", "Subtitle" }, { "Index", 3 }, { "Codec", "subrip" },
                                    { "IsTextSubtitleStream", true } };
        const QJsonObject source{ { "Id", "src" }, { "MediaStreams", QJsonArray{ subtitle } } };
        const QByteArray film = QJsonDocument(QJsonObject{ { "Id", "film" },
                                                           { "MediaSources", QJsonArray{ source } } }).toJson();
        QSignalSpy jfItem(&jellyfin, &JellyfinBackend::itemLoaded);
        QSignalSpy emItem(&emby, &EmbyBackend::itemLoaded);
        jellyfin.load_item_detail(QStringLiteral("film"));
        jf->last()->complete(film);
        emby.load_item_detail(QStringLiteral("film"));
        em->last()->complete(film);
        const auto subUrl = [](const QSignalSpy &spy) {
            return spy.last().at(0).toMap().value(QStringLiteral("subtitleStreams")).toList().first().toMap()
                .value(QStringLiteral("subUrl")).toString();
        };
        QCOMPARE(subUrl(jfItem), QStringLiteral("http://server/Videos/film/src/Subtitles/3/Stream.srt"));
        QCOMPARE(subUrl(emItem), QStringLiteral("http://server/Videos/film/src/Subtitles/3/Stream.srt?api_key=token"));

        qsizetype asked = jf->replies.size();
        QVERIFY(QMetaObject::invokeMethod(&jellyfin, "getVideoQualities"));
        QCOMPARE(jf->replies.size(), asked + 1);
        QVERIFY(jf->last()->url().path().startsWith(QLatin1String("/MediaSegments/")));
        asked = em->replies.size();
        QVERIFY(QMetaObject::invokeMethod(&emby, "getVideoQualities"));
        QCOMPARE(em->replies.size(), asked);

        asked = jf->replies.size();
        jellyfin.logout();
        QCOMPARE(jf->replies.size(), asked + 1);
        QCOMPARE(jf->last()->url().path(), QStringLiteral("/Sessions/Logout"));
        asked = em->replies.size();
        emby.logout();
        QCOMPARE(em->replies.size(), asked + 1);
        QCOMPARE(em->last()->url().path(), QStringLiteral("/Devices"));
        em->last()->complete(QByteArray());
        QCOMPARE(em->last()->url().path(), QStringLiteral("/Sessions/Logout"));
        QCOMPARE(em->last()->request().rawHeader("X-Emby-Token"), QByteArray("token"));
        QVERIFY(!jellyfin.has_auth() && !emby.has_auth());
    }

    // The URL mpv plays a transcode from, made of the server's TranscodingUrl
    // (rewriteTranscodeUrl): Jellyfin's without the token, which mpv sends
    // in a header, Emby's with it, the server's own kept or the backend's
    // added; the subtitle the server took from the item's defaults taken out
    // when the user chose none; the session the one reported with; the
    // height held to Video Quality's, replaced or added, and left as it is
    // for Direct Play's none. And the URL mpv plays the original file from
    // (directStreamUrl), with Emby's token.
    void transcodeAndDirectUrlsAreMadeForMpv() {
        const QString server = QStringLiteral("http://server");
        const QString asked = QStringLiteral("/videos/film/master.m3u8?DeviceId=d&MediaSourceId=film&api_key=old"
                                             "&PlaySessionId=p1&SubtitleStreamIndex=3&SubtitleMethod=Encode&MaxHeight=1080");
        QCOMPARE(EmbyApiBackend::rewriteTranscodeUrl(server, asked, QStringLiteral("p2"), -1, 480, false,
                                                     QStringLiteral("token")),
                 QStringLiteral("http://server/videos/film/master.m3u8?DeviceId=d&MediaSourceId=film"
                                "&PlaySessionId=p2&MaxHeight=480"));
        QCOMPARE(EmbyApiBackend::rewriteTranscodeUrl(server, asked, QStringLiteral("p2"), 3, 0, true,
                                                     QStringLiteral("token")),
                 QStringLiteral("http://server/videos/film/master.m3u8?DeviceId=d&MediaSourceId=film&api_key=old"
                                "&PlaySessionId=p2&SubtitleStreamIndex=3&SubtitleMethod=Encode&MaxHeight=1080"));
        // The token spelt as Jellyfin spells it too, a height by another
        // name, and no session to pin.
        QCOMPARE(EmbyApiBackend::rewriteTranscodeUrl(server, QStringLiteral("/videos/film/master.m3u8?ApiKey=old&height=2160"),
                                                     QString(), 3, 576, false, QString()),
                 QStringLiteral("http://server/videos/film/master.m3u8?MaxHeight=576"));
        QCOMPARE(EmbyApiBackend::rewriteTranscodeUrl(server, QStringLiteral("/videos/film/master.m3u8?MediaSourceId=film"),
                                                     QString(), -1, 720, true, QStringLiteral("token")),
                 QStringLiteral("http://server/videos/film/master.m3u8?MediaSourceId=film&api_key=token&MaxHeight=720"));
        // Signed out meanwhile: no token to add.
        QCOMPARE(EmbyApiBackend::rewriteTranscodeUrl(server, QStringLiteral("/videos/film/master.m3u8?MediaSourceId=film"),
                                                     QString(), -1, 0, true, QString()),
                 QStringLiteral("http://server/videos/film/master.m3u8?MediaSourceId=film"));

        QCOMPARE(EmbyApiBackend::directStreamUrl(server, QStringLiteral("film"), QStringLiteral("src"),
                                                 QStringLiteral("p1"), QString()),
                 QStringLiteral("http://server/Videos/film/stream?static=true&mediaSourceId=src&PlaySessionId=p1"));
        QCOMPARE(EmbyApiBackend::directStreamUrl(server, QStringLiteral("film"), QStringLiteral("src"),
                                                 QString(), QStringLiteral("token")),
                 QStringLiteral("http://server/Videos/film/stream?static=true&mediaSourceId=src&api_key=token"));
    }

    // The body a request sent, and the methods its device profile says a
    // subtitle format comes by.
    static QJsonObject sentBody(const Reply *reply) { return QJsonDocument::fromJson(reply->sent).object(); }
    static QString subtitleMethods(const QJsonObject &body, const char *format) {
        QStringList methods;
        for (const QJsonValue &v : body["DeviceProfile"].toObject()["SubtitleProfiles"].toArray()) {
            if (v.toObject()["Format"].toString() == QLatin1String(format))
                methods << v.toObject()["Method"].toString();
        }
        return methods.join(QLatin1Char(','));
    }
    // Settings → Video Quality for both servers, in config.json.
    static void setVideoQuality(const QString &dataRoot, const char *quality) {
        QFile config(dataRoot + QStringLiteral("/config.json"));
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QJsonObject module{ { "video_quality", quality } };
        config.write(QJsonDocument(QJsonObject{ { "modules", QJsonObject{ { "com.osdos.jellyfin", module },
                                                                          { "com.osdos.emby", module } } } }).toJson());
    }

    // What PlaybackInfo is asked for, each server's way: Direct Play (Video
    // Quality Auto) with a profile that matches anything; Jellyfin's text
    // subtitles as sidecars or embedded, burnt in for a transcode, and
    // Emby's always as sidecars, its image ones burnt in for a transcode;
    // Jellyfin's Direct Play bitrate at 1000 Mbps, Emby's in its profile,
    // which it needs; a quality's caps for a transcode. Direct Play's URL
    // carries Emby's token and not Jellyfin's; a transcode's is held to the
    // height asked for.
    void playbackInfoIsAskedEachServersWay() {
        QTemporaryDir tmp;
        JellyfinBackend jellyfin(tmp.path());
        EmbyBackend emby(tmp.path());
        Network *jf = signIn(jellyfin);
        Network *em = signIn(emby);
        QSignalSpy jfReady(&jellyfin, &JellyfinBackend::streamUrlReady);
        QSignalSpy emReady(&emby, &EmbyBackend::streamUrlReady);

        jellyfin.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1);
        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1);
        Reply *jfInfo = jf->last(), *emInfo = em->last();
        QJsonObject jfBody = sentBody(jfInfo), emBody = sentBody(emInfo);
        QVERIFY(jfInfo->url().path().endsWith(QLatin1String("/Items/film/PlaybackInfo")));
        QVERIFY(jfBody["EnableDirectPlay"].toBool() && emBody["EnableDirectPlay"].toBool());
        QCOMPARE(jfBody["MaxStreamingBitrate"].toInt(), 1000000000);
        QVERIFY(!emBody.contains(QLatin1String("MaxStreamingBitrate")));
        QVERIFY(!jfBody.contains(QLatin1String("MaxHeight")) && !emBody.contains(QLatin1String("MaxHeight")));
        QVERIFY(!jfBody["DeviceProfile"].toObject().contains(QLatin1String("MaxStreamingBitrate")));
        QCOMPARE(emBody["DeviceProfile"].toObject()["MaxStreamingBitrate"].toInt(), 2000000000);
        QCOMPARE(jfBody["DeviceProfile"].toObject()["DirectPlayProfiles"].toArray().size(), 1);
        QCOMPARE(emBody["DeviceProfile"].toObject()["DirectPlayProfiles"].toArray().size(), 1);
        QCOMPARE(subtitleMethods(jfBody, "subrip"), QStringLiteral("External,Embed"));
        QCOMPARE(subtitleMethods(jfBody, "pgssub"), QStringLiteral("Embed"));
        QCOMPARE(subtitleMethods(emBody, "subrip"), QStringLiteral("External"));
        QCOMPARE(subtitleMethods(emBody, "pgssub"), QStringLiteral("Embed"));
        const QByteArray direct = QJsonDocument(QJsonObject{
            { "PlaySessionId", "ps" },
            { "MediaSources", QJsonArray{ QJsonObject{ { "Id", "film" }, { "SupportsDirectPlay", true } } } } }).toJson();
        jfInfo->complete(direct);
        emInfo->complete(direct);
        QCOMPARE(jfReady.last().at(0).toString(),
                 QStringLiteral("http://server/Videos/film/stream?static=true&mediaSourceId=film&PlaySessionId=ps"));
        QCOMPARE(emReady.last().at(0).toString(),
                 QStringLiteral("http://server/Videos/film/stream?static=true&mediaSourceId=film&PlaySessionId=ps"
                                "&api_key=token"));

        setVideoQuality(tmp.path(), "720p");
        jellyfin.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, 2);
        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, 2);
        jfInfo = jf->last();
        emInfo = em->last();
        jfBody = sentBody(jfInfo);
        emBody = sentBody(emInfo);
        for (const QJsonObject &body : { jfBody, emBody }) {
            QVERIFY(!body["EnableDirectPlay"].toBool());
            QCOMPARE(body["MaxStreamingBitrate"].toInt(), 6000000);
            QCOMPARE(body["MaxHeight"].toInt(), 720);
            QCOMPARE(body["SubtitleStreamIndex"].toInt(), 2);
            QVERIFY(body["DeviceProfile"].toObject()["DirectPlayProfiles"].toArray().isEmpty());
        }
        QCOMPARE(emBody["DeviceProfile"].toObject()["MaxStreamingBitrate"].toInt(), 6000000);
        QCOMPARE(subtitleMethods(jfBody, "subrip"), QStringLiteral("Encode"));
        QCOMPARE(subtitleMethods(jfBody, "webvtt"), QString());
        QCOMPARE(subtitleMethods(jfBody, "pgssub"), QStringLiteral("Encode"));
        QCOMPARE(subtitleMethods(emBody, "subrip"), QStringLiteral("External"));
        QCOMPARE(subtitleMethods(emBody, "pgssub"), QStringLiteral("Encode"));
        // Video Quality changed before the server answers: the transcode is
        // held to the height it was asked for at.
        setVideoQuality(tmp.path(), "480p");
        const QByteArray transcode = QJsonDocument(QJsonObject{
            { "PlaySessionId", "ps2" },
            { "MediaSources", QJsonArray{ QJsonObject{ { "Id", "film" }, { "TranscodingUrl",
                "/videos/film/master.m3u8?api_key=server&PlaySessionId=ps2&SubtitleStreamIndex=2&MaxHeight=1080" } } } } }).toJson();
        jfInfo->complete(transcode);
        emInfo->complete(transcode);
        QCOMPARE(jfReady.last().at(0).toString(),
                 QStringLiteral("http://server/videos/film/master.m3u8?PlaySessionId=ps2&SubtitleStreamIndex=2&MaxHeight=720"));
        QCOMPARE(emReady.last().at(0).toString(),
                 QStringLiteral("http://server/videos/film/master.m3u8?api_key=server&PlaySessionId=ps2"
                                "&SubtitleStreamIndex=2&MaxHeight=720"));
    }

    // Made as main.cpp makes it, with AppCore, a backend reads its settings
    // through it, a value AppCore holds back from config.json included (a
    // slider's steps), and a library turned off in Settings is hidden.
    // Registered (registerModule), it is wired by what it declares, its base
    // class's included: the choices the settings page asked for and a
    // sign-out reach AppCore, and a Server Address changed reaches it.
    void settingsComeThroughAppCore() {
        QTemporaryDir tmp;
        AppCore appCore(tmp.path(), tmp.path());
        EmbyBackend emby(tmp.path(), &appCore);
        Network *network = signIn(emby);
        appCore.registerModule(QStringLiteral("com.osdos.emby"), QStringLiteral("embyBackend"), &emby, nullptr);
        QSignalSpy options(&appCore, &AppCore::dynamicOptionsReady);
        QSignalSpy auth(&appCore, &AppCore::moduleAuthStateChanged);

        QVERIFY(appCore.save_setting_soon(QStringLiteral("com.osdos.emby"), QStringLiteral("video_quality"),
                                          QStringLiteral("720p")));
        {
            QFile config(tmp.path() + QStringLiteral("/config.json"));
            QVERIFY(!config.open(QIODevice::ReadOnly) || !config.readAll().contains("720p"));
        }
        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), -1, -1);
        QCOMPARE(sentBody(network->last())["MaxHeight"].toInt(), 720);

        QVERIFY(appCore.save_setting(QStringLiteral("com.osdos.emby"), QStringLiteral("libraries.2"), false));
        QSignalSpy libraries(&emby, &EmbyBackend::librariesLoaded);
        emby.load_libraries();
        const QJsonArray views{ QJsonObject{ { "Id", "1" }, { "Name", "Films" }, { "CollectionType", "movies" } },
                                QJsonObject{ { "Id", "2" }, { "Name", "Shows" }, { "CollectionType", "tvshows" } } };
        network->last()->complete(QJsonDocument(QJsonObject{ { "Items", views } }).toJson());
        network->last()->complete(films({}));    // Continue Watching: nothing
        network->last()->complete(films({}));    // Next Up: nothing
        QCOMPARE(libraries.count(), 1);
        QCOMPARE(titles(libraries, 0), QStringList({ "FILMS" }));

        emby.getVideoQualities();
        QCOMPARE(options.count(), 1);
        QCOMPARE(options.first().at(0).toString(), QStringLiteral("com.osdos.emby"));
        QCOMPARE(options.first().at(1).toString(), QStringLiteral("video_quality"));

        QVERIFY(appCore.save_setting(QStringLiteral("com.osdos.emby"), QStringLiteral("server_url"),
                                     QStringLiteral("http://elsewhere")));
        QVERIFY(!emby.has_auth());
        QCOMPARE(auth.count(), 1);
        QCOMPARE(auth.first().at(0).toString(), QStringLiteral("com.osdos.emby"));
    }

    // An Emby source in Dolby Vision, which mpv can't show, is asked for
    // again as a transcode to SDR held to 1080p and 20 Mbps (Video Quality
    // Auto caps none), an image subtitle dropped (burnt in, it would fail
    // the transcode) and a text one kept; the transcode's URL keeps the
    // server's height, Auto having none of its own. The cap is the re-ask's
    // alone: the next stream is asked for as before.
    void embyDolbyVisionIsTranscodedCapped() {
        QTemporaryDir tmp;
        EmbyBackend emby(tmp.path());
        Network *network = signIn(emby);
        QSignalSpy ready(&emby, &EmbyBackend::streamUrlReady);
        const auto dolbyVision = [](int subtitle, bool text) {
            const QJsonArray streams{ QJsonObject{ { "Type", "Video" }, { "VideoRangeType", "DOVI" } },
                                      QJsonObject{ { "Type", "Subtitle" }, { "Index", subtitle },
                                                   { "IsTextSubtitleStream", text } } };
            return QJsonDocument(QJsonObject{ { "PlaySessionId", "ps" }, { "MediaSources", QJsonArray{ QJsonObject{
                { "Id", "film" }, { "SupportsDirectPlay", true }, { "MediaStreams", streams } } } } }).toJson();
        };

        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), 1, 4);
        network->last()->complete(dolbyVision(4, false));
        QCOMPARE(ready.count(), 0);
        QJsonObject body = sentBody(network->last());
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/Items/film/PlaybackInfo")));
        QVERIFY(!body["EnableDirectPlay"].toBool());
        QCOMPARE(body["MaxStreamingBitrate"].toInt(), 20000000);
        QCOMPARE(body["MaxHeight"].toInt(), 1080);
        QCOMPARE(body["DeviceProfile"].toObject()["MaxStreamingBitrate"].toInt(), 20000000);
        QCOMPARE(body["AudioStreamIndex"].toInt(), 1);
        QVERIFY(!body.contains(QLatin1String("SubtitleStreamIndex")));
        network->last()->complete(QJsonDocument(QJsonObject{ { "PlaySessionId", "t" }, { "MediaSources", QJsonArray{
            QJsonObject{ { "Id", "film" }, { "TranscodingUrl", "/videos/film/master.m3u8?Height=2160&PlaySessionId=t" } } } } })
                                      .toJson());
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready.last().at(0).toString(),
                 QStringLiteral("http://server/videos/film/master.m3u8?Height=2160&PlaySessionId=t&api_key=token"));

        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), 1, 5);
        network->last()->complete(dolbyVision(5, true));
        QCOMPARE(sentBody(network->last())["SubtitleStreamIndex"].toInt(), 5);

        // A transcode the player forces (a track switched in the middle of
        // one, a direct play that failed) is held to the same cap, and drops
        // an image subtitle the same way.
        emby.get_playback_url(QStringLiteral("film"), QStringLiteral("film"), 2, 4, /*forceTranscode=*/true);
        network->last()->complete(dolbyVision(4, false));
        body = sentBody(network->last());
        QVERIFY(!body["EnableDirectPlay"].toBool());
        QCOMPARE(body["MaxStreamingBitrate"].toInt(), 20000000);
        QCOMPARE(body["MaxHeight"].toInt(), 1080);
        QCOMPARE(body["AudioStreamIndex"].toInt(), 2);
        QVERIFY(!body.contains(QLatin1String("SubtitleStreamIndex")));
        const int streams = ready.count();
        network->last()->complete(QJsonDocument(QJsonObject{ { "PlaySessionId", "t" }, { "MediaSources", QJsonArray{
            QJsonObject{ { "Id", "film" }, { "TranscodingUrl", "/videos/film/master.m3u8?PlaySessionId=t" } } } } })
                                      .toJson());
        QCOMPARE(ready.count(), streams + 1);
        QVERIFY(ready.last().at(0).toString().startsWith(QLatin1String("http://server/videos/film/master.m3u8")));

        emby.get_playback_url(QStringLiteral("other"), QStringLiteral("other"), -1, -1);
        body = sentBody(network->last());
        QVERIFY(body["EnableDirectPlay"].toBool());
        QVERIFY(!body.contains(QLatin1String("MaxStreamingBitrate")));
        QVERIFY(!body.contains(QLatin1String("MaxHeight")));
        QCOMPARE(body["DeviceProfile"].toObject()["MaxStreamingBitrate"].toInt(), 2000000000);
    }

    // Quick Connect says what the server answered, as it starts and at the
    // exchange after the approval: a 401 to the start is Quick Connect
    // turned off on the server, any other refusal the server's error with
    // its status, and only no answer at all, with no status either, a
    // connection that failed, in the network's own words. Qt marks every
    // answer of 400 and above as an error too: asked first, the error hid
    // the status behind Qt's words for it.
    void jellyfinQuickConnectSaysWhatTheServerAnswered() {
        QTemporaryDir tmp;
        JellyfinBackend backend(tmp.path());
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        QSignalSpy failed(&backend, &JellyfinBackend::quickConnectFailed);
        QSignalSpy errors(&backend, &JellyfinBackend::errorOccurred);
        QSignalSpy code(&backend, &JellyfinBackend::quickConnectCodeReady);
        QSignalSpy approved(&backend, &JellyfinBackend::quickConnectApproved);
        QSignalSpy changed(&backend, &JellyfinBackend::authStateChanged);
        const QString server = QStringLiteral("http://server");

        // The start.
        backend.quick_connect_initiate(server);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/QuickConnect/Initiate")));
        network->last()->fail(401);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("QUICK CONNECT NOT ENABLED ON SERVER"));
        backend.quick_connect_initiate(server);
        network->last()->fail(503);
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QStringLiteral("SERVER ERROR (HTTP 503)"));
        backend.quick_connect_initiate(server);
        network->last()->drop();
        QCOMPARE(failed.count(), 3);
        QCOMPARE(failed.at(2).at(0).toString(), QStringLiteral("CONNECTION FAILED: Connection refused"));
        QCOMPARE(code.count(), 0);
        backend.quick_connect_initiate(server);
        network->last()->complete(QJsonDocument(QJsonObject{ { "Code", "123456" }, { "Secret", "secret" } }).toJson());
        QCOMPARE(code.count(), 1);
        QCOMPARE(code.at(0).at(0).toString(), QStringLiteral("123456"));
        QCOMPARE(failed.count(), 3);

        // The wait for the approval, which reads the status first already: a
        // poll that didn't get through says nothing, the next one asks again.
        backend.quick_connect_poll(QStringLiteral("secret"));
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/QuickConnect/Connect")));
        network->last()->drop();
        QCOMPARE(failed.count(), 3);
        backend.quick_connect_poll(QStringLiteral("secret"));
        network->last()->fail(404);
        QCOMPARE(failed.count(), 4);
        QCOMPARE(failed.at(3).at(0).toString(), QStringLiteral("CODE EXPIRED — RETRY"));
        backend.quick_connect_poll(QStringLiteral("secret"));
        network->last()->fail(500);
        QCOMPARE(failed.count(), 5);
        QCOMPARE(failed.at(4).at(0).toString(), QStringLiteral("SERVER ERROR (HTTP 500)"));
        QCOMPARE(approved.count(), 0);
        backend.quick_connect_poll(QStringLiteral("secret"));
        network->last()->complete(QJsonDocument(QJsonObject{ { "Authenticated", true } }).toJson());
        QCOMPARE(approved.count(), 1);
        QCOMPARE(errors.count(), 0);

        // The exchange after the approval.
        backend.quick_connect_authenticate(QStringLiteral("secret"));
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/Users/AuthenticateWithQuickConnect")));
        network->last()->fail(401);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.at(0).at(0).toString(), QStringLiteral("AUTH FAILED (HTTP 401)"));
        backend.quick_connect_authenticate(QStringLiteral("secret"));
        network->last()->fail(500);
        QCOMPARE(errors.count(), 2);
        QCOMPARE(errors.at(1).at(0).toString(), QStringLiteral("AUTH FAILED (HTTP 500)"));
        backend.quick_connect_authenticate(QStringLiteral("secret"));
        network->last()->drop();
        QCOMPARE(errors.count(), 3);
        QCOMPARE(errors.at(2).at(0).toString(), QStringLiteral("AUTH FAILED: Connection refused"));
        QVERIFY(!backend.has_auth());
        QCOMPARE(changed.count(), 0);
        backend.quick_connect_authenticate(QStringLiteral("secret"));
        network->last()->complete(QJsonDocument(QJsonObject{
            { "AccessToken", "token" },
            { "User", QJsonObject{ { "Id", "user" }, { "Name", "User" } } } }).toJson());
        QVERIFY(backend.has_auth());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(errors.count(), 3);
        QCOMPARE(failed.count(), 5);
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

    // A live channel left while it tuned (back, then another channel): its
    // answer, coming late, starts no transcode and reaches no player, and
    // the tuner it took is let go of; the channel asked for after it plays.
    // One left as its transcode starts has the transcode stopped again.
    void plexChannelTunedForAPlayerLeftIsDropped() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        backend.m_liveDvrId = QStringLiteral("dvr");
        QSignalSpy ready(&backend, &PlexBackend::streamUrlReady);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        const auto tuned = [](const QString &key) {
            const QJsonObject grab{ { "Metadata", QJsonObject{ { "key", key }, { "ratingKey", "airing" },
                                                               { "duration", 1800000 } } } };
            return plexContainer({ { "MediaSubscription",
                                     QJsonArray{ QJsonObject{ { "MediaGrabOperation", QJsonArray{ grab } } } } } });
        };
        const auto asked = [](const Reply *reply, const char *name) {
            return QUrlQuery(reply->url()).queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
        };
        backend.tune_channel(QStringLiteral("A"), QStringLiteral("sessionA"));
        Reply *a = network->last();
        QVERIFY(a->url().path().endsWith(QLatin1String("/channels/A/tune")));
        // Back while it tunes, as LivePlayer's teardown has it.
        backend.cancel_stream();
        backend.stop_live_session(QStringLiteral("sessionA"));
        backend.tune_channel(QStringLiteral("B"), QStringLiteral("sessionB"));
        Reply *b = network->last();
        QVERIFY(b->url().path().endsWith(QLatin1String("/channels/B/tune")));
        const qsizetype before = network->replies.size();
        a->complete(tuned(QStringLiteral("/livetv/sessions/a")));
        // Only its timeline, stopped: no transcode started for it.
        QCOMPARE(network->replies.size(), before + 1);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/:/timeline")));
        QCOMPARE(asked(network->last(), "state"), QStringLiteral("stopped"));
        QCOMPARE(asked(network->last(), "key"), QStringLiteral("/livetv/sessions/a"));
        QCOMPARE(asked(network->last(), "X-Plex-Session-Identifier"), QStringLiteral("sessionA"));
        QVERIFY(backend.m_liveTimelineKey.isEmpty());
        QCOMPARE(ready.count(), 0);
        // The channel asked for after it: tuned, transcoded and played.
        b->complete(tuned(QStringLiteral("/livetv/sessions/b")));
        Reply *start = network->last();
        QVERIFY(start->url().path().endsWith(QLatin1String("/transcode/universal/start.m3u8")));
        QCOMPARE(asked(start, "session"), QStringLiteral("sessionB"));
        QCOMPARE(asked(start, "path"), QStringLiteral("/livetv/sessions/b"));
        start->complete("#EXTM3U\nsession/b/base/index.m3u8\n");
        QCOMPARE(ready.count(), 1);
        QVERIFY(ready.first().at(0).toString().endsWith(QLatin1String("session/b/base/index.m3u8")));
        QCOMPARE(backend.m_liveSessionId, QStringLiteral("sessionB"));

        // Left as its transcode starts: the stop went before the transcode
        // had begun, so it is stopped again as the server answers.
        backend.cancel_stream();
        backend.stop_live_session(QStringLiteral("sessionB"));
        backend.tune_channel(QStringLiteral("C"), QStringLiteral("sessionC"));
        network->last()->complete(tuned(QStringLiteral("/livetv/sessions/c")));
        Reply *late = network->last();
        QCOMPARE(asked(late, "session"), QStringLiteral("sessionC"));
        backend.cancel_stream();
        backend.stop_live_session(QStringLiteral("sessionC"));
        const qsizetype stopped = network->replies.size();
        late->complete("#EXTM3U\nsession/c/base/index.m3u8\n");
        QCOMPARE(network->replies.size(), stopped + 1);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/transcode/universal/stop")));
        QCOMPARE(asked(network->last(), "session"), QStringLiteral("sessionC"));
        QCOMPARE(ready.count(), 1);
        // A tune that fails for a player that has gone throws no other back.
        backend.tune_channel(QStringLiteral("D"), QStringLiteral("sessionD"));
        Reply *failing = network->last();
        backend.cancel_stream();
        failing->fail(500);
        QCOMPARE(errors.count(), 0);
    }

    // A server that no longer answers at the address kept for it since the
    // sign-in (the router gave it another over a power cut): its connections
    // are asked of plex.tv again and probed, the one that answers is kept,
    // and the libraries come from there. With none answering the failure is
    // said and the address stays; a refusal (an HTTP status) probes nothing.
    void plexServerThatMovedIsFoundAgain() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QJsonObject auth = backend.loadAuth();
        auth["active_server_machine_id"] = QStringLiteral("machine");
        auth["servers"] = QJsonArray{
            QJsonObject{ { "machineId", "other" }, { "name", "AWAY" }, { "uri", "http://other" } },
            QJsonObject{ { "machineId", "machine" }, { "name", "HOME" }, { "uri", "http://server" } } };
        backend.saveAuth(auth);
        QSignalSpy libraries(&backend, &PlexBackend::librariesLoaded);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        const auto resources = [](const QJsonArray &connections) {
            return QJsonDocument(QJsonArray{
                QJsonObject{ { "clientIdentifier", "other" }, { "provides", "server" },
                             { "connections", QJsonArray{ QJsonObject{ { "uri", "http://elsewhere" }, { "local", true } } } } },
                QJsonObject{ { "clientIdentifier", "machine" }, { "provides", "server" },
                             { "connections", connections } } }).toJson();
        };
        const auto uris = [&backend]() {
            QStringList list;
            for (const auto &v : backend.loadAuth()["servers"].toArray())
                list << v.toObject()["uri"].toString();
            return list;
        };

        backend.load_libraries_impl();
        network->last()->drop();    // Continue Watching
        Reply *sections = network->last();
        QVERIFY(sections->url().path().endsWith(QLatin1String("/library/sections")));
        sections->drop();
        Reply *asked = network->last();
        QCOMPARE(asked->url().host(), QStringLiteral("plex.tv"));
        QVERIFY(asked->url().path().endsWith(QLatin1String("/api/v2/resources")));
        QCOMPARE(errors.count(), 0);
        // Its local connections first, then the relay: the first doesn't
        // answer, the second does.
        asked->complete(resources({ QJsonObject{ { "uri", "http://relay" }, { "relay", true } },
                                    QJsonObject{ { "uri", "http://old" }, { "local", true } },
                                    QJsonObject{ { "uri", "http://moved" }, { "local", true } } }));
        QCOMPARE(network->last()->url().host(), QStringLiteral("old"));
        network->last()->drop();
        QCOMPARE(network->last()->url().host(), QStringLiteral("moved"));
        network->last()->complete(QByteArray());
        QCOMPARE(backend.loadAuth()["active_server_uri"].toString(), QStringLiteral("http://moved"));
        QCOMPARE(uris(), QStringList({ QStringLiteral("http://other"), QStringLiteral("http://moved") }));
        // The libraries, asked for again there.
        QCOMPARE(network->last()->url().host(), QStringLiteral("moved"));
        network->last()->complete(plexContainer({}));    // Continue Watching
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/library/sections")));
        QCOMPARE(network->last()->url().host(), QStringLiteral("moved"));
        network->last()->complete(plexContainer({ { "Directory", QJsonArray{ QJsonObject{
            { "key", "1" }, { "title", "Films" }, { "type", "movie" } } } } }));
        network->last()->complete(plexContainer({}));    // /livetv/dvrs
        QCOMPARE(libraries.count(), 1);
        QCOMPARE(errors.count(), 0);

        // Nothing answers where plex.tv says it is: said once, the address kept.
        backend.load_libraries_impl();
        network->last()->drop();
        network->last()->drop();
        network->last()->complete(resources({ QJsonObject{ { "uri", "http://gone" }, { "local", true } } }));
        QCOMPARE(network->last()->url().host(), QStringLiteral("gone"));
        network->last()->drop();
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().at(0).toString().startsWith(QLatin1String("LOAD LIBRARIES FAILED")));
        QCOMPARE(backend.loadAuth()["active_server_uri"].toString(), QStringLiteral("http://moved"));

        // The server's own refusal: nothing probed.
        backend.load_libraries_impl();
        network->last()->drop();
        Reply *refused = network->last();
        refused->fail(500);
        QCOMPARE(network->last(), refused);
        QCOMPARE(errors.count(), 2);

        // Probed once for each failure: the address it found failing too is
        // said, not probed for again without end.
        backend.load_libraries_impl();
        network->last()->drop();
        network->last()->drop();
        network->last()->complete(resources({ QJsonObject{ { "uri", "http://moved" }, { "local", true } } }));
        network->last()->complete(QByteArray());    // the probe
        network->last()->drop();                    // Continue Watching
        Reply *again = network->last();
        again->drop();                              // the libraries
        QCOMPARE(network->last(), again);
        QCOMPARE(errors.count(), 3);
    }

    // Emby Connect lists a server with its LAN address and the one it has
    // from outside. The LAN one is tried first; when it gives no exchange (a
    // server in another house, or in a container that names an address of
    // its own) the other is, and the sign-in is with that one. The failure
    // is said only when both fail, and at once for a server with one address.
    void embyConnectTriesTheRemoteAddress() {
        QTemporaryDir tmp;
        EmbyBackend backend(tmp.path());
        delete backend.m_nam;
        auto *network = new Network(&backend);
        backend.m_nam = network;
        backend.m_connectUserId = QStringLiteral("exampleplayer");
        backend.m_connectAccessToken = QStringLiteral("Tok123exampleABC");
        QSignalSpy failed(&backend, &EmbyBackend::connectFailed);
        QSignalSpy changed(&backend, &EmbyBackend::authStateChanged);
        QSignalSpy picker(&backend, &EmbyBackend::connectServersReady);
        const auto server = [](const char *name, const char *local, const char *remote, const char *key) {
            return QJsonObject{ { "Name", name }, { "LocalAddress", local }, { "Url", remote },
                                { "AccessKey", key }, { "SystemId", name } };
        };

        // One server: exchanged at once, at its LAN address.
        backend.fetchConnectServers();
        network->last()->complete(QJsonDocument(QJsonArray{
            server("Away", "http://172.17.0.2:8096/", "https://away.example:8920", "KeyAway123") }).toJson());
        Reply *local = network->last();
        QCOMPARE(local->url().host(), QStringLiteral("172.17.0.2"));
        QVERIFY(local->url().path().endsWith(QLatin1String("/Connect/Exchange")));
        local->drop();
        Reply *remote = network->last();
        QVERIFY(remote != local);
        QCOMPARE(remote->url().host(), QStringLiteral("away.example"));
        QVERIFY(remote->url().path().endsWith(QLatin1String("/Connect/Exchange")));
        QCOMPARE(remote->request().rawHeader("X-Emby-Token"), QByteArray("KeyAway123"));
        QCOMPARE(failed.count(), 0);
        remote->complete(QJsonDocument(QJsonObject{ { "LocalUserId", "user" },
                                                    { "AccessToken", "Tok456exampleDEF" } }).toJson());
        QCOMPARE(backend.m_serverUrl, QStringLiteral("https://away.example:8920"));
        QCOMPARE(backend.m_accessToken, QStringLiteral("Tok456exampleDEF"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(failed.count(), 0);

        // Two servers, picked from as the picker does (by address and key):
        // both addresses failing is said once, after the second.
        backend.fetchConnectServers();
        network->last()->complete(QJsonDocument(QJsonArray{
            server("Home", "http://192.168.1.50:8096", "https://home.example", "KeyHome123"),
            server("Cabin", "", "https://cabin.example", "KeyCabin123") }).toJson());
        QCOMPARE(picker.count(), 1);
        const QVariantList listed = picker.first().at(0).toList();
        QCOMPARE(listed.size(), 2);
        const QVariantMap home = listed.at(0).toMap(), cabin = listed.at(1).toMap();
        backend.connect_select_server(home.value(QStringLiteral("address")).toString(),
                                      home.value(QStringLiteral("accessKey")).toString());
        QCOMPARE(network->last()->url().host(), QStringLiteral("192.168.1.50"));
        network->last()->fail(404);    // something else answers there
        QCOMPARE(failed.count(), 0);
        QCOMPARE(network->last()->url().host(), QStringLiteral("home.example"));
        network->last()->fail(502);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("SERVER EXCHANGE FAILED (HTTP 502)"));
        // One address only: its failure at once, nothing else tried.
        backend.connect_select_server(cabin.value(QStringLiteral("address")).toString(),
                                      cabin.value(QStringLiteral("accessKey")).toString());
        Reply *only = network->last();
        QCOMPARE(only->url().host(), QStringLiteral("cabin.example"));
        only->drop();
        QCOMPARE(network->last(), only);
        QCOMPARE(failed.count(), 2);
    }

    // A sign-in whose account didn't come once the code was claimed (plex.tv
    // a moment away) leaves a token and no one to choose: the users are asked
    // for again as their page opens, not shown as none for ever. Their
    // servers not coming is said, rather than answered with users nothing
    // was kept of; once both have come they are kept, and asked for no more.
    void plexSignInWithoutItsAccountAsksForItAgain() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        backend.saveAuth(QJsonObject{ { "auth_token", "token" } });
        QCOMPARE(backend.get_auth_state(), QStringLiteral("needs_user"));
        QSignalSpy users(&backend, &PlexBackend::usersLoaded);
        QSignalSpy errors(&backend, &PlexBackend::errorOccurred);
        const QByteArray home = QJsonDocument(QJsonObject{ { "users", QJsonArray{
            QJsonObject{ { "id", 1 }, { "uuid", "owner" }, { "title", "Owner" } } } } }).toJson();

        backend.load_users_from_cache();
        QCOMPARE(network->replies.size(), 1);
        QCOMPARE(network->last()->url().host(), QStringLiteral("plex.tv"));
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/api/v2/home/users")));
        QCOMPARE(users.count(), 0);
        network->last()->drop();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.last().at(0).toString(), QStringLiteral("FAILED TO LOAD ACCOUNT: Connection refused"));
        QCOMPARE(users.count(), 0);

        // The users come, their servers don't.
        backend.load_users_from_cache();
        network->last()->complete(home);
        QVERIFY(network->last()->url().path().endsWith(QLatin1String("/api/v2/resources")));
        network->last()->fail(500);
        QCOMPARE(errors.count(), 2);
        QCOMPARE(errors.last().at(0).toString(), QStringLiteral("FAILED TO LOAD SERVERS: HTTP 500"));
        QCOMPARE(users.count(), 0);
        QVERIFY(backend.loadAuth()["users"].toArray().isEmpty());

        // Both come: kept, the server at the address that answers.
        backend.load_users_from_cache();
        network->last()->complete(home);
        network->last()->complete(QJsonDocument(QJsonArray{ QJsonObject{
            { "clientIdentifier", "machine" }, { "name", "Home" }, { "provides", "server" },
            { "accessToken", "server-token" },
            { "connections", QJsonArray{ QJsonObject{ { "uri", "http://server" }, { "local", true } } } } } }).toJson());
        QCOMPARE(network->last()->url().host(), QStringLiteral("server"));
        network->last()->complete(QByteArray());
        QCOMPARE(users.count(), 1);
        QCOMPARE(users.last().at(0).toList().size(), 1);
        QCOMPARE(backend.loadAuth()["users"].toArray().size(), 1);
        QCOMPARE(backend.loadAuth()["servers"].toArray().size(), 1);
        QCOMPARE(errors.count(), 2);
        const qsizetype asked = network->replies.size();
        backend.load_users_from_cache();
        QCOMPARE(network->replies.size(), asked);
        QCOMPARE(users.count(), 2);
        QCOMPARE(users.last().at(0).toList().size(), 1);
    }

    // An NFC card's item looked for, and the card given up (back) or another
    // tapped before it came: the first's answer, coming late, plays in no
    // player and fails none, at whichever step it had got to, and nothing
    // more is asked for it; the card asked for last is the one told. A
    // set's rows the same.
    void plexCardLeftAnswersNoOne() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy item(&backend, &PlexBackend::cardItemReady);
        QSignalSpy rows(&backend, &PlexBackend::cardQueueReady);
        QSignalSpy errors(&backend, &PlexBackend::cardError);
        const auto found = [](const QString &key) { return plexContainer({ { "Metadata", plexFilms({ key }) } }); };

        // Given up as its guid is looked for: its item is not asked for.
        backend.resolve_card(QStringLiteral("plex://movie/a"), QString());
        Reply *a = network->last();
        backend.cancel_stream();
        backend.resolve_card(QStringLiteral("plex://movie/b"), QString());
        Reply *b = network->last();
        QVERIFY(a != b);
        const qsizetype asked = network->replies.size();
        a->complete(found(QStringLiteral("1")));
        QCOMPARE(network->replies.size(), asked);
        b->complete(found(QStringLiteral("2")));
        // Another card tapped as the item itself is fetched.
        Reply *second = network->last();
        QVERIFY(second->url().path().endsWith(QLatin1String("/library/metadata/2")));
        backend.resolve_card(QStringLiteral("plex://movie/c"), QString());
        Reply *c = network->last();
        second->complete(found(QStringLiteral("2")));
        QCOMPARE(item.count(), 0);
        c->complete(found(QStringLiteral("3")));
        network->last()->complete(found(QStringLiteral("3")));
        QCOMPARE(item.count(), 1);
        QCOMPARE(item.first().at(0).toMap().value(QStringLiteral("ratingKey")).toString(), QStringLiteral("3"));

        // A card left that fails says nothing in the next one's place, and a
        // set left answers with no rows.
        backend.resolve_card(QStringLiteral("plex://movie/d"), QString());
        Reply *d = network->last();
        backend.resolve_card_queue(QStringLiteral("plex://collection/7"));
        Reply *set = network->last();
        d->fail(500);
        QCOMPARE(errors.count(), 0);
        backend.cancel_stream();
        set->complete(plexContainer({ { "Metadata", plexFilms({ "8", "9" }) } }));
        QCOMPARE(rows.count(), 0);
        backend.resolve_card_queue(QStringLiteral("plex://collection/7"));
        network->last()->complete(plexContainer({ { "Metadata", plexFilms({ "8", "9" }) } }));
        QCOMPARE(rows.count(), 1);
        QCOMPARE(rows.first().at(0).toList().size(), 2);
        // The card asked for last failing is said.
        backend.resolve_card(QStringLiteral("plex://movie/e"), QString());
        network->last()->fail(500);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(item.count(), 1);
    }

    // A show shuffled: its episodes are asked for once (its seasons, then
    // each season's episodes) and the bag made of them is kept, so the next
    // draw asks for the episode drawn and nothing else; only a bag run out
    // asks for the episodes again.
    void plexShuffleDrawsFromTheBagItKept() {
        QTemporaryDir tmp;
        PlexBackend backend(tmp.path(), tmp.path());
        Network *network = signInPlex(backend, tmp.path());
        QVERIFY(network);
        QSignalSpy next(&backend, &PlexBackend::nextEpisodeReady);
        const QJsonArray seasons{ QJsonObject{ { "ratingKey", "s1" }, { "type", "season" } },
                                  QJsonObject{ { "ratingKey", "s2" }, { "type", "season" } } };
        // An episode drawn for the show, every request it makes answered:
        // how many there were.
        const auto draw = [&]() {
            const qsizetype before = network->replies.size();
            backend.load_random_episode(QStringLiteral("show"));
            for (qsizetype i = before; i < network->replies.size(); ++i) {
                Reply *reply = network->replies.at(i);
                const QString path = reply->url().path();
                if (path.endsWith(QLatin1String("/show/children")))
                    reply->complete(plexContainer({ { "Metadata", seasons } }));
                else if (path.endsWith(QLatin1String("/s1/children")))
                    reply->complete(plexContainer({ { "Metadata", plexFilms({ "101", "102" }) } }));
                else if (path.endsWith(QLatin1String("/s2/children")))
                    reply->complete(plexContainer({ { "Metadata", plexFilms({ "201" }) } }));
                else
                    reply->complete(plexContainer({ { "Metadata", plexFilms({ path.section(QLatin1Char('/'), -1) }) } }));
            }
            return network->replies.size() - before;
        };
        QCOMPARE(draw(), 4);    // the seasons, each one's episodes, the episode drawn
        QCOMPARE(next.count(), 1);
        QCOMPARE(draw(), 1);
        QCOMPARE(draw(), 1);
        QCOMPARE(next.count(), 3);
        QStringList drawn;
        for (const QList<QVariant> &told : next)
            drawn << told.at(0).toMap().value(QStringLiteral("ratingKey")).toString();
        drawn.sort();
        QCOMPARE(drawn, QStringList({ QStringLiteral("101"), QStringLiteral("102"), QStringLiteral("201") }));
        // The bag run out: made again of the show's episodes as they are now.
        QCOMPARE(draw(), 4);
        QCOMPARE(next.count(), 4);
    }
};

QTEST_GUILESS_MAIN(MediaServersTest)
#include "media_servers_test.moc"
