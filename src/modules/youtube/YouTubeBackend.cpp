#include "YouTubeBackend.h"

#include "../../AppCore.h"
#include "../web_player/WebPlayerBackend.h"
#include "../../util/AtomicFile.h"
#include "../../util/InstallHint.h"
#include "../../util/YtDlpFormat.h"
#include "../../util/YtDlpLocator.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

static const QString kModuleId = QStringLiteral("com.osdos.youtube");
static const char *kSubscriptionsFileName = "youtube_subscriptions.txt";
static const char *kPlaylistsFileName     = "youtube_playlists.txt";

// What the tree's help line says when a source fails.
static const char *kSubscriptionsProblem = "Could not load the subscriptions: check the network";
static const char *kPlaylistsProblem     = "Could not load the playlists: check the network,"
                                           " and that yt-dlp is installed";
static const char *kSearchProblem        = "Could not search YouTube: check the network,"
                                           " and that yt-dlp is up to date";
// With how to put yt-dlp on, for this system (util/InstallHint.h).
static QString noYtDlpProblem() {
    return QStringLiteral("Searching needs yt-dlp, which is not installed. ") + installhint::forProgram(QStringLiteral("yt-dlp"));
}

static QString watchUrlFor(const QString &videoId) {
    return QStringLiteral("https://www.youtube.com/watch?v=") + videoId;
}

YouTubeBackend::YouTubeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                               DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot), m_appCore(appCore),
      m_history(dataRoot + "/youtube_history.json", kMaxHistoryItems)
{
    // Google's sign-in as YouTube's own SIGN IN button opens it, back to
    // YouTube once signed in. Nothing to browse, so no catalogue.
    WebPlayerBackend::Service service{};
    service.id            = QStringLiteral("youtube");
    service.homeUrl       = QStringLiteral("https://www.youtube.com");
    service.signInUrl     = QStringLiteral(
        "https://accounts.google.com/ServiceLogin?service=youtube&passive=true"
        "&continue=https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue"
        "%26app%3Ddesktop%26next%3Dhttps%253A%252F%252Fwww.youtube.com%252F");
    service.needsChromium = true;
    m_browser = new WebPlayerBackend(service, appRoot, dataRoot, appCore, handoff, this);
    connect(m_browser, &WebPlayerBackend::authStateChanged, this, &YouTubeBackend::authStateChanged);
}

QObject *YouTubeBackend::browser() const {
    return m_browser;
}

void YouTubeBackend::signOut() {
    m_browser->signOut();
}

QString YouTubeBackend::get_auth_state() const {
    return m_browser->get_auth_state();
}

void YouTubeBackend::opened() {
#ifdef Q_OS_LINUX
    static const QString unit = QStringLiteral("osdos-yt-dlp-update.service");
    if (m_updateStarted || !QFileInfo::exists(QStringLiteral("/etc/systemd/system/") + unit))
        return;
    m_updateStarted = true;
    // Started through systemd, which runs one update at a time, so never
    // alongside the timer's; its polkit rule lets the app's user start it.
    // Without a connection, or with nothing new, it ends by itself.
    if (QProcess::startDetached(QStringLiteral("systemctl"),
                                {QStringLiteral("--no-ask-password"), QStringLiteral("--no-block"),
                                 QStringLiteral("start"), unit}))
        qDebug("[YouTube] yt-dlp update started");
#endif
}

QString YouTubeBackend::cookiesFromBrowser() const {
    const QString profile = m_browser->browserProfile();
    // Chromium's cookie store: in Network/ since Chromium 96, beside it before.
    if (!QFileInfo::exists(profile + QStringLiteral("/Default/Network/Cookies"))
        && !QFileInfo::exists(profile + QStringLiteral("/Default/Cookies")))
        return {};
#ifdef Q_OS_MACOS
    // Google Chrome, whose key to them is in the login keychain.
    return QStringLiteral("chrome:") + profile;
#else
    // web-player.sh runs the browser with --password-store=basic: Chromium's
    // fixed key, not a keyring's.
    return QStringLiteral("chromium+basictext:") + profile;
#endif
}

QStringList YouTubeBackend::cookieArgs() const {
    const QString cookies = cookiesFromBrowser();
    if (cookies.isEmpty())
        return {};
    return { QStringLiteral("--cookies-from-browser"), cookies };
}

// ---------------------------------------------------------------------------
// Subscriptions: Settings' list (subscriptions), then youtube_subscriptions.txt
// ---------------------------------------------------------------------------

// One of the module's two lists: the lines typed in Settings or OSD/OS Setup
// (modules.<id>.<key>, a list of lines), then the file's in the data folder.
// Blank lines and '#' comments are no lines.
QStringList YouTubeBackend::listLines(const char *key, const char *fileName) const {
    QStringList lines;
    const auto take = [&lines](const QString &raw) {
        const QString line = raw.trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            lines << line;
    };
    if (m_appCore) {
        const QVariantList typed = m_appCore->get_setting(kModuleId, QLatin1String(key)).toList();
        for (const QVariant &line : typed)
            take(line.toString());
    }
    QFile f(m_dataRoot + "/" + fileName);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd())
            take(QString::fromUtf8(f.readLine()));
    }
    return lines;
}

QStringList YouTubeBackend::readSubscriptionIds() const {
    QStringList ids;
    for (QString line : listLines("subscriptions", kSubscriptionsFileName)) {
        // Be lenient with pasted channel URLs: take the segment after "channel/".
        const int slash = line.indexOf(QLatin1String("channel/"));
        if (slash >= 0) {
            line = line.mid(slash + 8);
            const int end = line.indexOf(QRegularExpression(QStringLiteral("[/?#]")));
            if (end >= 0)
                line = line.left(end);
        }
        if (!line.isEmpty() && !ids.contains(line))
            ids << line;
    }
    return ids;
}

