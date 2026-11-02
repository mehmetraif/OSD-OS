#include "RomCheck.h"
#include "../../util/SevenZip.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>
#ifdef OSDOS_MINIZIP
#include <minizip/unzip.h>
#endif

namespace {

// MAME is asked about this many sets at a time, and given this long for them
// (it reads the table of every archive of each, and of the sets they take
// ROMs from: seconds, on a Pi too).
constexpr int kVerifyBatch = 100;
constexpr int kVerifyBatchMs = 10 * 60 * 1000;
// Files a set lacks kept for it, for its Game Details and the log.
constexpr int kMostMissing = 20;
constexpr int kVersion = 1;

QStringList capped(QStringList names) {
    if (names.size() > kMostMissing)
        names.resize(kMostMissing);
    return names;
}

} // namespace

namespace romcheck {

QHash<QString, DatSet> readDat(const QString &path) {
    QHash<QString, DatSet> sets;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return sets;
    QXmlStreamReader xml(&file);
    QString name;
    DatSet set;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            // FinalBurn Neo's DATs say game; MAME's own, machine.
            if (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                name = attributes.value(QLatin1String("name")).toString().toLower();
                set = DatSet();
                set.romOf = attributes.value(QLatin1String("romof")).toString().toLower();
            } else if (xml.name() == QLatin1String("rom") && !name.isEmpty()) {
                // A ROM without a good dump has no CRC, and isn't looked for.
                const QXmlStreamAttributes attributes = xml.attributes();
                bool ok = false;
                const quint32 crc = attributes.value(QLatin1String("crc")).toUInt(&ok, 16);
                if (!ok || attributes.value(QLatin1String("status")) == QLatin1String("nodump"))
                    continue;
                Rom rom;
                rom.name = attributes.value(QLatin1String("name")).toString();
                rom.crc = crc;
                bool sized = false;
                const qint64 size = attributes.value(QLatin1String("size")).toLongLong(&sized);
                rom.size = sized ? size : -1;
                set.roms.append(rom);
            }
        } else if (xml.isEndElement()
                   && (xml.name() == QLatin1String("game") || xml.name() == QLatin1String("machine"))) {
            if (!name.isEmpty())
                sets.insert(name, set);
            name.clear();
        }
    }
    if (xml.hasError() && xml.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        qWarning("[Arcade] FinalBurn Neo's list %s isn't whole: %s", qPrintable(path), qPrintable(xml.errorString()));
    return sets;
}

Contents contentsOf(const QString &archive, bool *ok) {
    Contents contents;
    *ok = false;
    if (archive.endsWith(QLatin1String(".7z"), Qt::CaseInsensitive)) {
        QString error;
        const QList<sevenzip::Entry> entries = sevenzip::entries(archive, &error);
        if (!error.isEmpty())
            return contents;
        for (const sevenzip::Entry &entry : entries) {
            // An empty file's CRC is 0, which the table leaves out.
            if (entry.hasCrc || entry.size == 0)
                contents.insert(entry.crc, entry.size);
        }
        *ok = true;
        return contents;
    }
#ifdef OSDOS_MINIZIP
    unzFile zip = unzOpen64(QFile::encodeName(archive).constData());
    if (!zip)
        return contents;
    int result = unzGoToFirstFile(zip);
    while (result == UNZ_OK) {
        unz_file_info64 info{};
        result = unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0);
        if (result != UNZ_OK)
            break;
        contents.insert(quint32(info.crc), qint64(info.uncompressed_size));
        result = unzGoToNextFile(zip);
    }
    unzClose(zip);
    *ok = result == UNZ_END_OF_LIST_OF_FILE;
#endif
    return contents;
}

QStringList missingRoms(const QString &set, const QHash<QString, DatSet> &dat,
                        const std::function<QString(const QString &)> &archiveOf,
                        QHash<QString, Contents> *cache, bool *unreadable) {
    *unreadable = false;
    const auto it = dat.constFind(set);
    if (it == dat.constEnd())
        return {};
    // Its own archive, then those of the sets it takes ROMs from, a BIOS
    // set's last.
    QList<Contents> searched;
    QString name = set;
    QSet<QString> seen;
    for (int depth = 0; depth < 4 && !name.isEmpty() && !seen.contains(name); ++depth) {
        seen.insert(name);
        const QString path = archiveOf(name);
        if (!path.isEmpty()) {
            auto read = cache->constFind(path);
            if (read == cache->constEnd()) {
                bool ok = false;
                const Contents contents = contentsOf(path, &ok);
                // Nothing said of a set whose own archive can't be read; a
                // parent's that can't counts as none.
                if (!ok && depth == 0) {
                    *unreadable = true;
                    return {};
                }
                read = cache->insert(path, contents);
            }
            searched.append(read.value());
        }
        name = dat.value(name).romOf;
    }
    QStringList missing;
    for (const Rom &rom : it->roms) {
        const bool found = std::any_of(searched.cbegin(), searched.cend(), [&rom](const Contents &contents) {
            return rom.size < 0 ? contents.contains(rom.crc) : contents.contains(rom.crc, rom.size);
        });
        if (!found)
            missing.append(rom.name);
    }
    return missing;
}

