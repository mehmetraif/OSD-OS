#include "RetroAchievements.h"
#include "Libretro.h"
#include "../highscores/ScoreTable.h"
#include "../../util/AtomicFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace {

// RetroAchievements' arcade games (its console 27), whose hash is their
// file's name.
constexpr int kArcade = 27;
// How long the list of games is kept, how long a board read is, and one
// that couldn't be read.
constexpr qint64 kListSeconds = 7 * 24 * 3600;
constexpr qint64 kBoardSeconds = 10 * 60;
constexpr qint64 kFailedSeconds = 60;
// After a list that couldn't be read: a minute, or with an older one to go
// on with, an hour.
constexpr qint64 kListRetrySeconds = 60;
constexpr qint64 kOldListSeconds = 3600;
// The moment a game is read after it was asked for, and the entries read.
constexpr int kWaitMs = 400;
constexpr int kEntries = 3;
constexpr int kTimeoutMs = 20000;
// On a reply stopped to be asked again (another key).
constexpr const char *kAgain = "osdosAskAgain";

QString listFile(const QString &home) {
    return home + QStringLiteral("/retroachievements-games.json");
}

} // namespace

const QUrl RetroAchievements::kApi(QStringLiteral("https://retroachievements.org/API/"));

RetroAchievements::RetroAchievements(const QString &home, const QUrl &api, QObject *parent)
    : QObject(parent), m_home(home), m_api(api)
{
    m_nam = new QNetworkAccessManager(this);
    m_wait = new QTimer(this);
    m_wait->setSingleShot(true);
    m_wait->setInterval(kWaitMs);
    connect(m_wait, &QTimer::timeout, this, &RetroAchievements::readWanted);
}

void RetroAchievements::setKey(const QString &key) {
    if (key == m_key)
        return;
    m_key = key;
    m_boards.clear();
    // The list the other key couldn't read, this one may.
    m_listRetry = QDateTime();
    // What is being read was asked with the other key: stopped, and read
    // again with this one.
    if (m_reply) {
        m_reply->setProperty(kAgain, true);
        m_reply->abort();
    }
}

QVariantMap RetroAchievements::board(const QString &fileName) {
    if (m_key.isEmpty())
        return { { QStringLiteral("state"), QStringLiteral("failed") } };
    const auto found = m_boards.constFind(fileName);
    const bool known = found != m_boards.constEnd();
    if (!known || !fresh(*found)) {
        m_wanted = fileName;
        m_wait->start();
    }
    // An older board shown while it is read again.
    if (known && (fresh(*found) || found->state == QLatin1String("ready")))
        return { { QStringLiteral("state"), found->state }, { QStringLiteral("title"), found->title },
                 { QStringLiteral("entries"), found->entries } };
    return { { QStringLiteral("state"), QStringLiteral("loading") } };
}

bool RetroAchievements::fresh(const Board &board) const {
    const qint64 age = board.read.secsTo(QDateTime::currentDateTimeUtc());
    return age >= 0 && age < (board.state == QLatin1String("failed") ? kFailedSeconds : kBoardSeconds);
}

bool RetroAchievements::listFresh() const {
    const qint64 age = m_listRead.isValid() ? m_listRead.secsTo(QDateTime::currentDateTimeUtc()) : -1;
    return age >= 0 && age < kListSeconds;
}

void RetroAchievements::loadList() {
    m_listLoaded = true;
    QFile file(listFile(m_home));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject kept = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject games = kept.value(QStringLiteral("games")).toObject();
    for (auto it = games.constBegin(); it != games.constEnd(); ++it)
        m_games.insert(it.key(), it.value().toInt());
    m_listRead = QDateTime::fromString(kept.value(QStringLiteral("read")).toString(), Qt::ISODate);
}

void RetroAchievements::readWanted() {
    if (m_reply || m_wanted.isEmpty() || m_key.isEmpty())
        return;
    const QString fileName = m_wanted;
    // Read meanwhile, as it was asked for again.
    const auto found = m_boards.constFind(fileName);
    if (found != m_boards.constEnd() && fresh(*found)) {
        m_wanted.clear();
        return;
    }
    if (!m_listLoaded)
        loadList();
    if (!listFresh()) {
        if (!m_listRetry.isValid() || m_listRetry <= QDateTime::currentDateTimeUtc()) {
            readList();
            return;
        }
        if (m_games.isEmpty()) {
            m_wanted.clear();
            done(fileName, { QStringLiteral("failed"), QString(), {}, {} });
            return;
        }
        // The older list meanwhile.
    }
    m_wanted.clear();
    const int game = m_games.value(libretro::achievementsHash(fileName));
    if (game <= 0) {
        done(fileName, { QStringLiteral("none"), QString(), {}, {} });
        return;
    }
    readBoards(fileName, game);
}

