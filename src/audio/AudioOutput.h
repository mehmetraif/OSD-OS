#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class AppCore;
class QProcessEnvironment;

// Settings → Audio Output: the sound card OSD/OS plays through, among those
// ALSA has (/proc/asound): the Pi's AV jack, its HDMI, a USB sound card.
// ALSA's own default is left as it is; each player is told the card as it
// starts: mpv (a video, Ambient Mode's and Weather's music) with
// --audio-device, any other program (a web player's browser, a script) with
// ALSA_CARD in its environment, and a video playing behind the menus moves to
// a new one at once (MpvController::followAudioOutput). A card chosen but
// unplugged is passed over until it is back: sound goes where it would
// without the setting.
//
// Offered on Linux where nothing between the players and ALSA picks the card
// itself, no PipeWire or PulseAudio server: the OS image. OSDOS_ASOUND_DIR
// stands in for /proc/asound, for tests.
class AudioOutput : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit AudioOutput(AppCore *appCore, QObject *parent = nullptr);

    bool available() const;

    // Settings' row, { options, values, value, note }: Auto, then each card
    // that can play now by its name, and the chosen one marked when it is
    // unplugged. A value is the setting as saved (app.audio_output): "" for
    // Auto, else { card, name }, the card's ALSA id and its name then. note,
    // for the row's help, without a USB sound card among those that can play:
    // why, as far as the system can tell ("" with one): no USB sound device
    // plugged in, or one plugged in that ALSA made no card of; and the Pi's
    // power having dropped too low, which can keep USB devices from working.
    // OSDOS_USB_DEVICES_DIR, OSDOS_PLATFORM_DIR and OSDOS_HWMON_DIR stand in
    // for /sys/bus/usb/devices, /sys/devices/platform and /sys/class/hwmon,
    // for tests.
    Q_INVOKABLE QVariantMap settingRow() const;

    // The chosen card's ALSA id while it is plugged in, else "".
    static QString card();
    // mpv's --audio-device for it ("alsa/default:CARD=<id>"), else "".
    static QString mpvDevice();
    // The options an mpv about to start takes for it: none without one.
    static QStringList mpvArgs();
    // For another program about to start: ALSA_CARD names the card.
    static void applyTo(QProcessEnvironment &env);
    // Whether there is a card that can play now, as far as ALSA says: false
    // only where ALSA is all there is and it lists none that can (the Pi 5's
    // HDMI counts only while that port shows the picture). A program that
    // can't open one may not start at all (MAME), and is better told to play
    // no sound.
    static bool hasPlaybackCard();

signals:
    // Settings → Audio Output was saved (another card, or the same again).
    void cardChanged();

private:
    void load();

    AppCore *m_appCore;
};