// ---------------------------------------------------------------------------
// The cache fill: one in-flight refresh serves every path waiting on it.
// ---------------------------------------------------------------------------

bool YouTubeBackend::SourceCache::fresh(const QStringList &ids, qint64 now) const {
    return loadedMs > 0 && now - loadedMs < kCacheTtlMs && ids == order;
}

QStringList YouTubeBackend::SourceCache::stale(const QStringList &ids, bool force, qint64 now) {
    order = ids;
    QStringList out;
    for (const QString &id : ids) {
        const Source &source = sources[id];
        if (force || !source.ok || now - source.fetchedMs > kCacheTtlMs)
            out << id;
    }
    return out;
}

void YouTubeBackend::SourceCache::store(const QString &id, const QString &title, const QVariantList &videos) {
    Source &source = sources[id];
    source.title     = title;
    source.videos    = videos;
    source.ok        = true;
    source.fetchedMs = QDateTime::currentMSecsSinceEpoch();
}

bool YouTubeBackend::SourceCache::landed() {
    --active;
    if (--pending > 0)
        return false;
    pending = 0;
    active  = 0;
    fetchQueue.clear();
    return true;
}

bool YouTubeBackend::SourceCache::finish(qint64 now) {
    bool anyOk = false;
    for (const QString &id : order)
        anyOk = anyOk || sources.value(id).ok;
    if (anyOk) {
        loadedMs = now;
        failedMs = 0;
    } else {
        failedMs = now;
    }
    return anyOk;
}

void YouTubeBackend::ensureFresh(bool forceRefresh) {
    if (m_channels.pending > 0)
        return; // refresh already in flight — the waits queue on it

    const QStringList ids = readSubscriptionIds();
    if (ids.isEmpty()) {
        answerLater(std::exchange(m_channels.waits, {}));
        return;
    }
    const QStringList stale = m_channels.stale(ids, forceRefresh, QDateTime::currentMSecsSinceEpoch());
    if (stale.isEmpty()) {
        finishAggregate(); // everything fresh — serve from cache
        return;
    }
    m_channels.pending    = stale.size();
    m_channels.fetchQueue = stale;
    spawnNextChannelFetch();
}

// The feeds a few at a time (kMaxConcurrentChannelFetches), the next asked
// for as each lands.
void YouTubeBackend::spawnNextChannelFetch() {
    while (m_channels.active < kMaxConcurrentChannelFetches && !m_channels.fetchQueue.isEmpty()) {
        ++m_channels.active;
        refreshChannel(m_channels.fetchQueue.takeFirst());
    }
}

// ---------------------------------------------------------------------------
// Per-channel fetch: the official RSS feed
// ---------------------------------------------------------------------------

QNetworkRequest YouTubeBackend::makeRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setTransferTimeout(10000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    return req;
}

// Atom feed → channel name + video maps (newest first, as served).
// Partial parses are kept: only a parse error with zero entries counts as failure.
static bool parseRssFeed(const QByteArray &data, const QString &channelId,
                         QString *channelName, QVariantList *videos) {
    static const QLatin1String kAtomNs("http://www.w3.org/2005/Atom");
    QXmlStreamReader xml(data);
    bool inEntry = false;
    QString videoId, title, altLink, description;
    QDateTime published;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("entry")) {
                inEntry = true;
                videoId.clear();
                title.clear();
                altLink.clear();
                description.clear();
                published = QDateTime();
            } else if (!inEntry && name == QLatin1String("title") && channelName->isEmpty()) {
                *channelName = xml.readElementText();
            } else if (inEntry && name == QLatin1String("videoId")) {
                videoId = xml.readElementText();
            } else if (inEntry && title.isEmpty() && name == QLatin1String("title")
                       && xml.namespaceUri() == kAtomNs) {
                // namespace check keeps <media:title> (inside media:group) out
                title = xml.readElementText();
            } else if (inEntry && name == QLatin1String("description")) {
                // media:description, inside media:group
                description = xml.readElementText();
            } else if (inEntry && name == QLatin1String("published")) {
                published = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
            } else if (inEntry && name == QLatin1String("link")
                       && xml.attributes().value(QLatin1String("rel")) == QLatin1String("alternate")) {
                // Shorts expose a /shorts/<id> alternate href; normal uploads use /watch?v=<id>
                altLink = xml.attributes().value(QLatin1String("href")).toString();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("entry")) {
            inEntry = false;
            if (videoId.isEmpty())
                continue;
            QVariantMap v;
            v["videoId"]     = videoId;
            v["title"]       = title;
            v["channelId"]   = channelId;
            v["channelName"] = QString(); // filled in once the feed title is known
            v["publishedAt"] = published.isValid() ? published.toUTC().toString(Qt::ISODate)
                                                   : QString();
            v["publishedMs"] = published.isValid() ? published.toMSecsSinceEpoch() : qint64(0);
            v["url"]         = watchUrlFor(videoId);
            v["isShort"]     = altLink.contains(QLatin1String("/shorts/"));
            v["description"] = description;
            videos->append(v);
        }
    }
    return !(xml.hasError() && videos->isEmpty());
}

void YouTubeBackend::refreshChannel(const QString &channelId) {
    QUrl rssUrl(m_feedUrl);
    rssUrl.setQuery(QStringLiteral("channel_id=") + channelId);
    QNetworkReply *reply = m_nam.get(makeRequest(rssUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply, channelId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QString name;
            QVariantList videos;
            if (parseRssFeed(reply->readAll(), channelId, &name, &videos)) {
                for (QVariant &v : videos) {
                    QVariantMap m = v.toMap();
                    m["channelName"] = name;
                    v = m;
                }
                m_channels.store(channelId, name, videos);
                // The channel opened is answered as its own feed lands, not
                // once the slowest of the others has.
                const QString path = QStringLiteral("channel/") + channelId;
                if (m_channels.waits.removeAll(path) > 0)
                    answerLater({ path });
            }
        }
        // On failure: keep any previously cached videos (stale beats empty);
        // fetchedMs stays old so the next load retries this channel.
        if (m_channels.landed())
            finishAggregate();
        else
            spawnNextChannelFetch();
    });
}

