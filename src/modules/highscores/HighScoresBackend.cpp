#include "HighScoresBackend.h"
#include "../arcade/Mame.h"
#include "../../util/AtomicFile.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <exception>
#include <utility>

#ifdef OSDOS_HIGH_SCORES
#include <openhi2txt/openhi2txt.h>
#endif

namespace {

// Recently Played: the games the Arcade module played last, this many.
constexpr int kRecent = 30;
// How the games are read and kept: one more reads every game again.
constexpr int kCacheVersion = 1;

QVariantMap folder(const QString &name, const QString &path) {
    return { { QStringLiteral("name"), name }, { QStringLiteral("path"), path },
             { QStringLiteral("isFolder"), true } };
}

// A link at path to target, made again when it points elsewhere. A folder
// where the link was (a backup restored through it) is a copy in the
// module's own folder, and goes.
void link(const QString &target, const QString &path) {
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (info.symLinkTarget() == target)
            return;
        QFile::remove(path);
    } else if (info.isDir()) {
        QDir(path).removeRecursively();
    } else if (info.exists()) {
        QFile::remove(path);
    }
    if (!QFile::link(target, path))
        qWarning("[HighScores] can't link %s to %s", qPrintable(path), qPrintable(target));
}

// A file as a game's stamp has it: its name, size and time.
QString stampOf(const QFileInfo &info) {
    return info.fileName() + QLatin1Char(':') + QString::number(info.size()) + QLatin1Char(':')
           + QString::number(info.lastModified().toMSecsSinceEpoch());
}

// What a file holds, not when it was written: an update that brings the
// same definitions reads nothing again.
QString digestOf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QString identityOf(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists())
        return QString();
    return QString::number(info.size()) + QLatin1Char(':') + QString::number(info.lastModified().toMSecsSinceEpoch());
}

// A game with no table, its fields alone (a best score, as Solar Fox
// keeps): a line each, lined up as a table of two columns.
QVariantList fieldEntries(const scores::Game &game) {
    scores::Table table;
    table.columns = { QStringLiteral("FIELD"), QStringLiteral("VALUE") };
    for (const auto &[id, value] : game.fields)
        table.rows << QStringList{ scores::label(id), value };
    const QStringList lines = scores::lines(table);
    QVariantList entries;
    for (int i = 0; i < lines.size(); ++i) {
        const QStringList &row = table.rows.at(i);
        entries << QVariantMap{ { QStringLiteral("name"), lines.at(i) },
                                { QStringLiteral("path"), QStringLiteral("field:%1:%2").arg(game.set, QString::number(i)) },
                                { QStringLiteral("isFolder"), false },
                                { QStringLiteral("kind"), QStringLiteral("row") },
                                { QStringLiteral("detail"), row.at(0) + QLatin1Char(' ') + row.at(1) } };
    }
    return entries;
}

#ifdef OSDOS_HIGH_SCORES
// The names of these sets as the Arcade module has them from MAME
// (arcade/games.json), where it has them.
QHash<QString, QString> namesOf(const QString &gamesJson, const QStringList &sets) {
    QHash<QString, QString> names;
    QFile file(gamesJson);
    if (!file.open(QIODevice::ReadOnly))
        return names;
    const QJsonObject machines = QJsonDocument::fromJson(file.readAll()).object()
                                     .value(QStringLiteral("machines")).toObject();
    for (const QString &set : sets) {
        const QString name = machines.value(set).toObject().value(QStringLiteral("description")).toString();
        if (!name.isEmpty())
            names.insert(set, name);
    }
    return names;
}

QString fromStd(const std::string &s) {
    return QString::fromStdString(s);
}

