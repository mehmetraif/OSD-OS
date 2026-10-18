#include "RetroAchievements.h"
#include "Libretro.h"
#include "../highscores/ScoreTable.h"
#include "../../util/AtomicFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace {

// RetroAchievements' arcade games (its console 27), whose hash is their
// file's name.
constexpr int kArcade = 27;
// How long the list of games is kept, how long a board and a game's
// achievements read are, and one that couldn't be read.
constexpr qint64 kListSeconds = 7 * 24 * 3600;
constexpr qint64 kBoardSeconds = 10 * 60;
constexpr qint64 kAchievementsSeconds = 5 * 60;
constexpr qint64 kFailedSeconds = 60;
// After a list that couldn't be read: a minute, or with an older one to go
// on with, an hour.
constexpr qint64 kListRetrySeconds = 60;
constexpr qint64 kOldListSeconds = 3600;
// The moment a game is read after it was asked for, and the entries read.
constexpr int kWaitMs = 400;
constexpr int kEntries = 3;
constexpr int kTimeoutMs = 20000;
// The most an answer of the Web API may be (the list of games, the biggest,
// is some hundreds of kilobytes), and a file of the media server's: a badge,
// 64 pixels square, or a picture.
constexpr qint64 kMaxAnswer = 16 * 1024 * 1024;
constexpr qint64 kMaxBadge = 512 * 1024;
constexpr qint64 kMaxPicture = 4 * 1024 * 1024;
// On a reply stopped to be asked again (another key).
constexpr const char *kAgain = "osdosAskAgain";

QString listFile(const QString &home) {
    return home + QStringLiteral("/retroachievements-games.json");
}

// A request of the Web API's or the media server's.
QNetworkRequest requestFor(const QUrl &url) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

// A reply given up on once it is bigger than limit: as its header says so,
// or as more than that comes.
void limitReply(QNetworkReply *reply, qint64 limit) {
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, limit] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 received, qint64) {
        if (received > limit)
            reply->abort();
    });
}

// A picture: a PNG or a JPEG, as its first bytes say.
bool isPicture(const QByteArray &data) {
    return data.startsWith("\x89PNG\r\n\x1a\n") || data.startsWith("\xff\xd8\xff");
}

// A badge's name as RetroAchievements gives it, and as it goes in a path.
bool isBadgeName(const QString &name) {
    static const QRegularExpression valid(QRegularExpression::anchoredPattern(QStringLiteral("[0-9A-Za-z]{1,32}")));
    return valid.match(name).hasMatch();
}

} // namespace

const QUrl RetroAchievements::kApi(QStringLiteral("https://retroachievements.org/API/"));
const QUrl RetroAchievements::kMedia(QStringLiteral("https://media.retroachievements.org/"));

RetroAchievements::RetroAchievements(const QString &home, const QUrl &api, const QUrl &media, QObject *parent)
    : QObject(parent), m_home(home), m_api(api), m_media(media)
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
    m_progress.clear();
    // The list the other key couldn't read, this one may.
    m_listRetry = QDateTime();
    // What is being read was asked with the other key: stopped, and read
    // again with this one.
    if (m_reply) {
        m_reply->setProperty(kAgain, true);
        m_reply->abort();
    }
}

