#include "AudioOutput.h"
#include "../AppCore.h"
#include "../util/LegacyNames.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

namespace {

// Set by AudioOutput: whether the setting is offered, and the card chosen,
// by its ALSA id with the name it had then (for while it is unplugged); ""
// for Auto.
bool g_available = false;
QString g_card;
QString g_name;

QString asoundDir() {
    return legacy::env("ASOUND_DIR", QStringLiteral("/proc/asound"));
}

#ifdef Q_OS_LINUX
// A sound server (PipeWire, PulseAudio) picks the card for what plays
// through it, so the setting would do nothing there.
bool soundServer() {
    if (qEnvironmentVariableIsSet("PULSE_SERVER"))
        return true;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return !runtime.isEmpty()
        && (QFileInfo::exists(runtime + QStringLiteral("/pipewire-0"))
            || QFileInfo::exists(runtime + QStringLiteral("/pulse/native")));
}
#endif

// The HDMI port a card plays to, the board's HDMI0 or HDMI1, else -1: under
// KMS vc4hdmi0 and vc4hdmi1, under the firmware's display driver (the Pi 4's
// fkms) "bcm2835 HDMI 1" and "bcm2835 HDMI 2".
int hdmiPort(const QString &id, const QString &shortName) {
    static const QRegularExpression kms(QStringLiteral("^vc4hdmi(\\d)$"));
    static const QRegularExpression firmware(QStringLiteral("^bcm2835 HDMI (\\d)$"));
    QRegularExpressionMatch m = kms.match(id);
    if (m.hasMatch())
        return m.captured(1).toInt();
    m = firmware.match(shortName);
    if (m.hasMatch())
        return m.captured(1).toInt() - 1;
    return -1;
}

struct Card {
    QString id;
    QString label;
};

// A card as /proc/asound/cards has it, on two lines:
//    0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones
//                         bcm2835 Headphones
struct AlsaCard {
    int index = -1;           // its number: card0 is ALSA's default
    QString id;
    QString driver;           // "USB-Audio" for a USB sound card
    QString shortName;
    bool playback = false;    // it has a playback device (pcm*p)
};

QList<AlsaCard> alsaCards() {
    QFile f(asoundDir() + QStringLiteral("/cards"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression line(
        QStringLiteral("^\\s*(\\d+)\\s+\\[(\\S+)\\s*\\]:\\s*(\\S*)\\s+-\\s+(.*)$"));
    QList<AlsaCard> cards;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &text : lines) {
        const QRegularExpressionMatch m = line.match(text);
        if (!m.hasMatch())
            continue;
        const QDir card(asoundDir() + QStringLiteral("/card") + m.captured(1));
        const bool playback = !card.entryList({ QStringLiteral("pcm*p") }, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
        cards.append({ m.captured(1).toInt(), m.captured(2), m.captured(3), m.captured(4).trimmed(), playback });
    }
    return cards;
}

// The text of a small file of the kernel's (sysfs), trimmed; "" for none.
QString readSmall(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.read(256)).trimmed();
}

#ifdef Q_OS_LINUX
// Whether a card can play now. The Pi 5's HDMI (KMS's vc4hdmi0 and
// vc4hdmi1) only while its port shows the picture: vc4-hdmi refuses to be
// opened (error 524) while that HDMI is off, as it is when the picture goes
// out through a JAMMA board, composite or the other port. DRM calls the
// board's HDMI0 and HDMI1 HDMI-A-1 and HDMI-A-2, each "enabled" while it
// has a picture; a port DRM doesn't list is taken to play, as is any other
// card.
bool playsNow(const AlsaCard &card) {
    if (!card.playback)
        return false;
    const int port = hdmiPort(card.id, card.shortName);
    if (port < 0 || !card.id.startsWith(QLatin1String("vc4hdmi")))
        return true;
    const QDir drm(legacy::env("DRM_DIR", QStringLiteral("/sys/class/drm")));
    const QStringList connectors = drm.entryList({ QStringLiteral("card*-HDMI-A-%1").arg(port + 1) },
                                                 QDir::Dirs | QDir::NoDotAndDotDot);
    return connectors.isEmpty()
        || std::any_of(connectors.cbegin(), connectors.cend(), [&drm](const QString &connector) {
               return readSmall(drm.filePath(connector + QStringLiteral("/enabled"))) == QLatin1String("enabled");
           });
}

// Whether ALSA's default card is set outside OSD/OS: ALSA_CARD (or
// ALSA_PCM_CARD) in OSD/OS's own environment, or an ALSA configuration, the
// system's (/etc/asound.conf, where INSTALL.md sets it) or the user's
// (~/.asoundrc, ~/.config/alsa/asoundrc). OSDOS_ALSA_CONFIG stands in for
// those files, for tests.
bool defaultSetElsewhere() {
    if (qEnvironmentVariableIsSet("ALSA_CARD") || qEnvironmentVariableIsSet("ALSA_PCM_CARD"))
        return true;
    if (legacy::envIsSet("ALSA_CONFIG"))
        return QFileInfo::exists(legacy::env("ALSA_CONFIG"));
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    const QStringList files { QStringLiteral("/etc/asound.conf"), QDir::homePath() + QStringLiteral("/.asoundrc"),
                              config + QStringLiteral("/alsa/asoundrc") };
    return std::any_of(files.cbegin(), files.cend(), [](const QString &file) { return QFileInfo::exists(file); });
}

// On Auto, the card the players are given in place of ALSA's default where
// that can't play now: its first card, card0, unless it is set elsewhere
// (defaultSetElsewhere()). A Pi 5's HDMI0 can't while the picture goes out
// of HDMI1, a JAMMA board or composite: the first card that can plays, the
// picture's HDMI or a USB sound card, rather than none. "" while ALSA's
// default plays, and with no card that can.
QString autoCard(const QList<AlsaCard> &cards) {
    const auto first = std::find_if(cards.cbegin(), cards.cend(), [](const AlsaCard &card) { return card.index == 0; });
    if ((first != cards.cend() && playsNow(*first)) || defaultSetElsewhere())
        return QString();
    const auto plays = std::find_if(cards.cbegin(), cards.cend(), playsNow);
    return plays == cards.cend() ? QString() : plays->id;
}
#else
QString autoCard(const QList<AlsaCard> &) {
    return QString();
}
#endif

// The USB devices plugged in that have a sound interface (class 01), by
// their product names, as the kernel lists them, whether ALSA made a card of
// them or not; and whether a driver (the kernel's snd-usb-audio) took one of
// their sound interfaces, which it does by itself when it is there.
struct UsbSoundDevice {
    QString name;
    bool driven = false;
};
QList<UsbSoundDevice> usbSoundDevices() {
    const QDir devices(legacy::env("USB_DEVICES_DIR", QStringLiteral("/sys/bus/usb/devices")));
    QList<UsbSoundDevice> found;
    // A device's interfaces are "<device>:<configuration>.<interface>"; a
    // driver bound to one is its "driver" link.
    for (const QString &entry : devices.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.contains(QLatin1Char(':'))
            || readSmall(devices.filePath(entry + QStringLiteral("/bInterfaceClass"))) != QLatin1String("01"))
            continue;
        const QString device = entry.section(QLatin1Char(':'), 0, 0);
        const QString product = readSmall(devices.filePath(device + QStringLiteral("/product")));
        const QString name = product.isEmpty() ? device : product;
        const bool driven = QFileInfo(devices.filePath(entry + QStringLiteral("/driver"))).isSymLink()
                            || QFileInfo::exists(devices.filePath(entry + QStringLiteral("/driver")));
        auto it = std::find_if(found.begin(), found.end(), [&name](const UsbSoundDevice &d) { return d.name == name; });
        if (it == found.end())
            found.append({ name, driven });
        else
            it->driven = it->driven || driven;
    }
    return found;
}

// The USB devices plugged in whose product names say they are for sound
// ("USB Audio"), whatever interfaces they show. Asked when none shows a sound
// interface: some adapters show their buttons' (HID) alone while nothing is
// in their jack, Realtek's USB Audio (0bda:4ef0) among them.
QStringList usbSoundNames() {
    const QDir devices(legacy::env("USB_DEVICES_DIR", QStringLiteral("/sys/bus/usb/devices")));
    QStringList names;
    for (const QString &device : devices.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (device.contains(QLatin1Char(':')))
            continue;
        const QString product = readSmall(devices.filePath(device + QStringLiteral("/product")));
        if ((product.contains(QLatin1String("audio"), Qt::CaseInsensitive)
             || product.contains(QLatin1String("sound"), Qt::CaseInsensitive))
            && !names.contains(product))
            names.append(product);
    }
    return names;
}

// Whether the Pi's power is too low, which can keep USB devices from coming
// up, as a sentence: since it started (its firmware's get_throttled, bit 16,
// or bit 0 for now), or, where the kernel no longer has that file (6.12 on),
// now (the rpi_volt hwmon's alarm, which says only what its last look saw).
// "" for neither, and where there is no such file (not a Pi).
QString underVoltage() {
    const QDir platform(legacy::env("PLATFORM_DIR", QStringLiteral("/sys/devices/platform")));
    for (const QString &soc : platform.entryList({ QStringLiteral("soc*") }, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir socDir(platform.filePath(soc));
        for (const QString &fw : socDir.entryList({ QStringLiteral("*firmware") }, QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok = false;
            const uint flags = readSmall(socDir.filePath(fw + QStringLiteral("/get_throttled"))).toUInt(&ok, 16);
            if (ok)
                return flags & 0x10001 ? QStringLiteral("The Pi's power has dropped too low since it started") : QString();
        }
    }
    const QDir hwmon(legacy::env("HWMON_DIR", QStringLiteral("/sys/class/hwmon")));
    for (const QString &entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (readSmall(hwmon.filePath(entry + QStringLiteral("/name"))) == QLatin1String("rpi_volt"))
            return readSmall(hwmon.filePath(entry + QStringLiteral("/in0_lcrit_alarm"))) == QLatin1String("1")
                       ? QStringLiteral("The Pi's power is too low") : QString();
    }
    return QString();
}

// The cards that can play, in ALSA's order, each by its name in Settings.
QList<Card> playbackCards(const QList<AlsaCard> &all) {
    struct Found {
        QString id;
        QString shortName;
        int hdmi;
    };
    QList<Found> found;
    int hdmiCards = 0;
    for (const AlsaCard &card : all) {
        if (!card.playback)
            continue;
        const int hdmi = hdmiPort(card.id, card.shortName);
        if (hdmi >= 0)
            ++hdmiCards;
        found.append({ card.id, card.shortName, hdmi });
    }

    QList<Card> cards;
    QStringList labels;
    for (const Found &c : found) {
        QString label;
        if (c.id == QLatin1String("Headphones") && c.shortName.startsWith(QLatin1String("bcm2835")))
            label = QStringLiteral("AV Jack");
        else if (c.hdmi >= 0)
            label = hdmiCards > 1 ? QStringLiteral("HDMI %1").arg(c.hdmi) : QStringLiteral("HDMI");
        else
            label = c.shortName.isEmpty() ? c.id : c.shortName;
        // Two of the same make: told apart by their ids.
        if (labels.contains(label))
            label += QStringLiteral(" (%1)").arg(c.id);
        labels.append(label);
        cards.append({ c.id, label });
    }
    return cards;
}

// Without a USB sound card among those that can play: why, as far as the
// system can tell; "" with one.
QString usbNote(const QList<AlsaCard> &cards) {
    const bool usbCard = std::any_of(cards.cbegin(), cards.cend(), [](const AlsaCard &card) {
        return card.playback && card.driver == QLatin1String("USB-Audio");
    });
    if (usbCard)
        return QString();
    QString note;
    // ALSA has a USB card, with nothing to play through: an input alone.
    const auto inputOnly = std::find_if(cards.cbegin(), cards.cend(), [](const AlsaCard &card) {
        return !card.playback && card.driver == QLatin1String("USB-Audio");
    });
    if (inputOnly != cards.cend()) {
        note = (inputOnly->shortName.isEmpty() ? inputOnly->id : inputOnly->shortName)
               + QStringLiteral(" is plugged in, but it has no output to play through");
    } else {
        const QList<UsbSoundDevice> usb = usbSoundDevices();
        QStringList names, driven;
        for (const UsbSoundDevice &d : usb) {
            names.append(d.name);
            if (d.driven)
                driven.append(d.name);
        }
        const QStringList silent = usb.isEmpty() ? usbSoundNames() : QStringList();
        if (!silent.isEmpty())
            note = silent.join(QStringLiteral(", "))
                   + QStringLiteral(" is plugged in, but shows no sound interface: some adapters show one only"
                                    " when something is in their jack as they are plugged in");
        else if (usb.isEmpty())
            note = QStringLiteral("No USB sound card is plugged in, as far as the system can tell");
        else if (driven.isEmpty())
            note = names.join(QStringLiteral(", "))
                   + QStringLiteral(" is plugged in, but the USB sound driver (snd-usb-audio) didn't take it");
        else
            note = driven.join(QStringLiteral(", "))
                   + QStringLiteral(" is plugged in and the USB sound driver took it, but ALSA made no card of it to play through");
    }
    const QString low = underVoltage();
    if (!low.isEmpty())
        note += QStringLiteral(". ") + low + QStringLiteral(", which can keep USB devices from working");
    qInfo("[AudioOutput] %s", qPrintable(note));
    return note;
}

// mpv's device for a card: ALSA's default device on it, the card's own where
// ALSA has one (a USB card's mixes what plays at once, the Pi's HDMI wants
// its samples framed as IEC958), else the card's first device, converting.
QString deviceOf(const QString &id) {
    return QStringLiteral("alsa/default:CARD=%1").arg(id);
}

// amixer, which sets a card's levels (keepFull()). OSDOS_AMIXER stands in for
// it in tests; without it a test's cards (OSDOS_ASOUND_DIR) get none, as
// they are not the system's.
QString amixer() {
    if (legacy::envIsSet("ASOUND_DIR"))
        return legacy::env("AMIXER");
    return legacy::env("AMIXER", QStringLiteral("amixer"));
}

// amixer on a card ("" for ALSA's default), for a second at most: whether it
// did what it was asked, and what it said.
bool runAmixer(const QString &program, const QString &id, const QStringList &args, QByteArray *output = nullptr) {
    QProcess process;
    process.start(program, (id.isEmpty() ? QStringList() : QStringList { QStringLiteral("-c"), id }) + args);
    if (!process.waitForFinished(1000)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// A card's levels raised, through amixer (keepFull()): on a thread of its
// own, as amixer reads alsa-lib's whole configuration and every control of
// the card (over USB for a USB card), tens of milliseconds, and a second and
// a half when the card doesn't answer, which held the GUI thread at every
// video's start.
void raiseLevels(const QString &program, const QString &id) {
    QByteArray contents;
    if (!runAmixer(program, id, { QStringLiteral("scontents") }, &contents))
        return;
    // amixer's words: a control's line, what it has (p for playback, c for
    // capture, volume and switch without either for both), its playback's
    // range, and each channel's level, raw, in percent and in dB where it
    // gives dB, then on or off.
    static const QRegularExpression head(QStringLiteral("^Simple mixer control '([^']*)',(\\d+)$"),
                                         QRegularExpression::MultilineOption);
    static const QRegularExpression capabilities(QStringLiteral("^\\s*Capabilities:(.*)$"),
                                                 QRegularExpression::MultilineOption);
    static const QRegularExpression limits(QStringLiteral("^\\s*Limits: Playback (-?\\d+) - (-?\\d+)"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression level(QStringLiteral("Playback (-?\\d+) \\[(\\d+)%\\](?: \\[(-?\\d+\\.\\d+)dB\\])?"));
    static const QRegularExpression input(
        QStringLiteral("\\b(mic|line|capture|input|loopback|sidetone|monitor|aux|cd|phone|video|beep)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(contents);
    QList<QRegularExpressionMatch> heads;
    for (QRegularExpressionMatchIterator it = head.globalMatch(text); it.hasNext();)
        heads.append(it.next());
    QStringList raised;
    for (int i = 0; i < heads.size(); ++i) {
        const QRegularExpressionMatch &h = heads.at(i);
        const int end = i + 1 < heads.size() ? heads.at(i + 1).capturedStart() : text.size();
        const QString block = text.mid(h.capturedEnd(), end - h.capturedEnd());
        if (input.match(h.captured(1)).hasMatch())
            continue;
        const QStringList has = capabilities.match(block).captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool volume = has.contains(QLatin1String("pvolume"));
        if ((!volume && !has.contains(QLatin1String("pswitch"))) || has.contains(QLatin1String("cvolume"))
            || has.contains(QLatin1String("cswitch")) || has.contains(QLatin1String("volume"))
            || has.contains(QLatin1String("switch")))
            continue;
        QStringList set;
        if (volume) {
            const QRegularExpressionMatch range = limits.match(block);
            bool low = false, dB = false;
            for (QRegularExpressionMatchIterator it = level.globalMatch(block); it.hasNext();) {
                const QRegularExpressionMatch channel = it.next();
                if (!channel.captured(3).isEmpty()) {
                    dB = true;
                    const bool belowTop = !range.hasMatch()
                                          || channel.captured(1).toLongLong() < range.captured(2).toLongLong();
                    low = low || (channel.captured(3).toDouble() < 0 && belowTop);
                } else {
                    low = low || channel.captured(2).toInt() < 100;
                }
            }
            if (low)
                set.append(dB ? QStringLiteral("0dB") : QStringLiteral("100%"));
        }
        if (block.contains(QLatin1String("[off]")))
            set.append(QStringLiteral("unmute"));
        if (set.isEmpty())
            continue;
        const QString control = QStringLiteral("'%1',%2").arg(h.captured(1), h.captured(2));
        runAmixer(program, id, QStringList { QStringLiteral("-q"), QStringLiteral("sset"), control } + set);
        raised.append(control);
    }
    if (!raised.isEmpty())
        qInfo("[AudioOutput] %s at full level: %s", qPrintable(id.isEmpty() ? QStringLiteral("ALSA's default card") : id),
              qPrintable(raised.join(QStringLiteral(", "))));
}

// A card at its full level as it is given to a player ("" for ALSA's default
// card): the volume is what it plays into, a TV's, or an amplifier's knob (a
// JAMMA board's). Each of its controls with a playback volume below 0 dB
// goes to 0 dB, its top where that is lower, or one that gives no dB to
// 100%; one switched off is switched on. Never lower: one set above 0 dB
// (the AV jack goes to +4 dB, which can clip) stays so. A control of an
// input is left as it is, its capture's or its playback's, which plays what
// comes in (a microphone's, a line in's). Once for the players given it
// together (an mpv process asks mpvArgs() and applyTo()), and off the GUI
// thread (raiseLevels()): the player starts meanwhile, and plays into the
// levels as they rise.
void keepFull(const QString &id) {
    static QString last;
    static QElapsedTimer since;
    if (since.isValid() && id == last && since.elapsed() < 2000)
        return;
    last = id;
    since.start();
    const QString program = amixer();
    if (program.isEmpty())
        return;
    QThread *thread = QThread::create([program, id] { raiseLevels(program, id); });
    thread->setObjectName(QStringLiteral("amixer"));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QString describe() {
    const QString used = AudioOutput::card();
    if (!g_card.isEmpty() && used == g_card)
        return g_card;
    QString text = used.isEmpty() ? QStringLiteral("ALSA's default card")
                                  : QStringLiteral("%1, as ALSA's default card can't play now").arg(used);
    if (!g_card.isEmpty())
        text += QStringLiteral(", %1 being unplugged").arg(g_card);
    return text;
}

} // namespace

AudioOutput::AudioOutput(AppCore *appCore, QObject *parent)
    : QObject(parent), m_appCore(appCore) {
#ifdef Q_OS_LINUX
    g_available = QFileInfo::exists(asoundDir() + QStringLiteral("/cards")) && !soundServer();
#endif
    if (!g_available)
        return;
    load();
    qInfo("[AudioOutput] Sound through %s", qPrintable(describe()));
    connect(m_appCore, &AppCore::appSettingChanged, this, [this](const QString &key) {
        if (key != QLatin1String("audio_output"))
            return;
        load();
        qInfo("[AudioOutput] Sound through %s", qPrintable(describe()));
        emit cardChanged();
    });
}

bool AudioOutput::available() const {
    return g_available;
}

void AudioOutput::load() {
    const QVariantMap chosen = m_appCore->get_setting(QString(), QStringLiteral("audio_output")).toMap();
    g_card = chosen.value(QStringLiteral("card")).toString();
    g_name = chosen.value(QStringLiteral("name")).toString();
}

QVariantMap AudioOutput::settingRow() const {
    const QList<AlsaCard> all = alsaCards();
    const QList<Card> cards = playbackCards(all);
    // Auto names the card it plays through while ALSA's default can't.
    const QString autoId = autoCard(all);
    const auto autoPlays = std::find_if(cards.cbegin(), cards.cend(), [&autoId](const Card &c) { return c.id == autoId; });
    QStringList options { autoId.isEmpty() || autoPlays == cards.cend() ? QStringLiteral("Auto")
                                                                         : QStringLiteral("Auto (%1)").arg(autoPlays->label) };
    QVariantList values { QString() };
    QString value = options.first();
    bool plugged = false;
    for (const Card &c : cards) {
        options.append(c.label);
        values.append(QVariantMap { { QStringLiteral("card"), c.id }, { QStringLiteral("name"), c.label } });
        if (c.id == g_card) {
            value = c.label;
            plugged = true;
        }
    }
    // Chosen but unplugged: it stays chosen, and comes back into use once it
    // is plugged in again, until another is chosen.
    if (!g_card.isEmpty() && !plugged) {
        value = (g_name.isEmpty() ? g_card : g_name) + QStringLiteral(" (Unplugged)");
        options.append(value);
        values.append(QVariantMap { { QStringLiteral("card"), g_card }, { QStringLiteral("name"), g_name } });
    }
    return { { QStringLiteral("options"), options },
             { QStringLiteral("values"), values },
             { QStringLiteral("value"), value },
             { QStringLiteral("note"), usbNote(all) } };
}

QString AudioOutput::card() {
    if (!g_available)
        return QString();
    const QList<AlsaCard> all = alsaCards();
    const QList<Card> cards = playbackCards(all);
    const bool plugged = !g_card.isEmpty()
                         && std::any_of(cards.cbegin(), cards.cend(), [](const Card &c) { return c.id == g_card; });
    // Auto, or the card chosen unplugged.
    const QString id = plugged ? g_card : autoCard(all);
    keepFull(id);
    return id;
}

QString AudioOutput::mpvDevice() {
    const QString id = card();
    return id.isEmpty() ? QString() : deviceOf(id);
}

QStringList AudioOutput::mpvArgs(bool inApp) {
    const QString id = card();
    // Auto's card reaches a process as ALSA's default (applyTo()), so that an
    // audio-device in its mpv.conf still has the last word: only a card
    // chosen goes on its command line. libmpv reads neither.
    if (id.isEmpty() || (!inApp && id != g_card))
        return {};
    return { QStringLiteral("--audio-device=") + deviceOf(id) };
}

bool AudioOutput::hasPlaybackCard() {
#ifdef Q_OS_LINUX
    if (!QFileInfo::exists(asoundDir() + QStringLiteral("/cards")) || soundServer())
        return true;
    const QList<AlsaCard> cards = alsaCards();
    return std::any_of(cards.cbegin(), cards.cend(), playsNow);
#else
    return true;
#endif
}

void AudioOutput::applyTo(QProcessEnvironment &env) {
    // ALSA's default device ("default") is then on that card, as above.
    const QString id = card();
    if (!id.isEmpty())
        env.insert(QStringLiteral("ALSA_CARD"), id);
}