QString messageFor(const openhi2txt::HiScoreResult &result) {
    switch (result.errorKind) {
    case openhi2txt::HiScoreErrorKind::DefinitionNotFound:
        return QStringLiteral("hi2txt has no description of this game's high scores yet");
    case openhi2txt::HiScoreErrorKind::StructureNotMatched:
        return QStringLiteral("The file isn't laid out as hi2txt knows this game's: another version of it?");
    case openhi2txt::HiScoreErrorKind::DefinitionInvalid:
    case openhi2txt::HiScoreErrorKind::OutputNotFound:
        return QStringLiteral("hi2txt's description of this game can't be read");
    case openhi2txt::HiScoreErrorKind::InputNotFound:
        // A .hi file kept, its definition reading the game's NVRAM alone.
        return QStringLiteral("hi2txt reads this game's table from a file MAME hasn't saved");
    case openhi2txt::HiScoreErrorKind::InvalidData:
        return QStringLiteral("The file holds what hi2txt can't make out");
    default:
        return QStringLiteral("The high scores couldn't be read: ") + fromStd(result.error);
    }
}

// One game's files, read as its definition says, into game.
void readInto(scores::Game &game, const openhi2txt::Context &context, const QString &root) {
    const std::string set = game.set.toStdString();
    // A game's NVRAM alone, which its definition (if any) reads no table
    // from: most games keep their settings there, not their scores.
    if (game.file.startsWith(QLatin1String("nvram/")) && !context.hasInputForGame(set)) {
        game.hidden = true;
        return;
    }
    openhi2txt::ReadOptions read;
    read.includeExtra = false;
    read.useDefaultFallback = false;
    read.scoreGrouping = true;
    read.scoreGroupingSeparator = ",";
    const openhi2txt::HiScoreResult result = context.refreshGame(set, read);
    if (!result.ok) {
        game.error = messageFor(result);
        return;
    }
    // The file the definition read, as it is in MAME's folders.
    if (!result.usedInputPath.empty()) {
        const QString used = QDir(root).relativeFilePath(fromStd(result.usedInputPath));
        game.file = used.startsWith(QLatin1String("hiscore/")) ? used.mid(8) : used;
    }
    for (const openhi2txt::HiScoreTable &t : result.tables) {
        if (t.display != openhi2txt::DisplayLevel::Always)
            continue;
        scores::Table table;
        table.id = fromStd(t.id);
        for (const std::string &column : t.columns)
            table.columns << fromStd(column);
        for (const std::vector<std::string> &row : t.rows) {
            QStringList cells;
            for (const std::string &cell : row)
                cells << fromStd(cell);
            table.rows << cells;
        }
        if (!table.rows.isEmpty())
            game.tables << table;
    }
    for (const openhi2txt::HiScoreField &field : result.fields) {
        if (field.display == openhi2txt::DisplayLevel::Always)
            game.fields << qMakePair(fromStd(field.id), fromStd(field.value));
    }
    if (game.tables.isEmpty() && game.fields.isEmpty())
        game.error = QStringLiteral("The file holds no high score table yet");
}

// readInto, with what openhi2txt throws (what the filesystem refuses it: a
// link that loops, a folder it can't read) as why the game can't be read.
scores::Game readGame(const openhi2txt::Context &context, const QString &root,
                      const HighScoresBackend::Source &source) {
    scores::Game game;
    game.set = source.set;
    game.file = source.file;
    game.stamp = source.stamp;
    QString thrown;
    try {
        readInto(game, context, root);
        return game;
    } catch (const std::exception &e) {
        thrown = QString::fromLocal8Bit(e.what());
    } catch (...) {
    }
    game.file = source.file;
    game.tables.clear();
    game.fields.clear();
    game.hidden = false;
    game.error = QStringLiteral("The high scores couldn't be read") + (thrown.isEmpty() ? QString() : QStringLiteral(": ") + thrown);
    return game;
}
#endif

} // namespace

HighScoresBackend::HighScoresBackend(const QString &appRoot, const QString &dataRoot,
                                     std::function<QStringList()> recentSets, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_arcade(dataRoot + QStringLiteral("/arcade")), m_home(dataRoot + QStringLiteral("/highscores")),
      m_definitions(appRoot + QStringLiteral("/modules/highscores/data/hi2txt.zip")),
      m_recentSets(std::move(recentSets))
{
#ifdef OSDOS_HIGH_SCORES
    if (!QFileInfo::exists(m_definitions))
        m_problem = QStringLiteral("hi2txt's game definitions are missing: ") + m_definitions;
#else
    m_problem = QStringLiteral("This build of OSD/OS reads no high score tables: it was built without minizip");
#endif
    m_watcher = new QFutureWatcher<Read>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &HighScoresBackend::onRead);
}

