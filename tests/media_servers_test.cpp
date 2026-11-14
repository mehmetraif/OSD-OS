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
// again at the next start, Auto quality caps no transcode, a live channel
// left while it tuned plays in no one's player, a Plex server that moved is
// found again at the address it has now, Emby Connect tries a server's
// address from outside when its LAN address gives no sign-in, and Settings'
// Libraries are told why there are none when the server can't be reached.

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
    // No answer at all: nothing at the address (the server moved, the
    // network down), so no status either.
    void drop() {
        setError(ConnectionRefusedError, QStringLiteral("Connection refused"));
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
        Backend backend(tmp.path(), tmp.path());
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
        Backend backend(tmp.path(), tmp.path());
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

private slots:
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

    // The settings page learns what the server can do as it opens, with the
    // choices it asks for: Intro Skip and Credit Skip are usable there, on
    // every opening, the server asked once.
    void jellyfinSettingsPageLearnsCapabilities() {
        QTemporaryDir tmp;
        JellyfinBackend backend(tmp.path(), tmp.path());
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
        PlexBackend backend(tmp.path(), tmp.path());
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
        EmbyBackend backend(tmp.path(), tmp.path());
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
};

QTEST_GUILESS_MAIN(MediaServersTest)
#include "media_servers_test.moc"
