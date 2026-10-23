#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// progetto-SNAPS' pictures of MAME's games, through ArcadeDB (Arcade
// Database, adb.arcadeitalia.net), which publishes them: a screenshot in
// play and the title screen of nearly every set MAME has, by the set's name,
// the two pictures the picture packs for MAME (progetto-SNAPS' Snapshots and
// Titles) hold. Its scraper service (service_scraper.php, query_mame) says of
// each set asked about whether it has them and where they are, a clone
// without pictures of its own given its parent's (use_parent); the pictures
// come from the addresses it gives, on its own server alone. One request is
// under way at a time, as the service asks of those who read it, and after an
// answer that there were too many (HTTP 429, or 503) nothing is asked until
// the moment it names (Retry-After), or a minute.
class ArcadeDb : public QObject {
    Q_OBJECT
public:
    static const QUrl kService;   // https://adb.arcadeitalia.net/service_scraper.php
    // As many sets as one request asks about.
    static constexpr int kMostAsked = 20;

    explicit ArcadeDb(const QUrl &service, QObject *parent = nullptr);

    // The service's address: a stand-in's. Without one (an empty address),
    // nothing is asked: every lookUp() fails at once.
    void setService(const QUrl &service) { m_service = service; }

    // Asks about sets (kMostAsked at most; more are asked about in turn),
    // after those asked about before: found() for each once the service has
    // answered, failed() for all of them when it couldn't.
    void lookUp(const QStringList &sets);
    // Sets are still to be asked about, or being.
    bool busy() const { return m_reply != nullptr || !m_queue.isEmpty(); }
    // Nothing is asked until then, after too many requests.
    bool heldOff() const { return m_heldUntil.isValid() && m_heldUntil > QDateTime::currentDateTimeUtc(); }

    // A picture from an address found() gave (one on the service's server:
    // any other is refused), downloaded after those asked for before it:
    // done with its bytes, a PNG or a JPEG; empty when it couldn't be
    // downloaded or is neither.
    void media(const QUrl &url, const std::function<void(const QByteArray &data)> &done);

signals:
    // What the service has of a set: the address of its screenshot in play
    // and of its title screen, "" for one it hasn't (both "" for a set it
    // doesn't know).
    void found(const QString &set, const QString &ingame, const QString &title);
    // The sets of a request the service couldn't answer (or answered with
    // anything but its list of games), and a set whose pictures it keeps on
    // another server, which isn't downloaded from; and how long to wait
    // before asking again (0 for no more than usual).
    void failed(const QStringList &sets, int holdSeconds);

private:
    struct Media {
        QUrl url;
        std::function<void(const QByteArray &)> done;
    };
    void next();
    void nextMedia();
    // The address is the service's server's.
    bool isOurs(const QUrl &url) const;

    QUrl m_service;
    QNetworkAccessManager *m_nam = nullptr;
    QList<QStringList> m_queue;       // the sets to ask about, a request each
    QNetworkReply *m_reply = nullptr; // the request under way
    QDateTime m_heldUntil;
    QList<Media> m_mediaQueue;
    QNetworkReply *m_mediaReply = nullptr;
};