QString stampOf(const QFileInfo &file) {
    if (!file.exists())
        return QString();
    return QStringLiteral("%1:%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
}

void VerifyReader::addData(const QByteArray &chunk) {
    m_pending += chunk;
    qsizetype end;
    while ((end = m_pending.indexOf('\n')) >= 0) {
        readLine(QString::fromUtf8(m_pending.left(end)));
        m_pending.remove(0, end + 1);
    }
}

void VerifyReader::finish() {
    if (!m_pending.isEmpty())
        readLine(QString::fromUtf8(std::exchange(m_pending, QByteArray())));
    m_missing.clear();
}

QHash<QString, Verdict> VerifyReader::take() {
    return std::exchange(m_verdicts, {});
}

void VerifyReader::readLine(const QString &text) {
    static const QRegularExpression verdict(QStringLiteral(R"(^romset (\S+) (?:\[\S+\] )?is (good|bad|best available)$)"));
    static const QRegularExpression file(QStringLiteral(R"(^(\S+)\s*: (.+?)(?: \(\d+ bytes\))? - (.+)$)"));
    const QString line = text.trimmed();
    if (const QRegularExpressionMatch match = verdict.match(line); match.hasMatch()) {
        const QString set = match.captured(1).toLower();
        Verdict result;
        result.good = match.captured(2) != QLatin1String("bad");
        const QStringList missing = m_missing.take(set);
        if (!result.good)
            result.missing = capped(missing);
        m_verdicts.insert(set, result);
        return;
    }
    // A file it lacks or has wrong. One without a good dump known, or one
    // the set can do without, doesn't make it bad.
    if (const QRegularExpressionMatch match = file.match(line); match.hasMatch()) {
        const QString status = match.captured(3);
        if ((status.startsWith(QLatin1String("NOT FOUND")) || status.startsWith(QLatin1String("INCORRECT")))
            && !status.contains(QLatin1String("NO GOOD DUMP")) && !status.contains(QLatin1String("OPTIONAL")))
            m_missing[match.captured(1).toLower()].append(match.captured(2));
    }
}

Results load(const QString &path) {
    Results results;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return results;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(QStringLiteral("version")).toInt() != kVersion)
        return results;
    results.checked = QDateTime::fromString(json.value(QStringLiteral("checked")).toString(), Qt::ISODate);
    results.mame = json.value(QStringLiteral("mame")).toString();
    results.fbneo = json.value(QStringLiteral("fbneo")).toString();
    const QJsonObject sets = json.value(QStringLiteral("sets")).toObject();
    for (auto it = sets.constBegin(); it != sets.constEnd(); ++it) {
        const QJsonObject set = it.value().toObject();
        State state;
        state.mame = set.value(QStringLiteral("mame")).toInt();
        state.fbneo = set.value(QStringLiteral("fbneo")).toInt();
        for (const QJsonValue &name : set.value(QStringLiteral("mameMissing")).toArray())
            state.mameMissing.append(name.toString());
        for (const QJsonValue &name : set.value(QStringLiteral("fbneoMissing")).toArray())
            state.fbneoMissing.append(name.toString());
        const QJsonObject family = set.value(QStringLiteral("family")).toObject();
        for (auto f = family.constBegin(); f != family.constEnd(); ++f)
            state.family.insert(f.key(), f.value().toString());
        results.sets.insert(it.key(), state);
    }
    return results;
}