void YouTubeBackend::finishAggregate() {
    // The tree's waits, answered from the cache once this has returned.
    if (m_channels.finish(QDateTime::currentMSecsSinceEpoch()))
        setProblem(QString());
    else
        setProblem(QString::fromLatin1(kSubscriptionsProblem));
    answerLater(std::exchange(m_channels.waits, {}));
}

QVariantList YouTubeBackend::buildFeed() const {
    QVariantList all;
    for (const QString &id : m_channels.order)
        all += m_channels.sources.value(id).videos;
    std::sort(all.begin(), all.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("publishedMs").toLongLong()
             > b.toMap().value("publishedMs").toLongLong();
    });
    return all.mid(0, kMaxFeedItems);
}

QVariantList YouTubeBackend::buildChannelList() const {
    QVariantList channels;
    for (const QString &id : m_channels.order) {
        const Source entry = m_channels.sources.value(id);
        QVariantMap c;
        // Fall back to the raw ID so a channel whose feed failed is still visible
        c["channelId"]  = id;
        c["title"]      = entry.title.isEmpty() ? id : entry.title;
        c["videoCount"] = entry.videos.size();
        channels << c;
    }
    std::sort(channels.begin(), channels.end(), [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value("title").toString(),
                                b.toMap().value("title").toString(),
                                Qt::CaseInsensitive) < 0;
    });
    return channels;
}

// ---------------------------------------------------------------------------
// Playlists: Settings' list (playlists), then youtube_playlists.txt
// Line format: [My Display Name | ] <playlist URL or bare playlist ID>
// ---------------------------------------------------------------------------

// "list=" query param when present, bare token otherwise. A URL without a
// list= param isn't a playlist link — rejected so it can't be fed to yt-dlp
// as something else entirely.
static QString playlistIdFromToken(QString token) {
    const int listPos = token.indexOf(QLatin1String("list="));
    if (listPos >= 0) {
        token = token.mid(listPos + 5);
        const int end = token.indexOf(QRegularExpression(QStringLiteral("[&#?/]")));
        if (end >= 0)
            token = token.left(end);
        return token;
    }
    if (token.contains(QLatin1String("://")))
        return {};
    return token;
}

QList<YouTubeBackend::PlaylistFileRef> YouTubeBackend::readPlaylistEntries() const {
    QList<PlaylistFileRef> refs;
    QStringList seen;
    for (const QString &line : listLines("playlists", kPlaylistsFileName)) {
        // Split the optional display-name prefix at the last '|' (URLs never
        // contain one, display names conceivably could).
        QString name, token = line;
        const int bar = line.lastIndexOf('|');
        if (bar >= 0) {
            name  = line.left(bar).trimmed();
            token = line.mid(bar + 1).trimmed();
        }
        const QString id = playlistIdFromToken(token);
        if (id.isEmpty() || seen.contains(id))
            continue;
        seen << id;
        refs.append({id, name});
    }
    return refs;
}

// ---------------------------------------------------------------------------
// Playlists — yt-dlp --flat-playlist subprocesses feeding a SourceCache, as
// the RSS feeds feed the channels'. yt-dlp is used (rather than the
// playlist RSS feed) because the feed stops at 15 entries.
// ---------------------------------------------------------------------------

void YouTubeBackend::ensurePlaylistsFresh(bool forceRefresh) {
    if (m_playlists.pending > 0)
        return; // refresh already in flight — the waits queue on it

    const QList<PlaylistFileRef> refs = readPlaylistEntries();
    if (refs.isEmpty()) {
        answerLater(std::exchange(m_playlists.waits, {}));
        return;
    }
    QStringList ids;
    for (const PlaylistFileRef &ref : refs)
        ids << ref.id;
    const QStringList stale = m_playlists.stale(ids, forceRefresh, QDateTime::currentMSecsSinceEpoch());
    if (stale.isEmpty()) {
        finishPlaylistAggregate(); // everything fresh — serve from cache
        return;
    }
    if (ytdlp::locate(m_dataRoot).isEmpty()) {
        // No yt-dlp: nothing can be fetched; report against whatever the
        // cache holds.
        finishPlaylistAggregate();
        return;
    }
    m_playlists.pending    = stale.size();
    m_playlists.fetchQueue = stale;
    spawnNextPlaylistFetch();
}

// A video as yt-dlp prints a flat playlist's entries (--print with id, title,
// channel and uploader, and in a search url and live_status too): what the
// tree's rows and the player need. Flat entries carry no publish date; the
// list's order stands in. A playlist's print no url, so none of them is a
// Short.
static QVariantMap flatVideo(const QJsonObject &obj) {
    const QString videoId = obj.value(QLatin1String("id")).toString();
    QString channel = obj.value(QLatin1String("channel")).toString();
    if (channel.isEmpty())
        channel = obj.value(QLatin1String("uploader")).toString();
    QVariantMap v;
    v["videoId"]     = videoId;
    v["title"]       = obj.value(QLatin1String("title")).toString();
    v["channelId"]   = QString();
    v["channelName"] = channel;
    v["publishedAt"] = QString();
    v["publishedMs"] = qint64(0);
    v["url"]         = watchUrlFor(videoId);
    v["isShort"]     = obj.value(QLatin1String("url")).toString().contains(QLatin1String("/shorts/"));
    return v;
}

