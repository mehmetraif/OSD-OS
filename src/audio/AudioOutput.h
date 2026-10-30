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
// --audio-device, every program OSD/OS starts (a web player's browser, a
// script, MAME, RetroArch, those mpvs) with ALSA_CARD in its environment, and
// a video playing behind the menus moves to a new one at once
// (MpvController::followAudioOutput). A card chosen but unplugged is passed
// over until it is back: sound goes as on Auto, where ALSA's default plays,
// or, while that can't (a Pi 5's HDMI without the picture), the first card
// that can. The card given is at its full level (card()): the volume is
// what it plays into, a TV's or an amplifier's knob.
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

    // Settings' row, { options, values, value, note }: Auto (naming the card
    // it plays through while ALSA's default can't), then each card that can
    // play by its name, and the chosen one marked when it is unplugged. A
    // value is the setting as saved (app.audio_output): "" for
    // Auto, else { card, name }, the card's ALSA id and its name then. note,
    // for the row's help, without a USB sound card among those that can play:
    // why, as far as the system can tell ("" with one): no USB sound device
    // plugged in, or one plugged in that ALSA made no card of; and the Pi's
    // power having dropped too low, which can keep USB devices from working.
    // OSDOS_USB_DEVICES_DIR, OSDOS_PLATFORM_DIR and OSDOS_HWMON_DIR stand in
    // for /sys/bus/usb/devices, /sys/devices/platform and /sys/class/hwmon,
    // for tests.
    Q_INVOKABLE QVariantMap settingRow() const;

    // The card the players are given, by its ALSA id: the chosen one while it
    // is plugged in; else "" for ALSA's default, unless that can't play now
    // and nothing outside OSD/OS sets it (/etc/asound.conf, ~/.asoundrc,
    // ALSA_CARD): then the first card that can, a USB sound card beside a
    // Pi 5 whose picture goes out of a JAMMA board say. "" with none. The
    // card is put at its full level as it is given, through amixer: its
    // playback controls below 0 dB at 0 dB (their top where that is lower,
    // 100% where they give no dB), and on; never lowered, an input's left
    // as they are. OSDOS_AMIXER stands in for amixer, for tests.
    static QString card();
    // mpv's --audio-device for it ("alsa/default:CARD=<id>"), else "".
    static QString mpvDevice();
    // The options an mpv about to start takes: a card chosen, none on Auto,
    // whose card a process has through ALSA_CARD (applyTo()), so that its
    // mpv.conf's audio-device still applies. inApp, for libmpv, which reads
    // neither: Auto's card too.
    static QStringList mpvArgs(bool inApp = false);
    // For a program about to start: ALSA_CARD names the card.
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