QByteArray toJson(const Results &results) {
    QStringList names = results.sets.keys();
    names.sort();
    QJsonObject sets;
    for (const QString &name : std::as_const(names)) {
        const State &state = results.sets[name];
        QJsonObject set;
        if (state.mame)
            set.insert(QStringLiteral("mame"), state.mame);
        if (state.fbneo)
            set.insert(QStringLiteral("fbneo"), state.fbneo);
        if (!state.mameMissing.isEmpty())
            set.insert(QStringLiteral("mameMissing"), QJsonArray::fromStringList(state.mameMissing));
        if (!state.fbneoMissing.isEmpty())
            set.insert(QStringLiteral("fbneoMissing"), QJsonArray::fromStringList(state.fbneoMissing));
        QJsonObject family;
        for (auto it = state.family.cbegin(); it != state.family.cend(); ++it)
            family.insert(it.key(), it.value());
        set.insert(QStringLiteral("family"), family);
        sets.insert(name, set);
    }
    const QJsonObject json{ { QStringLiteral("version"), kVersion },
                            { QStringLiteral("checked"), results.checked.toUTC().toString(Qt::ISODate) },
                            { QStringLiteral("mame"), results.mame },
                            { QStringLiteral("fbneo"), results.fbneo },
                            { QStringLiteral("sets"), sets } };
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

} // namespace romcheck

namespace {

// The archives' side of a check, off the app's thread: each set's family
// and its archives' stamps, and FinalBurn Neo's verdict on the sets it has.
void checkArchives(QPromise<QHash<QString, romcheck::State>> &promise, const RomCheck::Job &job) {
    const QHash<QString, romcheck::DatSet> dat = job.dat.isEmpty() ? QHash<QString, romcheck::DatSet>()
                                                                   : romcheck::readDat(job.dat);
    const QSet<QString> fbneo(job.fbneoSets.cbegin(), job.fbneoSets.cend());
    QStringList sets = job.mameSets + job.fbneoSets;
    sets.sort();
    sets.removeDuplicates();
    promise.setProgressRange(0, int(sets.size()));
    const auto archiveOf = [&job](const QString &name) { return job.archives.value(name); };
    QHash<QString, romcheck::Contents> cache;
    QHash<QString, romcheck::State> results;
    for (qsizetype i = 0; i < sets.size(); ++i) {
        if (promise.isCanceled())
            return;
        const QString &set = sets.at(i);
        romcheck::State state;
        // Its own archive, its parent's and those it takes ROMs from: the
        // result holds while they are as they were.
        QStringList family{ set };
        if (const QString parent = job.parents.value(set); !parent.isEmpty())
            family << parent;
        QString from = dat.value(set).romOf;
        for (int depth = 0; depth < 3 && !from.isEmpty() && !family.contains(from); ++depth) {
            family << from;
            from = dat.value(from).romOf;
        }
        for (const QString &name : std::as_const(family))
            state.family.insert(name, job.stamps.value(name));
        if (fbneo.contains(set) && dat.contains(set)) {
            bool unreadable = false;
            const QStringList missing = romcheck::missingRoms(set, dat, archiveOf, &cache, &unreadable);
            if (!unreadable) {
                state.fbneo = missing.isEmpty() ? 1 : -1;
                state.fbneoMissing = capped(missing);
            }
        }
        results.insert(set, state);
        promise.setProgressValue(int(i + 1));
    }
    promise.addResult(results);
}

} // namespace

RomCheck::RomCheck(QObject *parent) : QObject(parent) {
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kVerifyBatchMs);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        qWarning("[Arcade] Check ROMs: MAME took too long over a batch of games");
        if (m_process) {
            m_process->disconnect(this);
            m_process->kill();
            m_process->waitForFinished(1000);
            m_process->deleteLater();
            m_process = nullptr;
        }
        finish(QStringLiteral("MAME Took Too Long"));
    });
}

RomCheck::~RomCheck() {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (m_archives) {
        m_archives->cancel();
        m_archives->waitForFinished();
    }
}

void RomCheck::start(const Job &job, bool held) {
    if (m_active)
        return;
    m_job = job;
    m_active = true;
    m_held = held;
    m_result.clear();
    m_results = romcheck::Results();
    m_results.mame = job.mame.isEmpty() ? QString() : job.mameFrom;
    m_results.fbneo = job.dat.isEmpty() ? QString() : job.fbneoFrom;
    m_toVerify = job.mame.isEmpty() ? QStringList() : job.mameSets;
    m_batch.clear();
    m_answered.clear();
    m_stage = QStringLiteral("Archives");
    m_done = 0;
    m_total = 0;
    if (!m_archives) {
        m_archives = new QFutureWatcher<QHash<QString, romcheck::State>>(this);
        connect(m_archives, &QFutureWatcherBase::finished, this, &RomCheck::archivesRead);
        connect(m_archives, &QFutureWatcherBase::progressRangeChanged, this, [this](int, int most) {
            m_total = most;
            emit progressChanged();
        });
        connect(m_archives, &QFutureWatcherBase::progressValueChanged, this, [this](int value) {
            m_done = value;
            emit progressChanged();
        });
    }
    m_archives->setFuture(QtConcurrent::run(&checkArchives, job));
    emit progressChanged();
}

