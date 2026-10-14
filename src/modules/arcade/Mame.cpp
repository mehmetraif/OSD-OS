#include "Mame.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace mame {

namespace {

// How far into the game the credits go in (coinScript()): past the start-up
// checks most boards run (Galaga's RAM and ROM test, the Neo-Geo BIOS's), in
// the game's own time, so a game MAME warns about first waits for it.
constexpr int kCoinDelaySeconds = 5;

} // namespace

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
    put("sourcefile", sourceFile);
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
    m.sourceFile = map.value(QStringLiteral("sourcefile")).toString();
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
                    m_machine.sourceFile = attrs.value(QLatin1String("sourcefile")).toString();
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

QString titleOf(const QString &description) {
    QString title = description;
    // "Galaga (Namco rev. B)": the game, without which version of it.
    const int paren = title.indexOf(QLatin1String(" ("));
    if (paren > 0)
        title.truncate(paren);
    // "Truxton / Tatsujin": the first of the names it went by.
    const int slash = title.indexOf(QLatin1String(" / "));
    if (slash > 0)
        title.truncate(slash);
    title = title.trimmed();
    return title.isEmpty() ? description.trimmed() : title;
}

QString systemOf(const QString &sourceFile) {
    // The boards most of MAME's games are on, as their makers named them.
    static const QHash<QString, QString> kBoards = {
        // Capcom, and Sony's ZN boards (Capcom's, Taito's and others')
        { QStringLiteral("cps1"), QStringLiteral("CPS-1") },
        { QStringLiteral("cps2"), QStringLiteral("CPS-2") },
        { QStringLiteral("cps3"), QStringLiteral("CPS-3") },
        { QStringLiteral("zn"), QStringLiteral("ZN") },
        // SNK
        { QStringLiteral("neogeo"), QStringLiteral("Neo-Geo") },
        { QStringLiteral("hng64"), QStringLiteral("Hyper Neo-Geo 64") },
        // Sega
        { QStringLiteral("naomi"), QStringLiteral("NAOMI") },
        { QStringLiteral("dc_atomiswave"), QStringLiteral("Atomiswave") },
        { QStringLiteral("segasp"), QStringLiteral("Sega SP") },
        { QStringLiteral("chihiro"), QStringLiteral("Chihiro") },
        { QStringLiteral("triforce"), QStringLiteral("Triforce") },
        { QStringLiteral("lindbergh"), QStringLiteral("Lindbergh") },
        { QStringLiteral("stv"), QStringLiteral("ST-V") },
        { QStringLiteral("model1"), QStringLiteral("Model 1") },
        { QStringLiteral("model2"), QStringLiteral("Model 2") },
        { QStringLiteral("model3"), QStringLiteral("Model 3") },
        { QStringLiteral("system1"), QStringLiteral("Sega System 1") },
        { QStringLiteral("system16"), QStringLiteral("Sega System 16") },
        { QStringLiteral("segas16a"), QStringLiteral("Sega System 16A") },
        { QStringLiteral("segas16b"), QStringLiteral("Sega System 16B") },
        { QStringLiteral("segas18"), QStringLiteral("Sega System 18") },
        { QStringLiteral("segas24"), QStringLiteral("Sega System 24") },
        { QStringLiteral("segas32"), QStringLiteral("Sega System 32") },
        { QStringLiteral("segac2"), QStringLiteral("Sega System C-2") },
        { QStringLiteral("segae"), QStringLiteral("Sega System E") },
        { QStringLiteral("segaxbd"), QStringLiteral("Sega X Board") },
        { QStringLiteral("segaybd"), QStringLiteral("Sega Y Board") },
        { QStringLiteral("segaorun"), QStringLiteral("Sega Out Run") },
        { QStringLiteral("segahang"), QStringLiteral("Sega Hang-On") },
        { QStringLiteral("megatech"), QStringLiteral("Mega-Tech") },
        { QStringLiteral("megaplay"), QStringLiteral("Mega Play") },
        // Namco
        { QStringLiteral("namcos1"), QStringLiteral("Namco System 1") },
        { QStringLiteral("namcos2"), QStringLiteral("Namco System 2") },
        { QStringLiteral("namcos10"), QStringLiteral("Namco System 10") },
        { QStringLiteral("namcos11"), QStringLiteral("Namco System 11") },
        { QStringLiteral("namcos12"), QStringLiteral("Namco System 12") },
        { QStringLiteral("namcos21"), QStringLiteral("Namco System 21") },
        { QStringLiteral("namcos22"), QStringLiteral("Namco System 22") },
        { QStringLiteral("namcos23"), QStringLiteral("Namco System 23") },
        { QStringLiteral("namcos86"), QStringLiteral("Namco System 86") },
        { QStringLiteral("namconb1"), QStringLiteral("Namco NB-1") },
        { QStringLiteral("namcona1"), QStringLiteral("Namco NA-1") },
        { QStringLiteral("namcops2"), QStringLiteral("Namco System 246") },
        // Konami
        { QStringLiteral("konamigx"), QStringLiteral("Konami GX") },
        { QStringLiteral("ksys573"), QStringLiteral("Konami System 573") },
        { QStringLiteral("djmain"), QStringLiteral("DJ Main") },
        { QStringLiteral("firebeat"), QStringLiteral("Firebeat") },
        { QStringLiteral("viper"), QStringLiteral("Konami Viper") },
        { QStringLiteral("twinkle"), QStringLiteral("Twinkle") },
        { QStringLiteral("hornet"), QStringLiteral("Konami Hornet") },
        { QStringLiteral("kpython2"), QStringLiteral("Konami Python 2") },
        // Taito
        { QStringLiteral("taito_b"), QStringLiteral("Taito B System") },
        { QStringLiteral("taito_f2"), QStringLiteral("Taito F2 System") },
        { QStringLiteral("taito_f3"), QStringLiteral("Taito F3 System") },
        { QStringLiteral("taito_h"), QStringLiteral("Taito H System") },
        { QStringLiteral("taito_l"), QStringLiteral("Taito L System") },
        { QStringLiteral("taito_x"), QStringLiteral("Taito X System") },
        { QStringLiteral("taito_z"), QStringLiteral("Taito Z System") },
        { QStringLiteral("taitojc"), QStringLiteral("Taito JC System") },
        { QStringLiteral("taitogn"), QStringLiteral("Taito G-NET") },
        { QStringLiteral("taitotz"), QStringLiteral("Taito Type-Zero") },
        { QStringLiteral("taitotx"), QStringLiteral("Taito Type X") },
        // Irem
        { QStringLiteral("m62"), QStringLiteral("Irem M62") },
        { QStringLiteral("m72"), QStringLiteral("Irem M72") },
        { QStringLiteral("m90"), QStringLiteral("Irem M90") },
        { QStringLiteral("m92"), QStringLiteral("Irem M92") },
        { QStringLiteral("m107"), QStringLiteral("Irem M107") },
        // Midway and Atari
        { QStringLiteral("midtunit"), QStringLiteral("Midway T-Unit") },
        { QStringLiteral("midwunit"), QStringLiteral("Midway Wolf Unit") },
        { QStringLiteral("midyunit"), QStringLiteral("Midway Y-Unit") },
        { QStringLiteral("midxunit"), QStringLiteral("Midway X-Unit") },
        { QStringLiteral("midvunit"), QStringLiteral("Midway V-Unit") },
        { QStringLiteral("mcr"), QStringLiteral("Midway MCR") },
        { QStringLiteral("mcr3"), QStringLiteral("Midway MCR-III") },
        { QStringLiteral("mcr68"), QStringLiteral("Midway MCR-68k") },
        { QStringLiteral("seattle"), QStringLiteral("Midway Seattle") },
        { QStringLiteral("vegas"), QStringLiteral("Midway Vegas") },
        { QStringLiteral("atarisy1"), QStringLiteral("Atari System 1") },
        { QStringLiteral("atarisy2"), QStringLiteral("Atari System 2") },
        { QStringLiteral("atarig1"), QStringLiteral("Atari G1") },
        { QStringLiteral("atarig42"), QStringLiteral("Atari G42") },
        { QStringLiteral("atarigt"), QStringLiteral("Atari GT") },
        { QStringLiteral("atarigx2"), QStringLiteral("Atari GX2") },
        // Nintendo
        { QStringLiteral("playch10"), QStringLiteral("PlayChoice-10") },
        { QStringLiteral("vsnes"), QStringLiteral("Vs. System") },
        { QStringLiteral("nss"), QStringLiteral("Super System") },
        // The rest
        { QStringLiteral("toaplan1"), QStringLiteral("Toaplan 1") },
        { QStringLiteral("toaplan2"), QStringLiteral("Toaplan 2") },
        { QStringLiteral("cave"), QStringLiteral("Cave 68000") },
        { QStringLiteral("cv1k"), QStringLiteral("Cave CV1000") },
        { QStringLiteral("psikyo"), QStringLiteral("Psikyo") },
        { QStringLiteral("psikyosh"), QStringLiteral("Psikyo SH-2") },
        { QStringLiteral("psikyo4"), QStringLiteral("Psikyo PS4") },
        { QStringLiteral("pgm"), QStringLiteral("PGM") },
        { QStringLiteral("pgm2"), QStringLiteral("PGM2") },
        { QStringLiteral("kaneko16"), QStringLiteral("Kaneko 16") },
        { QStringLiteral("suprnova"), QStringLiteral("Super Kaneko Nova") },
        { QStringLiteral("megasys1"), QStringLiteral("Mega System 1") },
        { QStringLiteral("ms32"), QStringLiteral("Mega System 32") },
        { QStringLiteral("seta"), QStringLiteral("Seta") },
        { QStringLiteral("seta2"), QStringLiteral("Seta 2") },
        { QStringLiteral("ssv"), QStringLiteral("Seta SSV") },
        { QStringLiteral("decocass"), QStringLiteral("DECO Cassette") },
        { QStringLiteral("deco32"), QStringLiteral("DECO 32") },
        { QStringLiteral("nmk16"), QStringLiteral("NMK16") },
    };
    // "capcom/cps1.cpp", or "cps1.cpp" from a MAME before its drivers went
    // in folders by maker: "cps1".
    const QString driver = sourceFile.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0).toLower();
    if (driver.isEmpty())
        return QString();
    return kBoards.value(driver, driver.toUpper());
}