void RetroAchievements::readList() {
    get(QStringLiteral("API_GetGameList.php"),
        { { QStringLiteral("i"), QString::number(kArcade) }, { QStringLiteral("f"), QStringLiteral("1") },
          { QStringLiteral("h"), QStringLiteral("1") } },
        [this](const QByteArray &json, bool ok) {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            const QJsonArray games = ok ? QJsonDocument::fromJson(json).array() : QJsonArray();
            if (!games.isEmpty()) {
                QHash<QString, int> found;
                QJsonObject kept;
                for (const QJsonValue &value : games) {
                    const QJsonObject game = value.toObject();
                    if (game.value(QStringLiteral("NumLeaderboards")).toInt() <= 0)
                        continue;
                    const int id = game.value(QStringLiteral("ID")).toInt();
                    for (const QJsonValue &hash : game.value(QStringLiteral("Hashes")).toArray()) {
                        found.insert(hash.toString().toLower(), id);
                        kept.insert(hash.toString().toLower(), id);
                    }
                }
                m_games = found;
                m_listRead = now;
                m_listRetry = QDateTime();
                QDir().mkpath(m_home);
                writeFileAtomically(listFile(m_home),
                                    QJsonDocument(QJsonObject{ { QStringLiteral("read"), now.toString(Qt::ISODate) },
                                                               { QStringLiteral("games"), kept } })
                                        .toJson(QJsonDocument::Compact));
            } else {
                // An answer that isn't the list keeps the one there was.
                qWarning("[Arcade] RetroAchievements' list of arcade games could not be read");
                m_listRetry = now.addSecs(m_games.isEmpty() ? kListRetrySeconds : kOldListSeconds);
            }
            readWanted();
        });
}

void RetroAchievements::readBoards(const QString &fileName, int game) {
    get(QStringLiteral("API_GetGameLeaderboards.php"), { { QStringLiteral("i"), QString::number(game) } },
        [this, fileName](const QByteArray &json, bool ok) {
            if (!ok) {
                done(fileName, { QStringLiteral("failed"), QString(), {}, {} });
                return;
            }
            // Its high scores: a board where more is better whose name says
            // score, else the first of those, else the first.
            static const QRegularExpression score(QStringLiteral("score"), QRegularExpression::CaseInsensitiveOption);
            const QJsonArray boards = QJsonDocument::fromJson(json).object().value(QStringLiteral("Results")).toArray();
            QJsonObject chosen, higher;
            for (const QJsonValue &value : boards) {
                const QJsonObject board = value.toObject();
                if (board.value(QStringLiteral("RankAsc")).toBool())
                    continue;
                if (higher.isEmpty())
                    higher = board;
                if (score.match(board.value(QStringLiteral("Title")).toString()).hasMatch()
                    || score.match(board.value(QStringLiteral("Description")).toString()).hasMatch()) {
                    chosen = board;
                    break;
                }
            }
            if (chosen.isEmpty())
                chosen = !higher.isEmpty() ? higher : boards.isEmpty() ? QJsonObject() : boards.first().toObject();
            const int leaderboard = chosen.value(QStringLiteral("ID")).toInt();
            if (leaderboard <= 0) {
                done(fileName, { QStringLiteral("none"), QString(), {}, {} });
                return;
            }
            // Another game asked for meanwhile: that one first.
            if (!m_wanted.isEmpty() && m_wanted != fileName) {
                readWanted();
                return;
            }
            m_wanted.clear();
            readEntries(fileName, leaderboard, chosen.value(QStringLiteral("Title")).toString().trimmed());
        });
}

void RetroAchievements::readEntries(const QString &fileName, int leaderboard, const QString &title) {
    get(QStringLiteral("API_GetLeaderboardEntries.php"),
        { { QStringLiteral("i"), QString::number(leaderboard) }, { QStringLiteral("c"), QString::number(kEntries) } },
        [this, fileName, title](const QByteArray &json, bool ok) {
            if (!ok) {
                done(fileName, { QStringLiteral("failed"), QString(), {}, {} });
                return;
            }
            QVariantList entries;
            const QJsonArray results = QJsonDocument::fromJson(json).object().value(QStringLiteral("Results")).toArray();
            for (const QJsonValue &value : results) {
                const QJsonObject entry = value.toObject();
                const QString formatted = entry.value(QStringLiteral("FormattedScore")).toString();
                entries << QVariantMap{
                    { QStringLiteral("rank"), scores::ordinal(entry.value(QStringLiteral("Rank")).toInt()) },
                    { QStringLiteral("score"), !formatted.isEmpty() ? formatted
                                               : entry.value(QStringLiteral("Score")).toVariant().toString() },
                    { QStringLiteral("name"), entry.value(QStringLiteral("User")).toString() } };
                if (entries.size() == kEntries)
                    break;
            }
            done(fileName, { QStringLiteral("ready"), title, entries, {} });
        });
}

void RetroAchievements::get(const QString &endpoint, const QList<std::pair<QString, QString>> &query,
                            const Answer &answer) {
    QUrl url = m_api.resolved(QUrl(endpoint));
    QUrlQuery items;
    items.addQueryItem(QStringLiteral("y"), m_key);
    for (const auto &[name, value] : query)
        items.addQueryItem(name, value);
    url.setQuery(items);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kTimeoutMs);
    m_reply = m_nam->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint, answer] {
        reply->deleteLater();
        m_reply = nullptr;
        // Stopped for another key: what is wanted read with it. A read that
        // ran out of time is stopped too, and is a failure.
        if (reply->property(kAgain).toBool()) {
            readWanted();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            qWarning("[Arcade] RetroAchievements refused the Web API key");
            emit keyRefused();
            answer(QByteArray(), false);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            // Not errorString(): it names the URL, the key in it.
            qWarning("[Arcade] RetroAchievements' %s could not be read (HTTP %d, error %d)",
                     qPrintable(endpoint), status, int(reply->error()));
            answer(QByteArray(), false);
            return;
        }
        answer(reply->readAll(), true);
    });
}

void RetroAchievements::done(const QString &fileName, const Board &board) {
    Board kept = board;
    kept.read = QDateTime::currentDateTimeUtc();
    m_boards.insert(fileName, kept);
    emit boardReady(fileName);
    readWanted();
}