void YouTubeBackend::runYtDlp(const QStringList &args, std::function<void(QProcess *)> done) {
    auto *proc = new QProcess(this);
    auto finish = [proc, done = std::move(done)]() {
        proc->deleteLater();
        done(proc);
    };
    connect(proc, &QProcess::finished, this, finish);
    // finished() is never emitted when the binary fails to launch
    connect(proc, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart)
            finish();
    });
    QTimer::singleShot(kPlaylistFetchTimeoutMs, proc, [proc]() { proc->kill(); });
    // The same user-updatable yt-dlp mpv's ytdl_hook will use at playback
    // time (data-dir drop-in → sibling → PATH), so app and mpv agree.
    proc->start(ytdlp::locate(m_dataRoot), args);
}

void YouTubeBackend::spawnNextPlaylistFetch() {
    while (m_playlists.active < kMaxConcurrentPlaylistFetches
           && !m_playlists.fetchQueue.isEmpty()) {
        const QString playlistId = m_playlists.fetchQueue.takeFirst();
        ++m_playlists.active;

        const QStringList args = cookieArgs() + QStringList{
            QStringLiteral("--flat-playlist"),
            QStringLiteral("-I"), QStringLiteral("1:%1").arg(kMaxPlaylistItems),
            QStringLiteral("--no-warnings"),
            // One JSON object per entry — robust against '|' etc. in titles
            QStringLiteral("--print"),
            QStringLiteral("%(.{id,title,channel,uploader,playlist_title})j"),
            QStringLiteral("--"),
            QStringLiteral("https://www.youtube.com/playlist?list=") + playlistId,
        };

        runYtDlp(args, [this, playlistId](QProcess *proc) {
            QString      title;
            QVariantList videos;
            const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
            for (const QByteArray &line : lines) {
                const QJsonObject obj = QJsonDocument::fromJson(line.trimmed()).object();
                if (obj.isEmpty())
                    continue;
                if (title.isEmpty())
                    title = obj.value(QLatin1String("playlist_title")).toString();
                const QString videoId    = obj.value(QLatin1String("id")).toString();
                const QString videoTitle = obj.value(QLatin1String("title")).toString();
                if (videoId.isEmpty())
                    continue;
                // Tombstones YouTube leaves in place of removed entries
                if (videoTitle == QLatin1String("[Private video]")
                    || videoTitle == QLatin1String("[Deleted video]"))
                    continue;
                videos.append(flatVideo(obj));
            }
            // Non-zero exit with parsed entries still counts (partial page
            // failures on huge lists) — same "partial parses kept" stance as RSS.
            const bool ok = proc->exitStatus() == QProcess::NormalExit
                            && (proc->exitCode() == 0 || !videos.isEmpty());
            if (ok)
                m_playlists.store(playlistId, title, videos);
            // On failure: keep any previously cached videos (stale beats empty)

            if (m_playlists.landed())
                finishPlaylistAggregate();
            else
                spawnNextPlaylistFetch();
        });
    }
}

void YouTubeBackend::finishPlaylistAggregate() {
    if (m_playlists.finish(QDateTime::currentMSecsSinceEpoch()))
        setProblem(QString());
    else
        setProblem(QString::fromLatin1(kPlaylistsProblem));
    // The playlists folder too: its names may have come in with the videos.
    QStringList answered = std::exchange(m_playlists.waits, {});
    if (!answered.contains(QStringLiteral("playlists")))
        answered << QStringLiteral("playlists");
    answerLater(answered);
}

// ---------------------------------------------------------------------------
// ADVANCED settings → yt-dlp format and mpv arguments
// ---------------------------------------------------------------------------

// The languages YouTube most often has dubbed audio and subtitles in, by the
// code yt-dlp reports them with; a code also matches its regions ("pt" takes
// "pt-BR").
static const struct { const char *code; const char *label; } kLanguages[] = {
    {"en", "English"},    {"es", "Spanish"},  {"fr", "French"},  {"de", "German"},
    {"it", "Italian"},    {"pt", "Portuguese"}, {"nl", "Dutch"}, {"pl", "Polish"},
    {"ru", "Russian"},    {"tr", "Turkish"},  {"ar", "Arabic"},  {"hi", "Hindi"},
    {"id", "Indonesian"}, {"ja", "Japanese"}, {"ko", "Korean"},  {"zh", "Chinese"},
};

static QVariantList languageOptions() {
    QVariantList options;
    for (const auto &l : kLanguages)
        options << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(l.code)},
                               {QStringLiteral("label"), QString::fromLatin1(l.label)}};
    return options;
}

// The PLAYBACK RESOLUTION setting's height in lines (480 for one it doesn't know).
static int resolutionHeight(const QString &resolution) {
    static const QHash<QString, int> kHeights{
        {QStringLiteral("240p"), 240},   {QStringLiteral("360p"), 360},
        {QStringLiteral("480p"), 480},   {QStringLiteral("720p"), 720},
        {QStringLiteral("1080p"), 1080}, {QStringLiteral("1440p"), 1440},
        {QStringLiteral("2160p"), 2160}};
    return kHeights.value(resolution, 480);
}

QString YouTubeBackend::ytdlFormat(const QString &resolution, const QString &codec,
                                   const QString &maxFrameRate, const QString &audioLanguage) const {
    return ytdlp::format(resolutionHeight(resolution), codec != QLatin1String("Any"),
                         maxFrameRate == QLatin1String("30"), audioLanguage);
}

QString YouTubeBackend::ytdlFormat(const QVariantMap &settings) const {
    return ytdlFormat(settings.value(QStringLiteral("resolution")).toString(),
                      settings.value(QStringLiteral("codec")).toString(),
                      settings.value(QStringLiteral("maxFrameRate")).toString(),
                      settings.value(QStringLiteral("audioLanguage")).toString());
}

