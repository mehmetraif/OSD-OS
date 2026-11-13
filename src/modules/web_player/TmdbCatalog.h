#pragma once
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QVariant>
#include <functional>

class QNetworkReply;

// What one streaming service carries in one country, by category, for its web
// player module to browse in a TreeBrowser before it opens the service's own
// player. Netflix and Prime Video have no API a front end like this could use;
// TMDB (The Movie Database) has an official one that lists, per country, the
// films and series each service carries (its "watch providers", from JustWatch).
//
// The tree's paths, and what is in them:
//   home                SEARCH, MOVIES, SERIES, and the service's own home page
//   movie, tv           POPULAR, then the genres
//   movie/popular       the service's titles, the most popular first
//   movie/genre/<id>    the same, in one genre (and tv/… likewise)
//   search/<words>      TMDB's matches for the words that the service carries
// A list of titles holds the pages loaded so far and ends in MORE while there
// are more. A title opens on the service at the page Wikidata knows for it
// (resolveTitleUrl), or at the service's own search for its name.
//
// Needs a TMDB API key, free from themoviedb.org: a v3 key or a v4 read access
// token: Settings' TMDB Key (modules.<id>.tmdb_api_key, either module's), else
// the first line of <dataRoot>/tmdb_api_key.txt.
class TmdbCatalog : public QObject {
    Q_OBJECT
    // "" while browsing works; otherwise what is in the way, to show instead.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    struct Service {
        QString providerName;      // TMDB's name for it, e.g. "Netflix"
        int     fallbackProviderId; // TMDB's id, should the name not match
        QString homeLabel;         // the root entry that opens the service as is
        QString titleUrl;          // "%1" is the service's id for the title
        QString searchUrl;         // "%1" is the title's name, for when no id is known
        QString wikidataProperty;  // where Wikidata keeps the service's id
    };

    explicit TmdbCatalog(const QString &dataRoot, const Service &service,
                         QObject *parent = nullptr);

    // ISO 3166-1 country ("TR") and TMDB language ("en-US"). Changing either
    // forgets what was loaded.
    void setRegion(const QString &region);
    void setLanguage(const QString &language);

    // The entries at path, or an invalid QVariant (undefined in QML) while they
    // are on their way; listingReady(path) follows. A branch's request
    // (preview) is served the same way: TMDB answers fast enough.
    Q_INVOKABLE QVariant listing(const QString &path, bool preview = false);
    // Loads the next page of a list of titles onto its end.
    Q_INVOKABLE void loadMore(const QString &path);
    // Works out where a title entry opens on the service; titleUrlReady follows,
    // with the entry's path.
    Q_INVOKABLE void resolveTitleUrl(const QVariantMap &title);
    // What a title's info screen shows: detailsReady(path, { title, facts,
    // summary, rows: [{ label, value }] }) follows, at once when it has been
    // loaded before.
    Q_INVOKABLE void loadDetails(const QVariantMap &title);

    QString problem() const { return m_problem; }

signals:
    void listingReady(const QString &path);
    void titleUrlReady(const QString &path, const QString &url);
    void detailsReady(const QString &path, const QVariantMap &details);
    void problemChanged();

private:
    struct TitleList {
        QVariantList titles;
        int  pages      = 0;  // loaded so far
        int  totalPages = 0;
        bool loading    = false;
    };

    QString apiKey() const;
    void    setProblem(const QString &problem);
    void    forget();
    bool    failedRecently(const QString &path) const;
    // GETs a TMDB endpoint and hands its JSON to done: empty when it failed,
    // with the problem set.
    void    get(const QString &endpoint, QList<QPair<QString, QString>> query,
                std::function<void(const QVariantMap &)> done);
    // Runs then() once the service's TMDB id for the region is known.
    void    withProvider(std::function<void()> then);
    void    fetchGenres(const QString &type);
    void    fetchPage(const QString &path);
    void    search(const QString &path, const QString &words);
    QVariantMap titleEntry(const QVariantMap &result, const QString &type) const;
    QVariantMap detailsFrom(const QVariantMap &json, const QString &type) const;
    // The service's page for a title, from Wikidata by its TMDB or IMDb id;
    // titleUrlReady(path, …) with fallback when Wikidata doesn't know it.
    void    findOnWikidata(const QString &path, const QString &fallback,
                           const QString &type, int tmdbId, const QString &imdbId);

    QString m_dataRoot;
    Service m_service;
    QString m_region   = QStringLiteral("TR");
    QString m_language = QStringLiteral("en-US");
    QString m_tmdbUrl;
    QString m_wikidataUrl;
    QString m_problem;
    QString m_failure;   // what the last failed request was turned down for
    QNetworkAccessManager m_nam;

    // Bumped by forget(): answers to what was asked before are dropped.
    int  m_generation = 0;
    int  m_providerId = 0;    // 0 until looked up
    bool m_providerLookup = false;
    QList<std::function<void()>> m_waitingForProvider;
    QHash<QString, QVariantList> m_genres;   // "movie"/"tv" -> folder entries
    QStringList m_genresLoading;
    QHash<QString, TitleList> m_lists;       // path -> titles
    QHash<QString, QVariantList> m_searches; // path -> titles on the service
    QStringList m_searching;
    QHash<QString, qint64> m_failedAt;       // path -> when it last failed
    QHash<QString, QVariantMap> m_details;   // title path -> its info screen
};