HighScoresBackend::~HighScoresBackend() {
    // The read keeps nothing of this object's: it may finish on its own.
    m_watcher->disconnect(this);
}

QHash<QString, HighScoresBackend::Source> HighScoresBackend::listSources() const {
    QHash<QString, Source> sources;
    // The hiscore plugin's files.
    QDirIterator hi(m_arcade + QStringLiteral("/hiscore"), { QStringLiteral("*.hi") }, QDir::Files);
    while (hi.hasNext()) {
        hi.next();
        const QFileInfo info = hi.fileInfo();
        const QString set = info.completeBaseName().toLower();
        sources.insert(set, { set, info.fileName(), stampOf(info) });
    }
    // The games' NVRAM, a folder each, every file of it in the stamp.
    QDirIterator nvram(m_arcade + QStringLiteral("/nvram"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (nvram.hasNext()) {
        nvram.next();
        const QFileInfoList files = QDir(nvram.filePath()).entryInfoList(QDir::Files, QDir::Name);
        if (files.isEmpty())
            continue;
        const QString set = nvram.fileName().toLower();
        Source &source = sources[set];
        if (source.set.isEmpty())
            source = { set, QStringLiteral("nvram/") + nvram.fileName(), QString() };
        for (const QFileInfo &file : files)
            source.stamp += QStringLiteral("|nvram/") + stampOf(file);
    }
    return sources;
}

void HighScoresBackend::prepareRoot() const {
    const QString root = m_home + QStringLiteral("/mame");
    QDir().mkpath(root + QStringLiteral("/plugins/hiscore"));
    link(m_arcade + QStringLiteral("/hiscore"), root + QStringLiteral("/hiscore"));
    link(m_arcade + QStringLiteral("/nvram"), root + QStringLiteral("/nvram"));
    const QString plugins = mame::pluginsFolder(mame::programPath(m_dataRoot));
    const QString dat = plugins.isEmpty() ? QString() : plugins + QStringLiteral("/hiscore/hiscore.dat");
    if (!dat.isEmpty() && QFileInfo::exists(dat))
        link(QFileInfo(dat).absoluteFilePath(), root + QStringLiteral("/plugins/hiscore/hiscore.dat"));
    else
        QFile::remove(root + QStringLiteral("/plugins/hiscore/hiscore.dat"));
}

QString HighScoresBackend::definitionsIdentity() const {
    // By what the files hold, so an update that brings the same definitions
    // reads nothing again; digested (a megabyte of them) only when a file's
    // size or date has changed since the last asking, as this is asked at
    // every refresh (the Arcade screen opening, each game's end).
    const QString dat = m_home + QStringLiteral("/mame/plugins/hiscore/hiscore.dat");
    const QString stamp = identityOf(m_definitions) + QLatin1Char('|') + identityOf(dat);
    if (stamp != m_definitionsStamp) {
        m_definitionsStamp = stamp;
        m_definitionsIdentity = QString::number(kCacheVersion) + QLatin1Char('|') + digestOf(m_definitions)
                                + QLatin1Char('|') + digestOf(dat);
    }
    return m_definitionsIdentity;
}

QString HighScoresBackend::gamesJsonIdentity() const {
    return identityOf(m_arcade + QStringLiteral("/games.json"));
}

void HighScoresBackend::loadCache() {
    m_loaded = true;
    QFile file(m_home + QStringLiteral("/scores.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject cache = QJsonDocument::fromJson(file.readAll()).object();
    m_identity = cache.value(QStringLiteral("definitions")).toString();
    m_namesFrom = cache.value(QStringLiteral("namesFrom")).toString();
    const QJsonObject games = cache.value(QStringLiteral("games")).toObject();
    for (auto it = games.constBegin(); it != games.constEnd(); ++it) {
        scores::Game game = scores::Game::fromVariant(it.value().toObject().toVariantMap());
        game.set = it.key();
        m_games.insert(it.key(), game);
    }
    const QJsonObject names = cache.value(QStringLiteral("names")).toObject();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        m_names.insert(it.key(), it.value().toString());
}

void HighScoresBackend::saveCache() const {
    QJsonObject games;
    for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it)
        games.insert(it.key(), QJsonObject::fromVariantMap(it.value().toVariant()));
    QJsonObject names;
    for (auto it = m_names.constBegin(); it != m_names.constEnd(); ++it)
        names.insert(it.key(), it.value());
    const QJsonObject cache{ { QStringLiteral("definitions"), m_identity },
                             { QStringLiteral("namesFrom"), m_namesFrom },
                             { QStringLiteral("games"), games },
                             { QStringLiteral("names"), names } };
    QDir().mkpath(m_home);
    writeFileAtomically(m_home + QStringLiteral("/scores.json"), QJsonDocument(cache).toJson(QJsonDocument::Compact));
}

void HighScoresBackend::refresh() {
    // Nothing to read them with: the module's screens say why.
    if (!m_problem.isEmpty()) {
        emitShown();
        return;
    }
#ifdef OSDOS_HIGH_SCORES
    if (!m_loaded)
        loadCache();
    if (m_reading) {
        m_again = true;
        return;
    }
    prepareRoot();
    const QHash<QString, Source> sources = listSources();
    // Other definitions, another hiscore.dat, or games kept another way:
    // every game read again, what was read shown meanwhile.
    const QString identity = definitionsIdentity();
    const bool readAll = identity != m_identity;
    bool changed = false;
    // Games whose files are gone.
    for (auto it = m_games.begin(); it != m_games.end();) {
        if (!sources.contains(it.key())) {
            it = m_games.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    QList<Source> toRead;
    for (const Source &source : sources) {
        const auto it = m_games.constFind(source.set);
        if (readAll || it == m_games.constEnd() || it->stamp != source.stamp)
            toRead << source;
    }
    const QString namesFrom = gamesJsonIdentity();
    if (toRead.isEmpty() && namesFrom == m_namesFrom) {
        if (readAll) {
            m_identity = identity;
            changed = true;
        }
        if (changed)
            saveCache();
        emitShown();
        return;
    }

    const QStringList sets = sources.keys();
    const QString definitions = m_definitions;
    const QString root = m_home + QStringLiteral("/mame");
    const QString gamesJson = m_arcade + QStringLiteral("/games.json");
    if (!toRead.isEmpty())
        qInfo("[HighScores] Reading %lld games' files", static_cast<long long>(toRead.size()));
    m_namesFrom = namesFrom;
    m_readIdentity = identity;
    m_reading = true;
    emit readingChanged();
    m_watcher->setFuture(QtConcurrent::run([definitions, root, gamesJson, toRead, sets]() {
        Read read;
        // The definitions (a zip of them all) are opened only with files to
        // read; a change to the games' names alone needs none.
        if (!toRead.isEmpty()) {
            openhi2txt::ContextOptions options;
            options.definitionsZip = definitions.toStdString();
            options.mameRoot = root.toStdString();
            const openhi2txt::Context context(options);
            for (const Source &source : toRead)
                read.games << readGame(context, root, source);
        }
        read.names = namesOf(gamesJson, sets);
        return read;
    }));
#endif
}

void HighScoresBackend::onRead() {
    const Read read = m_watcher->result();
    for (const scores::Game &game : read.games) {
        if (!game.error.isEmpty())
            qInfo("[HighScores] %s: %s", qPrintable(game.file), qPrintable(game.error));
        m_games.insert(game.set, game);
    }
    m_names = read.names;
    m_identity = m_readIdentity;
    m_reading = false;
    saveCache();
    emit readingChanged();
    if (std::exchange(m_again, false)) {
        refresh();
        if (m_reading)
            return;
    }
    emitShown();
}

void HighScoresBackend::emitShown() {
    // Taken first: the folders shown ask again as these come in.
    const QSet<QString> shown = std::exchange(m_shown, {});
    for (const QString &path : shown)
        emit entriesReady(path);
}

// ---------------------------------------------------------------------------
// The tree.

QString HighScoresBackend::nameOf(const QString &set) const {
    const QString name = m_names.value(set);
    return name.isEmpty() ? set : name;
}

QList<const scores::Game *> HighScoresBackend::recentGames() const {
    QList<const scores::Game *> games;
    if (!m_recentSets)
        return games;
    const QStringList sets = m_recentSets();
    for (const QString &set : sets) {
        const auto it = m_games.constFind(set);
        if (it == m_games.constEnd() || it->hidden || !it->error.isEmpty() || games.contains(&it.value()))
            continue;
        games << &it.value();
        if (games.size() == kRecent)
            break;
    }
    return games;
}

QVariantMap HighScoresBackend::gameEntry(const scores::Game &game) const {
    QStringList detail;
    if (!game.tables.isEmpty()) {
        const QStringList best = scores::lines(game.tables.first());
        if (!best.isEmpty())
            detail << best.first().simplified();
    }
    for (const auto &[id, value] : game.fields)
        detail << scores::label(id) + QLatin1Char(' ') + value;
    detail << game.file;
    return { { QStringLiteral("name"), nameOf(game.set) },
             { QStringLiteral("path"), QStringLiteral("game:") + game.set },
             { QStringLiteral("isFolder"), true },
             { QStringLiteral("game"), game.set },
             { QStringLiteral("detail"), detail.join(QLatin1Char('\n')) } };
}

QVariantList HighScoresBackend::rowEntries(const QString &set, const scores::Table &table, const QString &key) const {
    QVariantList entries;
    const QStringList lines = scores::lines(table);
    for (int r = 0; r < lines.size(); ++r) {
        entries << QVariantMap{ { QStringLiteral("name"), lines.at(r) },
                                { QStringLiteral("path"), QStringLiteral("row:%1:%2:%3").arg(set, key, QString::number(r)) },
                                { QStringLiteral("isFolder"), false },
                                { QStringLiteral("kind"), QStringLiteral("row") },
                                { QStringLiteral("detail"), scores::describeRow(table, r).join(QLatin1Char('\n')) } };
    }
    return entries;
}

QVariantList HighScoresBackend::topEntries(const QString &set, int count) const {
    QVariantList entries;
    const auto game = m_games.constFind(set);
    if (game == m_games.constEnd() || game->hidden || !game->error.isEmpty() || count <= 0)
        return entries;
    using scores::ordinal;
    const auto entry = [](const QString &rank, const QString &score, const QString &name) {
        return QVariantMap{ { QStringLiteral("rank"), rank }, { QStringLiteral("score"), score },
                            { QStringLiteral("name"), name } };
    };
    // A column's or a field's label, in capitals, says what it holds.
    const auto isScore = [](const QString &label) { return label.contains(QLatin1String("SCORE")); };
    const auto isName = [](const QString &label) {
        return label.contains(QLatin1String("NAME")) || label.contains(QLatin1String("INITIAL"));
    };
    if (game->tables.isEmpty()) {
        // A best score kept on its own, and whose it is.
        QString score, name;
        for (const auto &[id, value] : game->fields) {
            const QString field = scores::label(id).toUpper();
            if (score.isEmpty() && isScore(field))
                score = value;
            else if (name.isEmpty() && isName(field))
                name = value;
        }
        if (!score.isEmpty())
            entries << entry(ordinal(1), score, name);
        return entries;
    }
    const scores::Table &table = game->tables.first();
    int rank = -1, score = -1, name = -1;
    for (int c = 0; c < table.columns.size(); ++c) {
        const QString column = scores::label(table.columns.at(c)).toUpper();
        if (rank < 0 && (column == QLatin1String("RANK") || column == QLatin1String("POSITION")))
            rank = c;
        else if (score < 0 && isScore(column))
            score = c;
        else if (name < 0 && isName(column))
            name = c;
    }
    // A table of times, or of rounds: its first column of those.
    for (int c = 0; score < 0 && c < table.columns.size(); ++c) {
        if (c != rank && c != name)
            score = c;
    }
    for (int r = 0; r < table.rows.size() && entries.size() < count; ++r) {
        const QStringList &row = table.rows.at(r);
        const auto cell = [&row](int c) { return c >= 0 && c < row.size() ? row.at(c).trimmed() : QString(); };
        entries << entry(ordinal(r + 1), cell(score), cell(name));
    }
    return entries;
}

QVariant HighScoresBackend::requestEntries(const QString &path) {
    // To be listed again should what it holds change.
    m_shown.insert(path);
    // Nothing read yet: the files are being read for the first time.
    if (m_reading && m_games.isEmpty())
        return QVariant();

    QList<const scores::Game *> readable, unreadable;
    for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {
        if (!it->hidden)
            (it->error.isEmpty() ? readable : unreadable) << &it.value();
    }
    const auto byName = [this](const scores::Game *a, const scores::Game *b) {
        return nameOf(a->set).compare(nameOf(b->set), Qt::CaseInsensitive) < 0;
    };

    if (path == QLatin1String("top")) {
        QVariantList top;
        if (!recentGames().isEmpty())
            top << folder(QStringLiteral("Recently Played"), QStringLiteral("recent"));
        if (!readable.isEmpty())
            top << folder(QStringLiteral("All Games"), QStringLiteral("all"));
        if (!unreadable.isEmpty())
            top << folder(QStringLiteral("Not Readable"), QStringLiteral("unreadable"));
        return top;
    }
    if (path == QLatin1String("recent") || path == QLatin1String("all")) {
        QList<const scores::Game *> list;
        if (path == QLatin1String("recent")) {
            list = recentGames();
        } else {
            list = readable;
            std::sort(list.begin(), list.end(), byName);
        }
        QVariantList entries;
        for (const scores::Game *game : std::as_const(list))
            entries << gameEntry(*game);
        return entries;
    }
    if (path == QLatin1String("unreadable")) {
        std::sort(unreadable.begin(), unreadable.end(), byName);
        QVariantList entries;
        for (const scores::Game *game : std::as_const(unreadable)) {
            entries << QVariantMap{ { QStringLiteral("name"), nameOf(game->set) },
                                    { QStringLiteral("path"), QStringLiteral("unreadable:") + game->set },
                                    { QStringLiteral("isFolder"), false },
                                    { QStringLiteral("kind"), QStringLiteral("unreadable") },
                                    { QStringLiteral("detail"), game->error + QLatin1Char('\n') + game->file } };
        }
        return entries;
    }
    if (path.startsWith(QLatin1String("game:"))) {
        const auto it = m_games.constFind(path.mid(5));
        if (it == m_games.constEnd())
            return QVariantList();
        // No table, fields alone: them. One table: its lines. Several: a
        // folder each.
        if (it->tables.isEmpty())
            return fieldEntries(*it);
        if (it->tables.size() == 1)
            return rowEntries(it->set, it->tables.first(), QStringLiteral("0"));
        QVariantList entries;
        for (int t = 0; t < it->tables.size(); ++t) {
            const scores::Table &table = it->tables.at(t);
            const QString name = table.id.isEmpty() ? QStringLiteral("Table %1").arg(t + 1) : scores::label(table.id);
            QVariantMap entry = folder(name, QStringLiteral("table:%1:%2").arg(it->set, QString::number(t)));
            const QStringList lines = scores::lines(table);
            entry.insert(QStringLiteral("detail"), lines.isEmpty() ? QString() : lines.first().simplified());
            entries << entry;
        }
        return entries;
    }
    if (path.startsWith(QLatin1String("table:"))) {
        const int colon = path.lastIndexOf(QLatin1Char(':'));
        const auto it = m_games.constFind(path.mid(6, colon - 6));
        if (colon <= 6 || it == m_games.constEnd())
            return QVariantList();
        bool ok = false;
        const int t = path.mid(colon + 1).toInt(&ok);
        if (!ok || t < 0 || t >= it->tables.size())
            return QVariantList();
        return rowEntries(it->set, it->tables.at(t), QString::number(t));
    }
    return QVariantList();
}
