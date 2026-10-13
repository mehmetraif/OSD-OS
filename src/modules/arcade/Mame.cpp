#include "Mame.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace mame {

QString programPath(const QString &dataRoot) {
    const QFileInfo dropIn(dataRoot + QStringLiteral("/bin/mame"));
    if (dropIn.isFile() && dropIn.isExecutable())
        return dropIn.absoluteFilePath();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("mame"));
    if (!onPath.isEmpty())
        return onPath;
    const QFileInfo debian(QStringLiteral("/usr/games/mame"));
    if (debian.isFile() && debian.isExecutable())
        return debian.absoluteFilePath();
    return QString();
}

QString pluginsFolder(const QString &program) {
    QStringList candidates;
    if (!program.isEmpty()) {
        const QFileInfo info(program);
        for (const QString &dir : { info.absolutePath(), QFileInfo(info.canonicalFilePath()).absolutePath() }) {
            candidates << dir + QStringLiteral("/../share/games/mame/plugins")
                       << dir + QStringLiteral("/../share/mame/plugins")
                       << dir + QStringLiteral("/plugins");
        }
    }
    candidates << QStringLiteral("/usr/share/games/mame/plugins")
               << QStringLiteral("/usr/local/share/games/mame/plugins");
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate + QStringLiteral("/hiscore/init.lua")))
            return QDir::cleanPath(candidate);
    }
    return QString();
}

QVariantMap Machine::toVariant() const {
    QVariantMap map{ { QStringLiteral("description"), description } };
    // Only what a machine has, to keep the cache short.
    const auto put = [&map](const char *key, const QString &value) {
        if (!value.isEmpty())
            map.insert(QLatin1String(key), value);
    };
    put("year", year);
    put("manufacturer", manufacturer);
    put("cloneof", cloneOf);
    put("status", status);
    if (players > 0)
        map.insert(QStringLiteral("players"), players);
    if (rotate != 0)
        map.insert(QStringLiteral("rotate"), rotate);
    if (bios)
        map.insert(QStringLiteral("bios"), true);
    if (device)
        map.insert(QStringLiteral("device"), true);
    if (!runnable)
        map.insert(QStringLiteral("runnable"), false);
    return map;
}

Machine Machine::fromVariant(const QString &name, const QVariantMap &map) {
    Machine m;
    m.name = name;
    m.description = map.value(QStringLiteral("description")).toString();
    m.year = map.value(QStringLiteral("year")).toString();
    m.manufacturer = map.value(QStringLiteral("manufacturer")).toString();
    m.cloneOf = map.value(QStringLiteral("cloneof")).toString();
    m.status = map.value(QStringLiteral("status")).toString();
    m.players = map.value(QStringLiteral("players")).toInt();
    m.rotate = map.value(QStringLiteral("rotate")).toInt();
    m.bios = map.value(QStringLiteral("bios")).toBool();
    m.device = map.value(QStringLiteral("device")).toBool();
    m.runnable = map.value(QStringLiteral("runnable"), true).toBool();
    return m;
}

void ListXmlReader::reset() {
    m_xml.clear();
    m_done.clear();
    m_machine = Machine();
    m_inMachine = false;
    m_depth = 0;
    m_hadDisplay = false;
    m_field.clear();
    m_text.clear();
    m_build.clear();
}

void ListXmlReader::addData(const QByteArray &chunk) {
    m_xml.addData(chunk);
    read();
}

QList<Machine> ListXmlReader::take() {
    QList<Machine> done;
    done.swap(m_done);
    return done;
}

bool ListXmlReader::failed() const {
    return m_xml.hasError() && m_xml.error() != QXmlStreamReader::PrematureEndOfDocumentError;
}

void ListXmlReader::read() {
    static const QSet<QString> kText = { QStringLiteral("description"), QStringLiteral("year"),
                                         QStringLiteral("manufacturer") };
    while (!m_xml.atEnd()) {
        const QXmlStreamReader::TokenType token = m_xml.readNext();
        if (token == QXmlStreamReader::Invalid)
            break;   // the end of what has come so far, or not XML
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = m_xml.name();
            const QXmlStreamAttributes attrs = m_xml.attributes();
            if (!m_inMachine) {
                if (name == QLatin1String("mame")) {
                    m_build = attrs.value(QLatin1String("build")).toString();
                } else if (name == QLatin1String("machine")) {
                    m_inMachine = true;
                    m_depth = 0;
                    m_hadDisplay = false;
                    m_machine = Machine();
                    m_machine.name = attrs.value(QLatin1String("name")).toString();
                    m_machine.cloneOf = attrs.value(QLatin1String("cloneof")).toString();
                    m_machine.bios = attrs.value(QLatin1String("isbios")) == QLatin1String("yes");
                    m_machine.device = attrs.value(QLatin1String("isdevice")) == QLatin1String("yes");
                    m_machine.runnable = attrs.value(QLatin1String("runnable")) != QLatin1String("no");
                    m_field.clear();
                }
                continue;
            }
            ++m_depth;
            if (m_depth != 1)
                continue;
            const QString element = name.toString();
            if (kText.contains(element)) {
                m_field = element;
                m_text.clear();
            } else if (element == QLatin1String("display")) {
                // The first screen's, for a game with several.
                if (!m_hadDisplay)
                    m_machine.rotate = attrs.value(QLatin1String("rotate")).toInt();
                m_hadDisplay = true;
            } else if (element == QLatin1String("input")) {
                m_machine.players = attrs.value(QLatin1String("players")).toInt();
            } else if (element == QLatin1String("driver")) {
                m_machine.status = attrs.value(QLatin1String("status")).toString();
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (m_inMachine && m_depth == 1 && !m_field.isEmpty())
                m_text += m_xml.text();
        } else if (token == QXmlStreamReader::EndElement) {
            if (!m_inMachine)
                continue;
            if (m_depth == 0) {
                // </machine>
                m_inMachine = false;
                if (!m_machine.name.isEmpty())
                    m_done.append(m_machine);
                continue;
            }
            if (m_depth == 1 && !m_field.isEmpty()) {
                const QString value = m_text.trimmed();
                if (m_field == QLatin1String("description"))
                    m_machine.description = value;
                else if (m_field == QLatin1String("year"))
                    m_machine.year = value;
                else
                    m_machine.manufacturer = value;
                m_field.clear();
                m_text.clear();
            }
            --m_depth;
        }
    }
}