void RetroAchievements::setUser(const QString &user) {
    if (user == m_user)
        return;
    m_user = user;
    m_progress.clear();
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

QVariantMap RetroAchievements::achievements(const QString &fileName) {
    if (m_key.isEmpty())
        return { { QStringLiteral("state"), QStringLiteral("failed") } };
    // A game the list of games hasn't has none, without asking: the Arcade
    // screen asks of every game it shows.
    if (!m_listLoaded)
        loadList();
    if (listFresh() && gameOf(fileName) <= 0)
        return { { QStringLiteral("state"), QStringLiteral("none") } };
    const auto found = m_progress.constFind(fileName);
    const bool known = found != m_progress.constEnd();
    if (!known || !fresh(*found)) {
        m_wantedAchievements = fileName;
        m_wait->start();
    }
    // Those read before shown while they are read again: read for this
    // user, as another has them read again (setUser()).
    if (known && (fresh(*found) || found->state == QLatin1String("ready")))
        return { { QStringLiteral("state"), found->state }, { QStringLiteral("achievements"), found->achievements },
                 { QStringLiteral("progress"), !m_user.isEmpty() } };
    return { { QStringLiteral("state"), QStringLiteral("loading") } };
}

void RetroAchievements::forgetAchievements(const QString &fileName) {
    m_progress.remove(fileName);
}

void RetroAchievements::findPictures(const QStringList &games, const QStringList &looked) {
    if (m_key.isEmpty() || games.isEmpty())
        return;
    m_wantedPictures = games;
    m_lookedPictures = looked;
    m_wait->start();
}

bool RetroAchievements::fresh(const Board &board) const {
    const qint64 age = board.read.secsTo(QDateTime::currentDateTimeUtc());
    return age >= 0 && age < (board.state == QLatin1String("failed") ? kFailedSeconds : kBoardSeconds);
}

bool RetroAchievements::fresh(const Progress &progress) const {
    const qint64 age = progress.read.secsTo(QDateTime::currentDateTimeUtc());
    return age >= 0 && age < (progress.state == QLatin1String("failed") ? kFailedSeconds : kAchievementsSeconds);
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
    // A list kept before the games without leaderboards were: its games all
    // have one, and it is read again as soon as it can be.
    const QJsonValue boards = kept.value(QStringLiteral("boards"));
    if (!boards.isArray()) {
        for (const int game : std::as_const(m_games))
            m_boardGames.insert(game);
        return;
    }
    for (const QJsonValue &game : boards.toArray())
        m_boardGames.insert(game.toInt());
    m_listRead = QDateTime::fromString(kept.value(QStringLiteral("read")).toString(), Qt::ISODate);
}

int RetroAchievements::gameOf(const QString &fileName) const {
    return m_games.value(libretro::achievementsHash(fileName));
}

bool RetroAchievements::wanting() const {
    return !m_wanted.isEmpty() || !m_wantedAchievements.isEmpty() || !m_wantedPictures.isEmpty();
}

void RetroAchievements::failWanted() {
    // All taken first: what each signal leads to finds nothing wanted.
    const QString board = std::exchange(m_wanted, QString());
    const QString achievements = std::exchange(m_wantedAchievements, QString());
    const QStringList pictures = std::exchange(m_wantedPictures, QStringList());
    if (!achievements.isEmpty())
        doneAchievements(achievements, { QStringLiteral("failed"), {}, {} });
    if (!board.isEmpty())
        done(board, { QStringLiteral("failed"), QString(), {}, {} });
    if (!pictures.isEmpty())
        emit picturesFound(pictures.first(), QStringLiteral("failed"), {});
}

void RetroAchievements::readWanted() {
    if (m_reply || m_key.isEmpty())
        return;
    // Read meanwhile, as it was asked for again.
    const auto board = m_boards.constFind(m_wanted);
    if (board != m_boards.constEnd() && fresh(*board))
        m_wanted.clear();
    const auto progress = m_progress.constFind(m_wantedAchievements);
    if (progress != m_progress.constEnd() && fresh(*progress))
        m_wantedAchievements.clear();
    if (!wanting())
        return;
    if (!m_listLoaded)
        loadList();
    if (!listFresh()) {
        if (!m_listRetry.isValid() || m_listRetry <= QDateTime::currentDateTimeUtc()) {
            readList();
            return;
        }
        if (m_games.isEmpty()) {
            failWanted();
            return;
        }
        // The older list meanwhile.
    }
    if (!m_wantedAchievements.isEmpty()) {
        const QString fileName = std::exchange(m_wantedAchievements, QString());
        const int game = gameOf(fileName);
        if (game <= 0)
            doneAchievements(fileName, { QStringLiteral("none"), {}, {} });
        else
            readAchievements(fileName, game);
        return;
    }
    if (!m_wanted.isEmpty()) {
        const QString fileName = std::exchange(m_wanted, QString());
        const int game = gameOf(fileName);
        if (game <= 0 || !m_boardGames.contains(game))
            done(fileName, { QStringLiteral("none"), QString(), {}, {} });
        else
            readBoards(fileName, game);
        return;
    }
    const QStringList games = std::exchange(m_wantedPictures, QStringList());
    const QStringList looked = std::exchange(m_lookedPictures, QStringList());
    int game = 0;
    QString chosen;
    for (const QString &fileName : games) {
        const int id = gameOf(fileName);
        if (id > 0 && (game == 0 || id == game)) {
            game = id;
            chosen = fileName;
        }
    }
    if (game <= 0)
        emit picturesFound(games.first(), QStringLiteral("unknown"), {});
    else if (!looked.contains(chosen))
        readPictures(chosen, game);
    // Nothing read: the next wanted.
    if (!m_reply)
        readWanted();
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
                QSet<int> boards;
                QJsonObject kept;
                QJsonArray keptBoards;
                for (const QJsonValue &value : games) {
                    const QJsonObject game = value.toObject();
                    const int id = game.value(QStringLiteral("ID")).toInt();
                    if (id <= 0)
                        continue;
                    if (game.value(QStringLiteral("NumLeaderboards")).toInt() > 0) {
                        boards.insert(id);
                        keptBoards.append(id);
                    }
                    for (const QJsonValue &hash : game.value(QStringLiteral("Hashes")).toArray()) {
                        found.insert(hash.toString().toLower(), id);
                        kept.insert(hash.toString().toLower(), id);
                    }
                }
                m_games = found;
                m_boardGames = boards;
                m_listRead = now;
                m_listRetry = QDateTime();
                QDir().mkpath(m_home);
                writeFileAtomically(listFile(m_home),
                                    QJsonDocument(QJsonObject{ { QStringLiteral("read"), now.toString(Qt::ISODate) },
                                                               { QStringLiteral("games"), kept },
                                                               { QStringLiteral("boards"), keptBoards } })
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

void RetroAchievements::readAchievements(const QString &fileName, int game) {
    // As the user has them, or with no user, as they are.
    const QString user = m_user;
    const bool progress = !user.isEmpty();
    const QString endpoint = progress ? QStringLiteral("API_GetGameInfoAndUserProgress.php")
                                      : QStringLiteral("API_GetGameExtended.php");
    const QList<std::pair<QString, QString>> query
        = progress ? QList<std::pair<QString, QString>>{ { QStringLiteral("g"), QString::number(game) },
                                                         { QStringLiteral("u"), user } }
                   : QList<std::pair<QString, QString>>{ { QStringLiteral("i"), QString::number(game) } };
    get(endpoint, query, [this, fileName, user](const QByteArray &json, bool ok) {
        // Read for the user there was: read again for this one.
        if (user != m_user) {
            if (m_wantedAchievements.isEmpty())
                m_wantedAchievements = fileName;
            readWanted();
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(json);
        if (!ok || !document.isObject()) {
            doneAchievements(fileName, { QStringLiteral("failed"), {}, {} });
            return;
        }
        // An object of them by their ids, or an empty array for none.
        const QJsonValue listed = document.object().value(QStringLiteral("Achievements"));
        QList<QJsonObject> found;
        if (listed.isObject()) {
            const QJsonObject byId = listed.toObject();
            for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
                found << it.value().toObject();
        } else {
            for (const QJsonValue &value : listed.toArray())
                found << value.toObject();
        }
        std::sort(found.begin(), found.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const auto order = [](const QJsonObject &o) {
                return std::pair(o.value(QStringLiteral("DisplayOrder")).toInt(), o.value(QStringLiteral("ID")).toInt());
            };
            return order(a) < order(b);
        });
        QVariantList achievements;
        for (const QJsonObject &achievement : std::as_const(found)) {
            const QString hardcore = achievement.value(QStringLiteral("DateEarnedHardcore")).toString();
            const QString earned = !hardcore.isEmpty() ? hardcore : achievement.value(QStringLiteral("DateEarned")).toString();
            const QString badge = achievement.value(QStringLiteral("BadgeName")).toString();
            achievements << QVariantMap{
                { QStringLiteral("id"), achievement.value(QStringLiteral("ID")).toInt() },
                { QStringLiteral("title"), achievement.value(QStringLiteral("Title")).toString().trimmed() },
                { QStringLiteral("description"), achievement.value(QStringLiteral("Description")).toString().trimmed() },
                { QStringLiteral("points"), achievement.value(QStringLiteral("Points")).toInt() },
                { QStringLiteral("badge"), isBadgeName(badge) ? badge : QString() },
                { QStringLiteral("earned"), earned.left(10) },
                { QStringLiteral("hardcore"), !hardcore.isEmpty() } };
        }
        doneAchievements(fileName, { achievements.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready"),
                                     achievements, {} });
    });
}

void RetroAchievements::readPictures(const QString &fileName, int game) {
    get(QStringLiteral("API_GetGame.php"), { { QStringLiteral("i"), QString::number(game) } },
        [this, fileName](const QByteArray &json, bool ok) {
            const QJsonObject found = QJsonDocument::fromJson(json).object();
            if (!ok || found.isEmpty()) {
                emit picturesFound(fileName, QStringLiteral("failed"), {});
                readWanted();
                return;
            }
            // Its title screen, a screenshot in play and its box art, each
            // once; not RetroAchievements' pictures of none yet (000001 the
            // icon's, 000002 the others').
            static const QRegularExpression picture(
                QRegularExpression::anchoredPattern(QStringLiteral("/Images/[0-9A-Za-z_]{1,32}\\.(png|jpg)")),
                QRegularExpression::CaseInsensitiveOption);
            static const QStringList placeholders{ QStringLiteral("/Images/000001.png"),
                                                   QStringLiteral("/Images/000002.png") };
            QStringList paths;
            for (const char *key : { "ImageTitle", "ImageIngame", "ImageBoxArt" }) {
                const QString path = found.value(QLatin1String(key)).toString();
                if (picture.match(path).hasMatch() && !placeholders.contains(path) && !paths.contains(path))
                    paths << path;
            }
            emit picturesFound(fileName, QStringLiteral("found"), paths);
            readWanted();
        });
}

void RetroAchievements::media(const QString &path, const std::function<void(const QByteArray &data)> &done) {
    static const QRegularExpression valid(
        QRegularExpression::anchoredPattern(QStringLiteral("/(Images|Badge)/[0-9A-Za-z_]{1,40}\\.(png|jpg)")),
        QRegularExpression::CaseInsensitiveOption);
    if (!valid.match(path).hasMatch()) {
        QTimer::singleShot(0, this, [done] { done(QByteArray()); });
        return;
    }
    m_mediaQueue.append({ path, done });
    nextMedia();
}

void RetroAchievements::nextMedia() {
    if (m_mediaReply || m_mediaQueue.isEmpty())
        return;
    const Media next = m_mediaQueue.takeFirst();
    const qint64 limit = next.path.startsWith(QLatin1String("/Badge/")) ? kMaxBadge : kMaxPicture;
    // Relative to the server's address, its path without the first /.
    m_mediaReply = m_nam->get(requestFor(m_media.resolved(QUrl(next.path.mid(1)))));
    QNetworkReply *reply = m_mediaReply;
    limitReply(reply, limit);
    connect(reply, &QNetworkReply::finished, this, [this, reply, next, limit] {
        reply->deleteLater();
        m_mediaReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data;
        if (reply->error() == QNetworkReply::NoError)
            data = reply->readAll();
        if (data.size() > limit || !isPicture(data)) {
            qWarning("[Arcade] RetroAchievements' %s could not be downloaded (HTTP %d, error %d)",
                     qPrintable(next.path), status, int(reply->error()));
            data.clear();
        }
        next.done(data);
        nextMedia();
    });
}

QString RetroAchievements::badge(const QString &name, bool locked) {
    if (!isBadgeName(name))
        return QString();
    const QString file = name + (locked ? QStringLiteral("_lock.png") : QStringLiteral(".png"));
    const QString path = m_home + QStringLiteral("/badges/") + file;
    if (QFile::exists(path))
        return QUrl::fromLocalFile(path).toString();
    // One download of each at a time; one that couldn't be, a minute later.
    const auto retry = m_badgesRetry.constFind(file);
    if (m_badgesWanted.contains(file)
        || (retry != m_badgesRetry.constEnd() && *retry > QDateTime::currentDateTimeUtc()))
        return QString();
    m_badgesWanted.insert(file);
    media(QStringLiteral("/Badge/") + file, [this, file, path](const QByteArray &data) {
        const auto saved = [this, file](bool written) {
            m_badgesWanted.remove(file);
            if (!written) {
                m_badgesRetry.insert(file, QDateTime::currentDateTimeUtc().addSecs(kFailedSeconds));
                return;
            }
            m_badgesRetry.remove(file);
            emit badgesReady();
        };
        if (data.isEmpty()) {
            saved(false);
            return;
        }
        // Written to the card off the GUI thread, as the list scrolls.
        const QString folder = m_home + QStringLiteral("/badges");
        QtConcurrent::run([folder, path, data] { return QDir().mkpath(folder) && writeFileAtomically(path, data); })
            .then(this, saved);
    });
    return QString();
}

void RetroAchievements::get(const QString &endpoint, const QList<std::pair<QString, QString>> &query,
                            const Answer &answer) {
    QUrl url = m_api.resolved(QUrl(endpoint));
    QUrlQuery items;
    items.addQueryItem(QStringLiteral("y"), m_key);
    for (const auto &[name, value] : query)
        items.addQueryItem(name, value);
    url.setQuery(items);
    m_reply = m_nam->get(requestFor(url));
    QNetworkReply *reply = m_reply;
    limitReply(reply, kMaxAnswer);
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
        const QByteArray json = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        if (reply->error() != QNetworkReply::NoError || json.size() > kMaxAnswer) {
            // Not errorString(): it names the URL, the key in it.
            qWarning("[Arcade] RetroAchievements' %s could not be read (HTTP %d, error %d)",
                     qPrintable(endpoint), status, int(reply->error()));
            answer(QByteArray(), false);
            return;
        }
        answer(json, true);
    });
}

void RetroAchievements::done(const QString &fileName, const Board &board) {
    Board kept = board;
    kept.read = QDateTime::currentDateTimeUtc();
    m_boards.insert(fileName, kept);
    emit boardReady(fileName);
    readWanted();
}

void RetroAchievements::doneAchievements(const QString &fileName, const Progress &progress) {
    Progress kept = progress;
    kept.read = QDateTime::currentDateTimeUtc();
    m_progress.insert(fileName, kept);
    emit achievementsReady(fileName);
    readWanted();
}
