#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <optional>

class QProcess;

// A Raspberry Pi to JAMMA board's pins as they are pressed, for the JAMMA
// sticks' button test (Settings → Controls → OSD/OS JAMMA P1 → Test Buttons):
// osdos-jamma --test, run beside the service, prints each pin of the board's
// two expanders that changes, with what the mapping makes of it. It sets them
// up as the service does and plays nothing, so the sticks go on as they were.
// A button the sticks never show (on a pin the mapping makes an input SDL
// doesn't map, or none) shows here too, and so does one wired to the other
// player's expander.
class JammaPins : public QObject {
    Q_OBJECT
public:
    // A line osdos-jamma --test prints: "P1 0x20 B1 pressed  tl2", the
    // player, the expander, the pin, pressed or released, and the input the
    // mapping makes of it. Nothing for any other line.
    struct Change {
        int player = 0;
        QString pin;      // "A0".."B7"
        bool pressed = false;
        QString input;    // "a", "tl2", "none"...
    };
    static std::optional<Change> parse(const QByteArray &line);

    explicit JammaPins(QObject *parent = nullptr);
    ~JammaPins() override;

    // osdos-jamma: the one in the app's folder, or a stand-in's.
    void setProgram(const QString &program) { m_program = program; }
    // Both players' pins as they are pressed (pinPressed()), or no more.
    void watch(bool on);

signals:
    void pinPressed(int player, const QString &pin, const QString &input);
    // They can't be read: osdos-jamma is missing, or it ended (what it said
    // last, "no MCP23017 at 0x20 or 0x21 on /dev/i2c-0: nothing to read").
    void failed(const QString &reason);

private:
    void readOutput();
    void ended();
    void stopProcess();

    QString m_program;
    QProcess *m_process = nullptr;
    QByteArray m_partial;   // a line not ended yet
};