bool isShortName(const QString &name) {
    // By hand rather than a regular expression: it is asked of every set in
    // the folder, tens of thousands in a full set.
    if (name.isEmpty() || name.size() > 40)
        return false;
    for (const QChar c : name) {
        const char16_t u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '_'))
            return false;
    }
    return true;
}

QString makerOf(const QString &manufacturer) {
    QString maker = manufacturer.trimmed();
    // "Namco (Midway license)": the maker, without whom it was made for.
    const int paren = maker.indexOf(QLatin1String(" ("));
    if (paren > 0)
        maker.truncate(paren);
    // "Capcom / Mitchell": the first of those who made it together.
    const int slash = maker.indexOf(QLatin1Char('/'));
    if (slash > 0)
        maker.truncate(slash);
    maker = maker.trimmed();
    // MAME's "<unknown>", and the odd game's "unknown".
    if (maker.startsWith(QLatin1Char('<')) || maker.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0)
        return QString();
    return maker;
}

QString decadeOf(const QString &year) {
    // "1981", "198?", and "1982?" where MAME isn't sure of the year.
    static const QRegularExpression decade(QStringLiteral("^(\\d{3})[\\d?]\\??$"));
    const QRegularExpressionMatch m = decade.match(year.trimmed());
    return m.hasMatch() ? m.captured(1) + QStringLiteral("0s") : QString();
}

QString controllerConfig(const QStringList &playerGuids) {
    QString maps;
    QSet<QString> mapped;
    for (int p = 0; p < playerGuids.size(); ++p) {
        const QString guid = playerGuids.at(p).trimmed();
        // MAME keeps one line per device: a second player on a pad of the
        // same model goes where MAME's order puts it.
        if (guid.isEmpty() || mapped.contains(guid))
            continue;
        mapped.insert(guid);
        maps += QStringLiteral("            <mapdevice device=\"%1\" controller=\"JOYCODE_%2\" />\n")
                    .arg(guid.toHtmlEscaped()).arg(p + 1);
    }
    return QStringLiteral(
               "<?xml version=\"1.0\"?>\n"
               "<!-- Written by OSD/OS as each game starts: the players of Settings -> Controls,\n"
               "     and Coin and Start together on player 1's controller, or Esc, to leave the game. -->\n"
               "<mameconfig version=\"10\">\n"
               "    <system name=\"default\">\n"
               "        <input>\n"
               "%1"
               "            <port type=\"UI_CANCEL\">\n"
               "                <newseq type=\"standard\">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START</newseq>\n"
               "            </port>\n"
               "        </input>\n"
               "    </system>\n"
               "</mameconfig>\n")
        .arg(maps);
}

Missing missingFiles(const QString &output) {
    // "pacman.6e NOT FOUND (tried in pacman puckman)": not an optional file,
    // nor one with no good dump known, which MAME goes on without.
    static const QRegularExpression notFound(
        QStringLiteral("^(\\S+) NOT FOUND(?: \\(tried in ([^)]*)\\))?\\s*$"));
    Missing missing;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = notFound.match(line.trimmed());
        if (!m.hasMatch())
            continue;
        if (!missing.files.contains(m.captured(1)))
            missing.files << m.captured(1);
        const QStringList sets = m.captured(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &set : sets) {
            if (!missing.sets.contains(set))
                missing.sets << set;
        }
    }
    return missing;
}

QString lastError(const QString &output) {
    // MAME's own lines: not those of the libraries it uses, ALSA's above all,
    // which complain at every start on a system with no sound card.
    QStringList lines;
    const QStringList all = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : all) {
        const QString text = line.trimmed();
        if (!text.isEmpty() && !text.startsWith(QLatin1String("ALSA lib ")))
            lines << text;
    }
    // MAME ends on what stopped it: the last of its lines that says it is an
    // error, among the last few, else its last line.
    for (int i = lines.size() - 1; i >= 0 && i >= lines.size() - 5; --i) {
        if (lines.at(i).contains(QLatin1String("error"), Qt::CaseInsensitive))
            return lines.at(i);
    }
    return lines.isEmpty() ? QString() : lines.last();
}

} // namespace mame