QStringList YouTubeBackend::playbackArgs(const QVariantMap &settings) const {
    QStringList args{
        QStringLiteral("--ytdl=yes"),
        QStringLiteral("--ytdl-format=") + ytdlFormat(settings)};

    // yt-dlp's own options, which mpv's hook passes on: all in one list, since
    // a second --ytdl-raw-options would replace the first.
    QStringList raw;
    // Without these mpv has yt-dlp list every subtitle the video has, none shown.
    const QString subtitles = settings.value(QStringLiteral("subtitles")).toString();
    if (subtitles == QLatin1String("On") || subtitles == QLatin1String("With Auto")) {
        const QString language = settings.value(QStringLiteral("subtitleLanguage")).toString().trimmed().toLower();
        const bool withAuto = subtitles == QLatin1String("With Auto");
        // Any ("-", or none): the subtitles the video has in whatever
        // language, and with the automatic captions only those in the
        // language it was made in (yt-dlp's "-orig"), never their many
        // translations.
        const bool any = language.isEmpty() || language == QLatin1String("-");
        raw << QStringLiteral("write-subs=")
            << QStringLiteral("sub-langs=") + (!any ? language + QStringLiteral(".*")
                                               : withAuto ? QStringLiteral(".*-orig")
                                               : QStringLiteral("all,-live_chat"));
        if (withAuto)
            raw << QStringLiteral("write-auto-subs=");
    }
    // The account, as the app's own yt-dlp runs have it. A path may hold a
    // comma, so the value goes in mpv's length-prefixed quoting: %bytes%value.
    const QString cookies = cookiesFromBrowser();
    if (!cookies.isEmpty())
        raw << QStringLiteral("cookies-from-browser=%") + QString::number(cookies.toUtf8().size())
                   + QLatin1Char('%') + cookies;
    if (!raw.isEmpty())
        args << QStringLiteral("--ytdl-raw-options=") + raw.join(QLatin1Char(','));

    bool ok = false;
    const double speed = settings.value(QStringLiteral("speed")).toString()
                             .remove(QLatin1Char('x')).toDouble(&ok);
    if (ok && speed > 0.0 && qAbs(speed - 1.0) > 0.001)
        args << QStringLiteral("--speed=%1").arg(speed);
    return args;
}

QVariantMap YouTubeBackend::playbackSettings() const {
    auto setting = [this](const char *key, const char *fallback) {
        const QString value = m_appCore
            ? m_appCore->get_setting(kModuleId, QLatin1String(key)).toString()
            : QString();
        return value.isEmpty() ? QString::fromLatin1(fallback) : value;
    };
    return {{QStringLiteral("resolution"), setting("playback_resolution", "480p")},
            {QStringLiteral("codec"), setting("video_codec", "H.264")},
            {QStringLiteral("maxFrameRate"), setting("max_frame_rate", "Any")},
            {QStringLiteral("audioLanguage"), setting("audio_language", "original")},
            {QStringLiteral("subtitles"), setting("subtitles", "Off")},
            {QStringLiteral("subtitleLanguage"), setting("subtitle_language", "-")},
            {QStringLiteral("speed"), setting("playback_speed", "1x")}};
}

QStringList YouTubeBackend::downloadArgs(bool canMerge) const {
    const QVariantMap s = playbackSettings();
    const QString format = canMerge
        ? ytdlFormat(s)
        : QStringLiteral("best[height<=?%1][vcodec^=avc1]/best[height<=?%1]/best")
              .arg(resolutionHeight(s.value(QStringLiteral("resolution")).toString()));
    QStringList args = cookieArgs();
    args << QStringLiteral("-f") << format;
    if (canMerge)
        args << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
    return args;
}

void YouTubeBackend::get_audio_languages() {
    QVariantList options{QVariantMap{{QStringLiteral("id"), QStringLiteral("original")},
                                     {QStringLiteral("label"), QStringLiteral("Original")}}};
    options << languageOptions();
    emit dynamicOptionsReady(QStringLiteral("audio_language"), options);
}

// Subtitle Language: Any (no preference, "-", where nothing is saved), then
// the languages.
void YouTubeBackend::get_subtitle_languages() {
    QVariantList options{QVariantMap{{QStringLiteral("id"), QStringLiteral("-")},
                                     {QStringLiteral("label"), QStringLiteral("Any")}}};
    options << languageOptions();
    emit dynamicOptionsReady(QStringLiteral("subtitle_language"), options);
}

// Subtitles, in the order every module's has, each saved as it always was:
// With Auto reads "On + Auto Captions".
void YouTubeBackend::get_subtitle_options() {
    QVariantList options;
    for (const auto &[id, label] : { std::pair{ "Off", "Off" }, std::pair{ "On", "On" },
                                     std::pair{ "With Auto", "On + Auto Captions" } })
        options << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                               {QStringLiteral("label"), QString::fromLatin1(label)}};
    emit dynamicOptionsReady(QStringLiteral("subtitles"), options);
}

// ---------------------------------------------------------------------------
// Watch history (youtube_history.json, keyed by videoId)
// Entry: { pos: <ms>, title, channelName, lastPlayed: <epoch ms> }
// Legacy pos-only entries are tolerated: they resume fine but are skipped by
// the History list (nothing to display) and pruned first (lastPlayed 0).
// ---------------------------------------------------------------------------

QVariantMap YouTubeBackend::getSavedPosition(const QString &videoId) {
    const QVariant val = m_history.entry(videoId);
    if (!val.isValid())
        return {};
    return val.toMap();
}

