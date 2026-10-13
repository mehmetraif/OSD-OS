#include "AudioOutput.h"
#include "../AppCore.h"
#include "../util/LegacyNames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

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

// The cards that can play (with a playback device, pcm*p), in ALSA's order,
// each by its name in Settings. /proc/asound/cards has two lines a card:
//    0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones
//                         bcm2835 Headphones
QList<Card> playbackCards() {
    QFile f(asoundDir() + QStringLiteral("/cards"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression line(
        QStringLiteral("^\\s*(\\d+)\\s+\\[(\\S+)\\s*\\]:\\s*\\S*\\s+-\\s+(.*)$"));
    struct Found {
        QString id;
        QString shortName;
        int hdmi;
    };
    QList<Found> found;
    int hdmiCards = 0;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &text : lines) {
        const QRegularExpressionMatch m = line.match(text);
        if (!m.hasMatch())
            continue;
        const QDir card(asoundDir() + QStringLiteral("/card") + m.captured(1));
        if (card.entryList({ QStringLiteral("pcm*p") }, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
            continue;
        const QString shortName = m.captured(3).trimmed();
        const int hdmi = hdmiPort(m.captured(2), shortName);
        if (hdmi >= 0)
            ++hdmiCards;
        found.append({ m.captured(2), shortName, hdmi });
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

QString describe() {
    if (g_card.isEmpty())
        return QStringLiteral("ALSA's default card");
    if (AudioOutput::card().isEmpty())
        return QStringLiteral("ALSA's default card, %1 being unplugged").arg(g_card);
    return g_card;
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
    QStringList options { QStringLiteral("Auto") };
    QVariantList values { QString() };
    QString value = options.first();
    bool plugged = false;
    for (const Card &c : playbackCards()) {
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
             { QStringLiteral("value"), value } };
}

QString AudioOutput::card() {
    if (!g_available || g_card.isEmpty())
        return QString();
    for (const Card &c : playbackCards()) {
        if (c.id == g_card)
            return g_card;
    }
    return QString();
}

QString AudioOutput::mpvDevice() {
    const QString id = card();
    // ALSA's default device on that card: the card's own where ALSA has one
    // (a USB card's mixes what plays at once, the Pi's HDMI wants its
    // samples framed as IEC958), else the card's first device, converting.
    return id.isEmpty() ? QString() : QStringLiteral("alsa/default:CARD=%1").arg(id);
}

QStringList AudioOutput::mpvArgs() {
    const QString device = mpvDevice();
    if (device.isEmpty())
        return {};
    return { QStringLiteral("--audio-device=%1").arg(device) };
}

bool AudioOutput::hasPlaybackCard() {
#ifdef Q_OS_LINUX
    if (!QFileInfo::exists(asoundDir() + QStringLiteral("/cards")) || soundServer())
        return true;
    return !playbackCards().isEmpty();
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