void RomCheck::archivesRead() {
    if (!m_active || m_archives->isCanceled() || m_archives->future().resultCount() == 0) {
        if (m_active)
            finish(QStringLiteral("Stopped"));
        return;
    }
    m_results.sets = m_archives->result();
    m_stage = QStringLiteral("MAME");
    m_done = 0;
    m_total = int(m_toVerify.size());
    if (m_toVerify.isEmpty()) {
        finish(QStringLiteral("Done"));
        return;
    }
    emit progressChanged();
    if (!m_held)
        verifyNext();
}

void RomCheck::pause() {
    if (!m_active || m_held)
        return;
    m_held = true;
    stopProcess();
    emit progressChanged();
}

void RomCheck::resume() {
    if (!m_active || !m_held)
        return;
    m_held = false;
    emit progressChanged();
    if (m_stage == QLatin1String("MAME") && !m_process)
        verifyNext();
}

void RomCheck::verifyNext() {
    m_batch = m_toVerify.mid(0, kVerifyBatch);
    m_toVerify.remove(0, m_batch.size());
    m_reader = romcheck::VerifyReader();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_job.workDir);
    // What it says on stderr is only of sets it has no machine of, which
    // its verdicts leave out too.
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &RomCheck::verifyRead);
    connect(m_process, &QProcess::finished, this, &RomCheck::verifyFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // finished() never comes for one that didn't start.
        if (error == QProcess::FailedToStart)
            verifyFinished(-1, QProcess::CrashExit);
    });
    m_process->start(m_job.mame, QStringList{ QStringLiteral("-rompath"), m_job.rompath, QStringLiteral("-verifyroms") }
                                     + m_batch);
    m_watchdog->start();
}

void RomCheck::verifyRead() {
    if (!m_process)
        return;
    m_reader.addData(m_process->readAllStandardOutput());
    const QHash<QString, romcheck::Verdict> verdicts = m_reader.take();
    for (auto it = verdicts.cbegin(); it != verdicts.cend(); ++it) {
        if (!m_batch.contains(it.key()) || m_answered.contains(it.key()))
            continue;
        romcheck::State &state = m_results.sets[it.key()];
        state.mame = it->good ? 1 : -1;
        state.mameMissing = it->missing;
        m_answered.insert(it.key());
        ++m_done;
    }
    if (!verdicts.isEmpty())
        emit progressChanged();
}

void RomCheck::verifyFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_process)
        return;
    m_watchdog->stop();
    m_reader.addData(m_process->readAllStandardOutput());
    m_reader.finish();
    verifyRead();
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    // All good (0), some not (2, missing files), or none found at all (5,
    // no such system): an answer. Anything else isn't one.
    if (status != QProcess::NormalExit || (exitCode != 0 && exitCode != 2 && exitCode != 5)) {
        qWarning("[Arcade] Check ROMs: MAME couldn't check the games (exit %d)", exitCode);
        finish(QStringLiteral("MAME Could Not Check"));
        return;
    }
    // A set it says nothing of is one it found none of the files of.
    for (const QString &set : std::as_const(m_batch)) {
        if (m_answered.contains(set))
            continue;
        m_results.sets[set].mame = -1;
        m_answered.insert(set);
        ++m_done;
    }
    emit progressChanged();
    if (m_toVerify.isEmpty())
        finish(QStringLiteral("Done"));
    else if (!m_held)
        verifyNext();
}

void RomCheck::stopProcess() {
    if (!m_process)
        return;
    m_watchdog->stop();
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(1000);
    m_process->deleteLater();
    m_process = nullptr;
    // The sets of the batch it hadn't answered for are asked again.
    QStringList again;
    for (const QString &set : std::as_const(m_batch)) {
        if (!m_answered.contains(set))
            again << set;
    }
    m_batch.clear();
    m_toVerify = again + m_toVerify;
}

void RomCheck::finish(const QString &result) {
    m_active = false;
    m_held = false;
    m_result = result;
    m_results.checked = QDateTime::currentDateTimeUtc();
    emit progressChanged();
    emit done(m_results);
}

QVariantMap RomCheck::progress() const {
    return { { QStringLiteral("active"), m_active }, { QStringLiteral("stage"), m_stage },
             { QStringLiteral("done"), m_done }, { QStringLiteral("total"), m_total },
             { QStringLiteral("held"), m_held }, { QStringLiteral("result"), m_result } };
}