void YouTubeBackend::savePosition(const QString &videoId, int positionMs,
                                  const QString &title, const QString &channelName) {
    QVariantMap entry;
    entry["pos"]         = positionMs;
    entry["title"]       = title;
    entry["channelName"] = channelName;
    m_history.save(videoId, entry);     // with lastPlayed, now
}

QVariantList YouTubeBackend::getHistory() const {
    const QVariantMap history = m_history.entries();
    QVariantList items;
    for (auto it = history.begin(); it != history.end(); ++it) {
        const QVariantMap entry = it.value().toMap();
        const QString title = entry.value("title").toString();
        if (title.isEmpty())
            continue; // legacy resume-only entry — nothing to display
        QVariantMap v;
        v["videoId"]     = it.key();
        v["title"]       = title;
        v["channelName"] = entry.value("channelName").toString();
        v["lastPlayed"]  = entry.value("lastPlayed").toLongLong();
        v["url"]         = watchUrlFor(it.key());
        items << v;
    }
    std::sort(items.begin(), items.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("lastPlayed").toLongLong()
             > b.toMap().value("lastPlayed").toLongLong();
    });
    return items;
}

void YouTubeBackend::delete_history() {
    m_history.clearAll();
}

// ---------------------------------------------------------------------------
// Watch later (youtube_watch_later.json — JSON array, newest-saved first)
// Entry: { videoId, title, channelName, addedMs }
// ---------------------------------------------------------------------------

QString YouTubeBackend::watchLaterFilePath() const {
    return m_dataRoot + "/youtube_watch_later.json";
}

QVariantList YouTubeBackend::loadWatchLater() const {
    QFile file(watchLaterFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).array().toVariantList();
}

void YouTubeBackend::saveWatchLater(const QVariantList &list) {
    writeFileAtomically(watchLaterFilePath(),
                        QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
}

QVariantList YouTubeBackend::getWatchLater() const {
    QVariantList items = loadWatchLater();
    for (QVariant &v : items) {
        QVariantMap m = v.toMap();
        m["url"] = watchUrlFor(m.value("videoId").toString());
        v = m;
    }
    return items;
}

bool YouTubeBackend::isInWatchLater(const QString &videoId) const {
    const QVariantList list = loadWatchLater();
    for (const QVariant &v : list) {
        if (v.toMap().value("videoId").toString() == videoId)
            return true;
    }
    return false;
}

void YouTubeBackend::addToWatchLater(const QString &videoId, const QString &title,
                                     const QString &channelName) {
    if (videoId.isEmpty() || isInWatchLater(videoId))
        return;
    QVariantList list = loadWatchLater();
    QVariantMap entry;
    entry["videoId"]     = videoId;
    entry["title"]       = title;
    entry["channelName"] = channelName;
    entry["addedMs"]     = QDateTime::currentMSecsSinceEpoch();
    list.prepend(entry);
    saveWatchLater(list);
}

void YouTubeBackend::removeFromWatchLater(const QString &videoId) {
    QVariantList list = loadWatchLater();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list[i].toMap().value("videoId").toString() == videoId)
            list.removeAt(i);
    }
    if (list.isEmpty())
        QFile::remove(watchLaterFilePath());
    else
        saveWatchLater(list);
}

void YouTubeBackend::delete_watch_later() {
    QFile::remove(watchLaterFilePath());
}

// ---------------------------------------------------------------------------
// The module's tree (TreeBrowser): every list above as a folder, and search.
// The feeds and playlists come from the same caches the loaders fill; a path
// that has to wait for one is answered (listingReady) once it is in.
// ---------------------------------------------------------------------------

static QVariantMap treeFolder(const QString &name, const QString &path) {
    return { { "name", name }, { "path", path }, { "isFolder", true } };
}

static QVariantMap treeAction(const QString &name, const QString &kind, const QString &path) {
    return { { "name", name }, { "path", path }, { "isFolder", false }, { "kind", kind } };
}

QVariant YouTubeBackend::entries(const QString &path, bool preview) {
    QVariantList list;
    if (path == QLatin1String("favorites")) {
        list = m_appCore ? m_appCore->get_list(kModuleId, QStringLiteral("favorites")) : QVariantList();
    } else {
        const QVariant listed = listing(path, preview);
        if (!listed.isValid())
            return listed;
        list = listed.toList();
        if (path == QLatin1String("home"))
            list = QVariantList{treeFolder(QStringLiteral("Recently Watched"), QStringLiteral("history")),
                                treeFolder(QStringLiteral("Favorites"), QStringLiteral("favorites"))} + list;
    }
    // Shorts are left out with DISPLAY SHORTS off; unset is on.
    const bool showShorts = !m_appCore || m_appCore->setting_on(kModuleId, QStringLiteral("display_shorts"), true);
    if (showShorts)
        return list;
    QVariantList kept;
    for (const QVariant &e : list)
        if (!e.toMap().value(QStringLiteral("isShort")).toBool())
            kept << e;
    return kept;
}

QVariant YouTubeBackend::listing(const QString &path, bool preview) {
    if (path == QLatin1String("home")) {
        // A visit starts clean: a source that is still failing says so
        // again when it is asked.
        setProblem(QString());
        QVariantList entries{ treeAction(QStringLiteral("Search"), QStringLiteral("search"),
                                         QStringLiteral("home/search")) };
        if (!readSubscriptionIds().isEmpty())
            entries << treeFolder(QStringLiteral("Subscriptions"), QStringLiteral("subscriptions"))
                    << treeFolder(QStringLiteral("Channels"), QStringLiteral("channels"));
        if (!readPlaylistEntries().isEmpty())
            entries << treeFolder(QStringLiteral("Playlists"), QStringLiteral("playlists"));
        if (!loadWatchLater().isEmpty())
            entries << treeFolder(QStringLiteral("Watch Later"), QStringLiteral("watchlater"));
        return entries;
    }
    if (path == QLatin1String("watchlater"))
        return videoEntries(getWatchLater());
    if (path == QLatin1String("history"))
        return videoEntries(getHistory());
    if (path == QLatin1String("subscriptions") || path == QLatin1String("channels")
        || path.startsWith(QLatin1String("channel/")))
        return channelListing(path);
    if (path == QLatin1String("playlists") || path.startsWith(QLatin1String("playlist/")))
        return playlistListing(path, preview);
    if (path.startsWith(QLatin1String("search/")))
        return searchListing(path);
    return QVariantList();
}