QHash<QString, QString> readGenres(const QString &path) {
    QHash<QString, QString> genres;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return genres;
    // [Category]'s lines only: the file has a section for the version each
    // game came in as well.
    bool categories = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            categories = line.compare(QLatin1String("[Category]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (!categories || equals <= 0 || line.startsWith(QLatin1Char(';')))
            continue;
        // "Fighter / Versus * Mature *": the genre before the kind of it.
        QString genre = line.mid(equals + 1);
        const int slash = genre.indexOf(QLatin1Char('/'));
        if (slash >= 0)
            genre.truncate(slash);
        genre.remove(QLatin1String("* Mature *"));
        genre = genre.trimmed();
        if (!genre.isEmpty())
            genres.insert(line.left(equals).trimmed().toLower(), genre);
    }
    return genres;
}

QString coinScript(int coins) {
    // Each coin held down a tenth of a second, a fifth between them, as a
    // coin mech's switch would be: games read it once a frame, and some count
    // a coin only once it has been there a few frames. Timed by the game's
    // own clock, which stands still while MAME shows a warning first. MAME
    // runs the script again at every reset of the game (F3, a watchdog),
    // keeping its globals: the credits go in once.
    return QStringLiteral(
               "-- Written by OSD/OS as the game starts: the credits bought on its\n"
               "-- Arcade screen, player 1's coin pressed once for each.\n"
               "if osdos_coins_run then return end\n"
               "osdos_coins_run = true\n"
               "local left = %1\n"
               "local nextAt, hold, gap = %2, 0.1, 0.2\n"
               "local coin = manager.machine.ioport:token_to_input_type(\"COIN1\")\n"
               "local field\n"
               "for _, port in pairs(manager.machine.ioport.ports) do\n"
               "  for _, f in pairs(port.fields) do\n"
               "    if f.type == coin then field = f end\n"
               "  end\n"
               "end\n"
               "if field and left > 0 then\n"
               "  local down = false\n"
               "  local pressing\n"
               "  pressing = emu.add_machine_frame_notifier(function()\n"
               "    local now = manager.machine.time:as_double()\n"
               "    if now < nextAt then return end\n"
               "    if down then\n"
               "      field:clear_value()\n"
               "      left = left - 1\n"
               "      if left <= 0 then pressing:unsubscribe() end\n"
               "      nextAt = now + gap\n"
               "    else\n"
               "      field:set_value(1)\n"
               "      nextAt = now + hold\n"
               "    end\n"
               "    down = not down\n"
               "  end)\n"
               "end\n")
        .arg(coins).arg(kCoinDelaySeconds);
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
