#pragma once
#include <QByteArray>
#include <QString>

#include <functional>

class QObject;

// A request to one of the OSD/OS image's root helpers: osdos-stop
// (scripts/install.sh) behind a systemd socket only the app's user may write
// to, osdos-<stem>.socket, one unit a request. A line goes out, a line comes
// back: "ok", or why not. Display Output's At Next Start ("display"), Wi-Fi's
// country ("wifi") and the device's name ("name") go this way.
namespace rootrequest {

struct Answer {
    bool ok = false;
    // The helper's reason; without its answer, what went wrong on the way
    // ("osdos-<stem>.socket isn't listening", "no answer", "no answer in
    // time").
    QString why;
    // The line went out, and whether one came back: a request sent and left
    // unanswered may yet have been done.
    bool sent = false;
    bool answered = false;
};

// Where the helper's socket is: /run/osdos-<stem>.sock, or in OSDOS_RUN_DIR
// for tests.
QString socketPath(const QString &stem);
// Whether it can be asked: a launcher of launcherApi or later
// (OSDOS_LAUNCHER_API), its socket there.
bool available(const QString &stem, int launcherApi);

// Sends line to the helper and calls done once, with its answer or why there
// is none: on its line, its socket closing, an error, or once timeoutMs is up.
// The socket and its connections are owner's, so none calls back once owner
// is gone.
void ask(QObject *owner, const QString &stem, const QByteArray &line, int timeoutMs,
         std::function<void(const Answer &)> done);

} // namespace rootrequest