QVariant YouTubeBackend::channelListing(const QString &path) {
    const QStringList ids = readSubscriptionIds();
    if (ids.isEmpty())
        return QVariantList();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_channels.failedMs < kRetryAfterMs) {
        setProblem(QString::fromLatin1(kSubscriptionsProblem));
        return QVariantList();
    }
    // A channel whose own feed is fresh is listed as it is, the others
    // perhaps still being asked for.
    if (path.startsWith(QLatin1String("channel/"))) {
        const Source entry = m_channels.sources.value(path.section(QLatin1Char('/'), 1));
        if (entry.ok && now - entry.fetchedMs < kCacheTtlMs)
            return videoEntries(entry.videos);
    }
    // Fresh as a whole: a channel whose own feed failed stays empty until the
    // next load, rather than having the tree ask again and again.
    if (!m_channels.fresh(ids, now)) {
        if (!m_channels.waits.contains(path))
            m_channels.waits << path;
        ensureFresh(false);
        // The channel opened is asked for next, ahead of those still waiting.
        if (path.startsWith(QLatin1String("channel/"))) {
            const QString id = path.section(QLatin1Char('/'), 1);
            if (m_channels.fetchQueue.removeOne(id))
                m_channels.fetchQueue.prepend(id);
        }
        return QVariant();
    }
    if (path == QLatin1String("subscriptions"))
        return videoEntries(buildFeed());
    if (path == QLatin1String("channels")) {
        QVariantList entries;
        for (const QVariant &v : buildChannelList()) {
            const QVariantMap channel = v.toMap();
            entries << treeFolder(channel.value("title").toString(),
                                  QStringLiteral("channel/") + channel.value("channelId").toString());
        }
        return entries;
    }
    return videoEntries(m_channels.sources.value(path.section(QLatin1Char('/'), 1)).videos);
}

QVariant YouTubeBackend::playlistListing(const QString &path, bool preview) {
    const QList<PlaylistFileRef> refs = readPlaylistEntries();
    if (refs.isEmpty())
        return QVariantList();
    if (path == QLatin1String("playlists")) {
        // Straight from the file: named as it names them, or as yt-dlp last
        // reported. Their videos are fetched once one is opened.
        QVariantList entries;
        for (const PlaylistFileRef &ref : refs) {
            const QString fetched = m_playlists.sources.value(ref.id).title;
            entries << treeFolder(!ref.name.isEmpty() ? ref.name : !fetched.isEmpty() ? fetched : ref.id,
                                  QStringLiteral("playlist/") + ref.id);
        }
        return entries;
    }
    QStringList ids;
    for (const PlaylistFileRef &ref : refs)
        ids << ref.id;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_playlists.fresh(ids, now))
        return videoEntries(m_playlists.sources.value(path.section(QLatin1Char('/'), 1)).videos);
    if (now - m_playlists.failedMs < kRetryAfterMs) {
        setProblem(QString::fromLatin1(kPlaylistsProblem));
        return QVariantList();
    }
    if (preview)
        return QVariant();
    if (!m_playlists.waits.contains(path))
        m_playlists.waits << path;
    ensurePlaylistsFresh(false);
    return QVariant();
}

QVariant YouTubeBackend::searchListing(const QString &path) {
    const Search search = m_searches.value(path);
    if (search.requested > 0) {
        QVariantList entries = videoEntries(search.videos);
        if (!search.exhausted)
            entries << treeAction(QStringLiteral("More…"), QStringLiteral("more"),
                                  path + QStringLiteral("#more"));
        return entries;
    }
    if (QDateTime::currentMSecsSinceEpoch() - search.failedMs < kRetryAfterMs) {
        setProblem(ytdlp::locate(m_dataRoot).isEmpty() ? noYtDlpProblem()
                                                       : QString::fromLatin1(kSearchProblem));
        return QVariantList();
    }
    searchPage(path);
    return QVariant();
}

void YouTubeBackend::loadMore(const QString &path) {
    if (m_searches.value(path).requested > 0)
        searchPage(path);
}

void YouTubeBackend::forgetSearches() {
    for (auto it = m_searches.begin(); it != m_searches.end();) {
        if (it->loading)
            ++it;
        else
            it = m_searches.erase(it);
    }
}

// One page of matches, through the same yt-dlp mpv plays them with.
void YouTubeBackend::searchPage(const QString &path) {
    Search &search = m_searches[path];
    if (search.loading || search.exhausted)
        return;
    const QString bin = ytdlp::locate(m_dataRoot);
    if (bin.isEmpty()) {
        search.failedMs = QDateTime::currentMSecsSinceEpoch();
        setProblem(noYtDlpProblem());
        answerLater({ path });
        return;
    }
    search.loading = true;
    const int first = search.requested + 1;
    const int last  = search.requested + kSearchPageSize;
    const QStringList args = cookieArgs() + QStringList{
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("-I"), QStringLiteral("%1:%2").arg(first).arg(last),
        QStringLiteral("--print"),
        QStringLiteral("%(.{id,title,channel,uploader,url,live_status})j"),
        QStringLiteral("--"),
        QStringLiteral("ytsearch%1:%2").arg(last).arg(path.section(QLatin1Char('/'), 1)),
    };

    runYtDlp(args, [this, path, last](QProcess *proc) {
        int found = 0;
        QVariantList videos;
        const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            const QJsonObject obj = QJsonDocument::fromJson(line.trimmed()).object();
            if (obj.value(QLatin1String("id")).toString().isEmpty())
                continue;
            ++found;
            // Announced, not on yet: nothing to play.
            if (obj.value(QLatin1String("live_status")).toString() == QLatin1String("is_upcoming"))
                continue;
            videos.append(flatVideo(obj));
        }
        Search &search = m_searches[path];
        search.loading = false;
        if (proc->exitStatus() == QProcess::NormalExit && (proc->exitCode() == 0 || found > 0)) {
            search.videos   += videos;
            search.requested = last;
            // Fewer than asked for: YouTube has no more.
            search.exhausted = found < kSearchPageSize;
            setProblem(QString());
        } else {
            search.failedMs = QDateTime::currentMSecsSinceEpoch();
            setProblem(QString::fromLatin1(kSearchProblem));
        }
        emit listingReady(path);
    });
}

QVariantList YouTubeBackend::videoEntries(const QVariantList &videos) const {
    QVariantList entries;
    for (const QVariant &v : videos) {
        QVariantMap video = v.toMap();
        video["name"]     = video.value("title");
        video["path"]     = QStringLiteral("video/") + video.value("videoId").toString();
        video["isFolder"] = false;
        video["kind"]     = QStringLiteral("video");
        entries << video;
    }
    return entries;
}

// After the caller has returned: a tree asking listing() must never be
// refreshed from inside that call.
void YouTubeBackend::answerLater(const QStringList &paths) {
    if (paths.isEmpty())
        return;
    QTimer::singleShot(0, this, [this, paths]() {
        for (const QString &path : paths)
            emit listingReady(path);
    });
}

void YouTubeBackend::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}

// ---------------------------------------------------------------------------
// A video's info screen: what the list knows at once, then what yt-dlp adds.
// ---------------------------------------------------------------------------

QVariantMap YouTubeBackend::detailsOf(const QVariantMap &video, bool complete) const {
    const QVariantMap extra = m_details.value(video.value("videoId").toString());
    const auto pick = [&video, &extra](const char *key) {
        const QString value = extra.value(QLatin1String(key)).toString();
        return value.isEmpty() ? video.value(QLatin1String(key)).toString() : value;
    };
    // YYYYMMDD from yt-dlp, an ISO date from the feed.
    QString date = extra.value("upload_date").toString();
    if (date.size() == 8)
        date = date.left(4) + QLatin1Char('-') + date.mid(4, 2) + QLatin1Char('-') + date.mid(6, 2);
    if (date.isEmpty())
        date = video.value("publishedAt").toString().left(10);
    QString channel = pick("channel");
    if (channel.isEmpty())
        channel = video.value("channelName").toString();

    QVariantMap details;
    details["title"] = pick("title");
    QStringList facts;
    if (!channel.isEmpty())
        facts << channel;
    if (!date.isEmpty())
        facts << date;
    details["facts"] = facts.join(QStringLiteral(" - "));
    details["summary"] = pick("description");

    QVariantList rows;
    const auto row = [&rows](const QString &label, const QString &value) {
        if (!value.isEmpty())
            rows << QVariantMap{ { "label", label }, { "value", value } };
    };
    row(QStringLiteral("Channel"), channel);
    row(QStringLiteral("Date"), date);
    row(QStringLiteral("Length"), extra.value("duration_string").toString());
    const qint64 views = extra.value("view_count").toLongLong();
    if (views > 0)
        row(QStringLiteral("Views"), QLocale(QLocale::English).toString(views));
    details["rows"] = rows;
    details["complete"] = complete;
    return details;
}

void YouTubeBackend::loadDetails(const QVariantMap &video) {
    const QString path = video.value("path").toString();
    const QString videoId = video.value("videoId").toString();
    const bool known = m_details.contains(videoId);
    const QVariantMap now = detailsOf(video, known || ytdlp::locate(m_dataRoot).isEmpty());
    // Never from inside the call: the view showing them is still opening.
    QTimer::singleShot(0, this, [this, path, now]() { emit detailsReady(path, now); });
    if (!known && !now.value("complete").toBool())
        fetchDetails(video);
}

// One video's metadata from yt-dlp, a few seconds' work on a Pi: one fetch at
// a time, and only the last video asked for while one runs.
void YouTubeBackend::fetchDetails(const QVariantMap &video) {
    if (m_fetchingDetails) {
        m_detailsNext = video;
        return;
    }
    m_fetchingDetails = true;
    const QStringList args = cookieArgs() + QStringList{
        QStringLiteral("--skip-download"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--print"),
        QStringLiteral("%(.{title,channel,uploader,upload_date,duration_string,view_count,description})j"),
        QStringLiteral("--"),
        video.value("url").toString(),
    };
    runYtDlp(args, [this, video](QProcess *proc) {
        m_fetchingDetails = false;
        const QJsonObject obj = QJsonDocument::fromJson(
            proc->readAllStandardOutput().trimmed().split('\n').value(0)).object();
        QVariantMap extra = obj.toVariantMap();
        if (extra.value("channel").toString().isEmpty())
            extra["channel"] = extra.value("uploader");
        const QString videoId = video.value("videoId").toString();
        if (!obj.isEmpty())
            m_details.insert(videoId, extra);
        // Complete either way: what yt-dlp couldn't say, the screen does without.
        emit detailsReady(video.value("path").toString(), detailsOf(video, true));
        if (!m_detailsNext.isEmpty()) {
            const QVariantMap next = std::exchange(m_detailsNext, {});
            if (!m_details.contains(next.value("videoId").toString()))
                fetchDetails(next);
        }
    });
}
