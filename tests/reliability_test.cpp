#include "AppCore.h"
#include "audio/AudioOutput.h"
#include "modules/playlists/ServerDownload.h"
#include "modules/weather/WeatherBackend.h"
#include "update/UpdateManager.h"
#include "util/AtomicFile.h"
#include "util/DurableFile.h"
#include "util/AsyncDirectoryCache.h"
#include "util/LogFile.h"
#include "util/SetupFile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QDir>
#include <QSemaphore>
#include <QThreadPool>
#include <QtTest>
#include <cstring>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <csignal>
#endif

class Reply : public QNetworkReply {
public:
    explicit Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly);
    }
    void abort() override {}
    void complete(QByteArray body) { m_body = body; setFinished(true); emit readyRead(); emit finished(); }
    qint64 bytesAvailable() const override { return m_body.size() + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const qint64 size = qMin(max, qint64(m_body.size()));
        if (!size) return -1;
        std::memcpy(data, m_body.constData(), size); m_body.remove(0, size); return size;
    }
private:
    QByteArray m_body;
};
class Network : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QList<Reply *> replies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        auto *reply = new Reply(request, this); replies << reply; return reply;
    }
};

class ReliabilityTest : public QObject {
    Q_OBJECT
    static QByteArray contents(const QString &path) { QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll(); }
private slots:
    void staleGeocodesCannotTouchReplacementList() {
        QTemporaryDir tmp;
        WeatherBackend backend(tmp.path(), tmp.path());
        delete backend.m_nam;
        auto *network = new Network(&backend); backend.m_nam = network;
        backend.resolveOthers({"old one", "old two"});
        Reply *first = network->replies.at(0), *second = network->replies.at(1);
        backend.resolveOthers({"new"});
        Reply *current = network->replies.at(2);
        const QByteArray body = R"({"results":[{"name":"Newest","latitude":1,"longitude":2}]})";
        second->complete(body); // old index 1 does not exist in the new list
        first->complete("{\"results\":[]}");
        QCOMPARE(backend.m_pendingOthers, 1);
        QVERIFY(backend.m_otherPoints.at(0).toMap().isEmpty());
        current->complete(body);
        QCOMPARE(backend.m_pendingOthers, 0);
        QCOMPARE(backend.m_otherPoints.size(), 1);
        QCOMPARE(backend.m_otherPoints.first().toMap().value("name").toString(), QString("NEWEST"));
    }

    void markerFailureDoesNotArmUpdate() {
        QTemporaryDir tmp;
        UpdateManager manager(tmp.path(), tmp.path());
        QDir().mkpath(tmp.path() + "/updates/staged.json"); // deterministic open failure
        QVERIFY(!manager.writeStagedMarkers("abcd"));
        QVERIFY(QDir().rmdir(tmp.path() + "/updates/staged.json"));
        QVERIFY(manager.writeStagedMarkers("abcd"));
        manager.m_assetName = "update.tar.gz";
        QVERIFY(manager.writeStagedMarkers("abcd"));
        QDir().mkpath(tmp.path() + "/updates/staged.sha256");
        manager.applyLinux();
        QCOMPARE(manager.state(), QString("error"));
    }

    // Only a newer release is offered: not the release before a prerelease
    // running (which GitHub's "latest" passes over), nor the same one.
    void updateOffersOnlyNewerReleases() {
        QVERIFY(UpdateManager::isNewer("v0.1.0", "v0.0.9"));
        QVERIFY(UpdateManager::isNewer("v0.0.10", "v0.0.9"));
        QVERIFY(UpdateManager::isNewer("v0.1.0", "v0.1.0-rc.1"));
        QVERIFY(UpdateManager::isNewer("v0.1.0-rc.2", "v0.1.0-rc.1"));
        QVERIFY(UpdateManager::isNewer("v0.1.0-rc.10", "v0.1.0-rc.2"));
        QVERIFY(UpdateManager::isNewer("v0.1.0-rc.1", "v0.1.0-beta.3"));
        // Tags without a dot (v1.0.0-rc1), as BUILDING.md writes them.
        QVERIFY(UpdateManager::isNewer("v1.0.0-rc10", "v1.0.0-rc9"));
        QVERIFY(!UpdateManager::isNewer("v1.0.0-rc9", "v1.0.0-rc10"));
        QVERIFY(UpdateManager::isNewer("v1.0.0-rc1", "v1.0.0-beta12"));
        QVERIFY(!UpdateManager::isNewer("v1.0.0-rc2", "v1.0.0-rc2"));
        QVERIFY(!UpdateManager::isNewer("v0.0.9", "v0.1.0-rc.1"));
        QVERIFY(!UpdateManager::isNewer("v0.1.0-rc.1", "v0.1.0"));
        QVERIFY(!UpdateManager::isNewer("v0.0.9", "v0.0.9"));
        QVERIFY(!UpdateManager::isNewer("nightly", "v0.0.9"));
        QVERIFY(UpdateManager::isNewer("v0.0.9", "dev"));
        QTemporaryDir tmp;
        UpdateManager manager(tmp.path(), tmp.path());
        const QString running = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion("v0.1.0-rc.1");
        manager.handleReleaseInfo(R"({"tag_name": "v0.0.9", "assets": []})");
        QCOMPARE(manager.state(), QString("upToDate"));
        QCoreApplication::setApplicationVersion(running);
    }

    // The package for this platform is picked by its name's ending, with
    // the SHA-256 GitHub keeps for it; a release without one is checked by
    // its size alone.
    void updateTakesTheAssetsDigest() {
        QCOMPARE(UpdateManager::assetDigest("sha256:" + QString(64, 'a')), QString(64, 'a'));
        QCOMPARE(UpdateManager::assetDigest("sha256:" + QString(64, 'A')), QString(64, 'a'));
        QCOMPARE(UpdateManager::assetDigest("sha256:" + QString(63, 'a')), QString());
        QCOMPARE(UpdateManager::assetDigest("sha256:" + QString(64, 'g')), QString());
        QCOMPARE(UpdateManager::assetDigest("sha512:" + QString(64, 'a')), QString());
        QCOMPARE(UpdateManager::assetDigest(QString(64, 'a')), QString());
        QCOMPARE(UpdateManager::assetDigest(QString()), QString());
        QTemporaryDir tmp;
        UpdateManager manager(tmp.path(), tmp.path());
        const QString running = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion("v0.0.9");
        const QString sha = QString(64, 'b');
        // A release as GitHub's API gives it: each package with its digest,
        // or without one.
        const auto release = [](const QString &digest) {
            QJsonArray assets;
            const auto asset = [&assets, &digest](const QString &name, int size, bool digested) {
                QJsonObject file{ { "name", name }, { "browser_download_url", "https://x/" + name }, { "size", size } };
                if (digested)
                    file.insert("digest", digest);
                assets.append(file);
            };
            asset("install.sh", 10, true);
            asset("OSD-OS-v0.1.0-linux-arm64.tar.gz", 20, !digest.isEmpty());
            asset("OSD-OS-v0.1.0-macOS-arm64.dmg", 30, !digest.isEmpty());
            asset("OSD-OS-linux-x86_64.AppImage", 40, !digest.isEmpty());
            asset("OSD-OS-v0.1.0-windows-x64.zip", 50, !digest.isEmpty());
            return QJsonDocument(QJsonObject{ { "tag_name", "v0.1.0" }, { "assets", assets } }).toJson();
        };
        manager.handleReleaseInfo(release("sha256:" + sha));
#if defined(Q_OS_WIN) || (defined(Q_OS_BSD4) && !defined(Q_OS_DARWIN))
        // Windows' zip is named for a download by hand, not taken; the BSDs
        // have no package among a release's files, and the newer version is
        // named.
        QCOMPARE(manager.state(), QString("error"));
        QVERIFY2(manager.errorMessage().contains("v0.1.0"), qPrintable(manager.errorMessage()));
#if defined(Q_OS_WIN)
        QVERIFY2(manager.errorMessage().contains("OSD-OS-v0.1.0-windows-x64.zip"), qPrintable(manager.errorMessage()));
#endif
        QVERIFY(manager.m_assetName.isEmpty());
#else
        QCOMPARE(manager.state(), QString("updateAvailable"));
        QVERIFY2(manager.m_assetName.startsWith("OSD-OS-"), qPrintable(manager.m_assetName));
        QCOMPARE(manager.m_expectedSha256, sha);
        manager.handleReleaseInfo(release(QString()));
        QCOMPARE(manager.state(), QString("updateAvailable"));
        QCOMPARE(manager.m_expectedSha256, QString());
#endif
        QCoreApplication::setApplicationVersion(running);
    }

    // The log the Windows build keeps in the data folder (util/LogFile): what
    // was logged before the folder was known, then the rest, each line on
    // disk as it comes; the run before's kept beside it.
    void logFileKeepsTheRunBefore() {
        QTemporaryDir tmp;
        const auto read = [&tmp](const QString &name) {
            QFile file(tmp.path() + QLatin1Char('/') + name);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        logfile::begin();
        qInfo("[test] before the folder was known");
        logfile::open(tmp.path());
        qInfo("[test] the first run");
        const QByteArray first = read(QStringLiteral("osdos.log"));
        QVERIFY2(first.contains("[test] before the folder was known"), first.constData());
        QVERIFY2(first.contains("[test] the first run"), first.constData());
        QVERIFY(first.indexOf("before the folder") < first.indexOf("the first run"));
        logfile::end();

        logfile::begin();
        logfile::open(tmp.path());
        qInfo("[test] the second run");
        logfile::end();
        const QByteArray second = read(QStringLiteral("osdos.log"));
        QVERIFY2(second.contains("[test] the second run"), second.constData());
        QVERIFY(!second.contains("[test] the first run"));
        QVERIFY(read(QStringLiteral("osdos-previous.log")).contains("[test] the first run"));

        // After end(), the test's own handler again: nothing more written.
        qInfo("[test] after the end");
        QVERIFY(!read(QStringLiteral("osdos.log")).contains("after the end"));
    }

    void durableWriteErrorsAreReported() {
#ifdef Q_OS_UNIX
        QTemporaryDir tmp;
        QVERIFY(!syncPath(tmp.path() + "/missing").isEmpty());
#ifdef Q_OS_LINUX
        // Linux's fsync refuses a device node (EINVAL); FreeBSD's and
        // OpenBSD's take one as synced.
        QVERIFY(!syncPath("/dev/null").isEmpty());
#endif
        const QString path = tmp.path() + "/ok";
        QVERIFY(writeFileAtomically(path, "ok"));
        QVERIFY(syncPath(path).isEmpty());
        QVERIFY(syncPath(tmp.path(), true).isEmpty());
#endif
    }

    void directoryReadDoesNotBlockAndIgnoresStaleResult() {
        AsyncDirectoryCache cache(this);
        QSemaphore started, release;
        QStringList ready;
        cache.ready = [&](const QString &key) { ready << key; };
        QVERIFY(!cache.fetch("old", [&]() { started.release(); release.acquire(); return QVariantList{"old"}; }).isValid());
        // A main-loop callback releases the worker; synchronous I/O would deadlock.
        QVERIFY(started.tryAcquire(1, 3000));
        cache.clear();
        QVERIFY(!cache.fetch("new", []() { return QVariantList{"new"}; }).isValid());
        release.release();
        QTRY_COMPARE(ready, QStringList{"new"});
        QCOMPARE(cache.fetch("new", []() { return QVariantList(); }).toList(), QVariantList{"new"});
        QThreadPool::globalInstance()->waitForDone();
    }

    void serverDownload_data() {
        QTest::addColumn<bool>("failWrite");
        QTest::newRow("replace-complete-file") << false;
#ifdef Q_OS_UNIX
        QTest::newRow("failed-write-preserves-old-file") << true;
#endif
    }
    void destroyingDownloadOwnerDoesNotDestroyRunningThread() {
        QTemporaryDir tmp;
        const QString base = tmp.path() + "/movie";
        QVERIFY(writeFileAtomically(base + ".mp4", "old"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto *owner = new QObject;
        ServerDownload::start(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/file").arg(server.serverPort()))), base, owner);
        QTRY_VERIFY(server.hasPendingConnections());
        QScopedPointer<QTcpSocket> socket(server.nextPendingConnection());
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: 1000000\r\n\r\npartial");
        socket->flush();
        QElapsedTimer elapsed; elapsed.start();
        delete owner;
        QVERIFY(elapsed.elapsed() < 300);
        QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(contents(base + ".mp4"), QByteArray("old"));
    }
    void serverDownload() {
        QFETCH(bool, failWrite);
        QTemporaryDir tmp;
        const QString base = tmp.path() + "/movie", target = base + ".mp4";
        QVERIFY(writeFileAtomically(target, "previous-good-file"));
        const QByteArray payload(65536, 'x');
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, payload]() {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: "
                              + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
        });
#ifdef Q_OS_UNIX
        struct LimitGuard {
            rlimit previous{}; decltype(std::signal(SIGXFSZ, SIG_IGN)) handler = SIG_DFL;
            bool changed = false;
            ~LimitGuard() { if (changed) { setrlimit(RLIMIT_FSIZE, &previous); std::signal(SIGXFSZ, handler); } }
        } limit;
        if (failWrite) {
            QVERIFY(getrlimit(RLIMIT_FSIZE, &limit.previous) == 0);
            auto next = limit.previous; next.rlim_cur = 512;
            limit.handler = std::signal(SIGXFSZ, SIG_IGN);
            QVERIFY(setrlimit(RLIMIT_FSIZE, &next) == 0); limit.changed = true;
        }
#endif
        auto *download = ServerDownload::start(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/file").arg(server.serverPort()))), base, this);
        QSignalSpy finished(download, &ServerDownload::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(finished.first().at(0).toBool(), !failWrite);
        QCOMPARE(contents(target), failWrite ? QByteArray("previous-good-file") : payload);
        if (failWrite) QVERIFY(!finished.first().at(2).toString().isEmpty());
    }

    // Audio Output's note without a USB sound card to play through: nothing
    // plugged in, an adapter named for sound showing its buttons alone (HID),
    // as one does with nothing in its jack (a keyboard left out), a USB sound
    // device no driver took (by its product name, once for its two sound
    // interfaces, the keyboard left out), one the driver
    // took that ALSA made no card of, the Pi's power having dropped (the
    // firmware's flags, or the hwmon's alarm where the kernel has no such
    // file), a USB card with no output; none once ALSA has the card.
    void usbSoundCardNote() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString asound = dir.path() + "/asound", usb = dir.path() + "/usb", platform = dir.path() + "/platform",
                      hwmon = dir.path() + "/hwmon";
        const auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).path()); QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(asound + "/cards", " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
                                         "                      bcm2835 Headphones\n"));
        QVERIFY(QDir().mkpath(asound + "/card0/pcm0p") && QDir().mkpath(usb) && QDir().mkpath(platform));
        qputenv("OSDOS_ASOUND_DIR", asound.toUtf8());
        qputenv("OSDOS_USB_DEVICES_DIR", usb.toUtf8());
        qputenv("OSDOS_PLATFORM_DIR", platform.toUtf8());
        qputenv("OSDOS_HWMON_DIR", hwmon.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("OSDOS_ASOUND_DIR"); qunsetenv("OSDOS_USB_DEVICES_DIR"); qunsetenv("OSDOS_PLATFORM_DIR");
            qunsetenv("OSDOS_HWMON_DIR");
        });
        AppCore core(dir.path(), dir.path());
        AudioOutput audio(&core);
        const auto note = [](const AudioOutput &output) { return output.settingRow().value("note").toString(); };
        QCOMPARE(note(audio), QString("No USB sound card is plugged in, as far as the system can tell"));
        QVERIFY(write(usb + "/1-1.4/product", "Keyboard\n"));
        QVERIFY(write(usb + "/1-1.4:1.0/bInterfaceClass", "03\n"));
        QCOMPARE(note(audio), QString("No USB sound card is plugged in, as far as the system can tell"));
        QVERIFY(write(usb + "/1-2/product", "USB Audio\n"));
        QVERIFY(write(usb + "/1-2:1.0/bInterfaceClass", "03\n"));
        QCOMPARE(note(audio), QString("USB Audio is plugged in, but shows no sound interface: some adapters show"
                                      " one only when something is in their jack as they are plugged in"));
        QVERIFY(QDir(usb + "/1-2").removeRecursively() && QDir(usb + "/1-2:1.0").removeRecursively());

        QVERIFY(write(usb + "/1-1.3/product", "USB Audio Device\n"));
        QVERIFY(write(usb + "/1-1.3:1.0/bInterfaceClass", "01\n"));
        QVERIFY(write(usb + "/1-1.3:1.1/bInterfaceClass", "01\n"));
        const QString unmade = "USB Audio Device is plugged in, but the USB sound driver (snd-usb-audio) didn't take it";
        QCOMPARE(note(audio), unmade);
        // The driver bound to one of its sound interfaces (the kernel's link
        // to it), and still no card.
        QVERIFY(QDir().mkpath(dir.path() + "/snd-usb-audio"));
        QVERIFY(QFile::link(dir.path() + "/snd-usb-audio", usb + "/1-1.3:1.1/driver"));
        QCOMPARE(note(audio), QString("USB Audio Device is plugged in and the USB sound driver took it, "
                                      "but ALSA made no card of it to play through"));
        QVERIFY(QFile::remove(usb + "/1-1.3:1.1/driver"));
        QCOMPARE(note(audio), unmade);
        // Bit 16 of the firmware's get_throttled (hex, as the kernel writes
        // it): under-voltage since boot.
        QVERIFY(write(platform + "/soc/soc:firmware/get_throttled", "50000\n"));
        QCOMPARE(note(audio), unmade + ". The Pi's power has dropped too low since it started,"
                                           " which can keep USB devices from working");
        QVERIFY(write(platform + "/soc/soc:firmware/get_throttled", "0\n"));
        QCOMPARE(note(audio), unmade);
        // A kernel without that file (6.12 on): the hwmon's alarm, for now.
        QVERIFY(QFile::remove(platform + "/soc/soc:firmware/get_throttled"));
        QVERIFY(write(hwmon + "/hwmon0/name", "cpu_thermal\n"));
        QVERIFY(write(hwmon + "/hwmon1/name", "rpi_volt\n"));
        QVERIFY(write(hwmon + "/hwmon1/in0_lcrit_alarm", "1\n"));
        QCOMPARE(note(audio), unmade + ". The Pi's power is too low, which can keep USB devices from working");
        QVERIFY(write(hwmon + "/hwmon1/in0_lcrit_alarm", "0\n"));
        QCOMPARE(note(audio), unmade);

        // ALSA's card of it with an input alone (a microphone), then with an
        // output.
        QVERIFY(write(asound + "/cards", " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
                                         "                      bcm2835 Headphones\n"
                                         " 1 [Device         ]: USB-Audio - USB Audio Device\n"
                                         "                      GeneralPlus USB Audio Device at usb-xhci-hcd.0-1.3, full speed\n"));
        QVERIFY(QDir().mkpath(asound + "/card1/pcm0c"));
        QCOMPARE(note(audio), QString("USB Audio Device is plugged in, but it has no output to play through"));
        QVERIFY(QDir().mkpath(asound + "/card1/pcm0p"));
        QCOMPARE(note(audio), QString());
    }

    // Audio Output on Auto where ALSA's default card can't play now: a Pi 5
    // whose HDMI shows no picture (it goes out of a JAMMA board) plays
    // through the first card that can, a USB card, or the HDMI port with the
    // picture, given a process as ALSA's default and libmpv as its device.
    // Not while card0 plays, nor while ALSA's default is set outside OSD/OS;
    // a card chosen plays as chosen, and one unplugged as on Auto.
    void autoCardWhereTheDefaultCantPlay() {
#ifndef Q_OS_LINUX
        QSKIP("ALSA's cards are Linux's");
#else
        QTemporaryDir dir, runtime;
        QVERIFY(dir.isValid() && runtime.isValid());
        const QString asound = dir.path() + "/asound", drm = dir.path() + "/drm";
        const auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).path()); QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(asound + "/cards", " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
                                         "                      vc4-hdmi-0\n"
                                         " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1\n"
                                         "                      vc4-hdmi-1\n"
                                         " 2 [Device         ]: USB-Audio - USB Audio Device\n"
                                         "                      GeneralPlus USB Audio Device at usb-xhci-hcd.0-1.3, full speed\n"));
        for (const char *card : { "/card0/pcm0p", "/card1/pcm0p", "/card2/pcm0p" })
            QVERIFY(QDir().mkpath(asound + card));
        QVERIFY(write(drm + "/card1-HDMI-A-1/enabled", "disabled\n"));
        QVERIFY(write(drm + "/card1-HDMI-A-2/enabled", "disabled\n"));
        QVERIFY(write(drm + "/card1-DPI-1/enabled", "enabled\n"));
        const QByteArray pulse = qgetenv("PULSE_SERVER"), xdg = qgetenv("XDG_RUNTIME_DIR"),
                         alsaCard = qgetenv("ALSA_CARD"), alsaPcmCard = qgetenv("ALSA_PCM_CARD");
        for (const char *name : { "PULSE_SERVER", "ALSA_CARD", "ALSA_PCM_CARD" })
            qunsetenv(name);
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        qputenv("OSDOS_ASOUND_DIR", asound.toUtf8());
        qputenv("OSDOS_DRM_DIR", drm.toUtf8());
        qputenv("OSDOS_ALSA_CONFIG", (dir.path() + "/asound.conf").toUtf8());
        const auto restore = qScopeGuard([&] {
            for (const char *name : { "OSDOS_ASOUND_DIR", "OSDOS_DRM_DIR", "OSDOS_ALSA_CONFIG" })
                qunsetenv(name);
            const auto put = [](const char *name, const QByteArray &value) {
                if (value.isNull()) qunsetenv(name); else qputenv(name, value);
            };
            put("PULSE_SERVER", pulse); put("XDG_RUNTIME_DIR", xdg);
            put("ALSA_CARD", alsaCard); put("ALSA_PCM_CARD", alsaPcmCard);
        });
        AppCore core(dir.path(), dir.path());
        AudioOutput audio(&core);
        QVERIFY(audio.available());
        const auto given = [] {
            QProcessEnvironment env;
            AudioOutput::applyTo(env);
            return env.value("ALSA_CARD", "-");
        };
        const auto autoLabel = [&audio] { return audio.settingRow().value("options").toStringList().value(0); };

        // Both HDMI ports dark: the USB card.
        QCOMPARE(AudioOutput::card(), QString("Device"));
        QCOMPARE(given(), QString("Device"));
        QCOMPARE(AudioOutput::mpvArgs(), QStringList());
        QCOMPARE(AudioOutput::mpvArgs(true), QStringList({ "--audio-device=alsa/default:CARD=Device" }));
        QCOMPARE(AudioOutput::mpvDevice(), QString("alsa/default:CARD=Device"));
        QCOMPARE(autoLabel(), QString("Auto (USB Audio Device)"));
        QCOMPARE(audio.settingRow().value("value").toString(), QString("Auto (USB Audio Device)"));
        // The picture on HDMI1: its port, ahead of the USB card.
        QVERIFY(write(drm + "/card1-HDMI-A-2/enabled", "enabled\n"));
        QCOMPARE(AudioOutput::card(), QString("vc4hdmi1"));
        QCOMPARE(autoLabel(), QString("Auto (HDMI 1)"));
        // On HDMI0, card0: ALSA's own default plays.
        QVERIFY(write(drm + "/card1-HDMI-A-1/enabled", "enabled\n"));
        QCOMPARE(AudioOutput::card(), QString());
        QCOMPARE(given(), QString("-"));
        QCOMPARE(AudioOutput::mpvArgs(true), QStringList());
        QCOMPARE(autoLabel(), QString("Auto"));
        QVERIFY(write(drm + "/card1-HDMI-A-1/enabled", "disabled\n"));
        QVERIFY(write(drm + "/card1-HDMI-A-2/enabled", "disabled\n"));

        // ALSA's default set elsewhere stands: in an ALSA configuration, or
        // OSD/OS's own environment.
        QVERIFY(write(dir.path() + "/asound.conf", "defaults.pcm.card \"vc4hdmi1\"\n"));
        QCOMPARE(AudioOutput::card(), QString());
        QCOMPARE(autoLabel(), QString("Auto"));
        QVERIFY(QFile::remove(dir.path() + "/asound.conf"));
        qputenv("ALSA_CARD", "vc4hdmi1");
        QCOMPARE(AudioOutput::card(), QString());
        qunsetenv("ALSA_CARD");
        QCOMPARE(AudioOutput::card(), QString("Device"));

        // A card chosen, on its command line too, whether it can play or
        // not; one unplugged as on Auto, which a process has as ALSA's
        // default rather than on its command line.
        QVERIFY(core.save_setting(QString(), "audio_output", QVariantMap{ { "card", "vc4hdmi0" }, { "name", "HDMI 0" } }));
        QCOMPARE(AudioOutput::card(), QString("vc4hdmi0"));
        QCOMPARE(AudioOutput::mpvArgs(), QStringList({ "--audio-device=alsa/default:CARD=vc4hdmi0" }));
        QCOMPARE(given(), QString("vc4hdmi0"));
        QVERIFY(core.save_setting(QString(), "audio_output", QVariantMap{ { "card", "Gone" }, { "name", "Gone Card" } }));
        QCOMPARE(AudioOutput::card(), QString("Device"));
        QCOMPARE(AudioOutput::mpvArgs(), QStringList());
        QCOMPARE(AudioOutput::mpvArgs(true), QStringList({ "--audio-device=alsa/default:CARD=Device" }));
        QCOMPARE(given(), QString("Device"));
        QCOMPARE(audio.settingRow().value("value").toString(), QString("Gone Card (Unplugged)"));

        // No card that can play: none given.
        QVERIFY(QDir(asound + "/card2").removeRecursively());
        QVERIFY(core.save_setting(QString(), "audio_output", QString()));
        QCOMPARE(AudioOutput::card(), QString());
        QVERIFY(!AudioOutput::hasPlaybackCard());
#endif
    }

    // The card given to the players at its full level, through amixer (a
    // stand-in noting what it is asked, its controls as a file says): a
    // playback control below 0 dB to 0 dB, one without dB below 100% to
    // 100%, one switched off on; never lowered (one at +4 dB, one at a top
    // below 0 dB), an input's left as they are (a microphone's, a line in's,
    // a control of both). Nothing asked of a card already full. Once for the
    // players given it together; ALSA's default card on Auto; a test's cards
    // none without the stand-in.
    void cardAtFullLevel() {
#ifndef Q_OS_LINUX
        QSKIP("ALSA's cards are Linux's");
#else
        QTemporaryDir dir, runtime;
        QVERIFY(dir.isValid() && runtime.isValid());
        const QString asound = dir.path() + "/asound", amixer = dir.path() + "/amixer", log = dir.path() + "/amixer.log";
        const auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).path()); QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(asound + "/cards", " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
                                         "                      bcm2835 Headphones\n"
                                         " 1 [Device         ]: USB-Audio - USB Audio Device\n"
                                         "                      GeneralPlus USB Audio Device at usb-xhci-hcd.0-1.3, full speed\n"));
        QVERIFY(QDir().mkpath(asound + "/card0/pcm0p") && QDir().mkpath(asound + "/card1/pcm0p"));
        QVERIFY(write(amixer, "#!/bin/sh\n"
                              "echo \"$*\" >> \"$(dirname \"$0\")/amixer.log\"\n"
                              "case \"$*\" in *scontents*) cat \"$(dirname \"$0\")/scontents\" ;; esac\n"));
        QVERIFY(QFile::setPermissions(amixer, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        // As amixer lists them: a USB card's PCM at -20 dB, a control without
        // dB at 32%, a switch alone off, the AV jack's boost (+4 dB), one at
        // its top below 0 dB, and an input's three.
        const QByteArray low =
            "Simple mixer control 'PCM',0\n"
            "  Capabilities: pvolume pswitch pswitch-joined\n"
            "  Playback channels: Front Left - Front Right\n"
            "  Limits: Playback 0 - 87\n"
            "  Mono:\n"
            "  Front Left: Playback 40 [46%] [-20.00dB] [on]\n"
            "  Front Right: Playback 40 [46%] [-20.00dB] [on]\n"
            "Simple mixer control 'Headphone',0\n"
            "  Capabilities: pvolume pvolume-joined\n"
            "  Playback channels: Mono\n"
            "  Limits: Playback 0 - 31\n"
            "  Mono: Playback 10 [32%]\n"
            "Simple mixer control 'Speaker',0\n"
            "  Capabilities: pswitch pswitch-joined\n"
            "  Playback channels: Mono\n"
            "  Mono: Playback [off]\n"
            "Simple mixer control 'Master',0\n"
            "  Capabilities: pvolume pvolume-joined pswitch pswitch-joined\n"
            "  Playback channels: Mono\n"
            "  Limits: Playback -10239 - 400\n"
            "  Mono: Playback 400 [100%] [4.00dB] [on]\n"
            "Simple mixer control 'Top',0\n"
            "  Capabilities: pvolume pvolume-joined\n"
            "  Playback channels: Mono\n"
            "  Limits: Playback 0 - 100\n"
            "  Mono: Playback 100 [100%] [-0.39dB]\n"
            "Simple mixer control 'Mic',0\n"
            "  Capabilities: pvolume pvolume-joined cvolume cvolume-joined pswitch pswitch-joined cswitch cswitch-joined\n"
            "  Playback channels: Mono\n"
            "  Capture channels: Mono\n"
            "  Limits: Playback 0 - 31 Capture 0 - 16\n"
            "  Mono: Playback 0 [0%] [-23.00dB] [off] Capture 10 [62%] [10.00dB] [on]\n"
            "Simple mixer control 'Line',0\n"
            "  Capabilities: pvolume pswitch\n"
            "  Playback channels: Mono\n"
            "  Limits: Playback 0 - 31\n"
            "  Mono: Playback 0 [0%] [-34.50dB] [off]\n"
            "Simple mixer control 'Auto Gain Control',0\n"
            "  Capabilities: switch switch-joined\n"
            "  Playback channels: Mono\n"
            "  Mono: [off]\n";
        QVERIFY(write(dir.path() + "/scontents", low));
        const QByteArray pulse = qgetenv("PULSE_SERVER"), xdg = qgetenv("XDG_RUNTIME_DIR"),
                         alsaCard = qgetenv("ALSA_CARD"), alsaPcmCard = qgetenv("ALSA_PCM_CARD");
        for (const char *name : { "PULSE_SERVER", "ALSA_CARD", "ALSA_PCM_CARD" })
            qunsetenv(name);
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        qputenv("OSDOS_ASOUND_DIR", asound.toUtf8());
        qputenv("OSDOS_DRM_DIR", (dir.path() + "/drm").toUtf8());
        qputenv("OSDOS_ALSA_CONFIG", (dir.path() + "/asound.conf").toUtf8());
        qputenv("OSDOS_AMIXER", amixer.toUtf8());
        const auto restore = qScopeGuard([&] {
            for (const char *name : { "OSDOS_ASOUND_DIR", "OSDOS_DRM_DIR", "OSDOS_ALSA_CONFIG", "OSDOS_AMIXER" })
                qunsetenv(name);
            const auto put = [](const char *name, const QByteArray &value) {
                if (value.isNull()) qunsetenv(name); else qputenv(name, value);
            };
            put("PULSE_SERVER", pulse); put("XDG_RUNTIME_DIR", xdg);
            put("ALSA_CARD", alsaCard); put("ALSA_PCM_CARD", alsaPcmCard);
        });
        // What amixer has been asked, a line each; and the same taken, for the
        // next look. amixer runs on a thread of its own, so a look waits for
        // what it expects.
        const auto lines = [&log] {
            if (!QFileInfo::exists(log))
                return QStringList();
            return QString::fromUtf8(contents(log)).split('\n', Qt::SkipEmptyParts);
        };
        const auto asked = [&log, &lines] {
            const QStringList all = lines();
            QFile::remove(log);
            return all;
        };
        AppCore core(dir.path(), dir.path());
        AudioOutput audio(&core);
        QVERIFY(audio.available());
        QTest::qWait(200);
        asked();

        // The USB card chosen, at once, the player going on meanwhile.
        const QVariantMap usb { { "card", "Device" }, { "name", "USB Audio Device" } };
        QVERIFY(core.save_setting(QString(), "audio_output", usb));
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 4, 5000);
        QCOMPARE(asked(), QStringList({ "-c Device scontents",
                                        "-c Device -q sset 'PCM',0 0dB",
                                        "-c Device -q sset 'Headphone',0 100%",
                                        "-c Device -q sset 'Speaker',0 unmute" }));
        // An mpv process's command line and environment: nothing more.
        QProcessEnvironment env;
        AudioOutput::mpvArgs();
        AudioOutput::applyTo(env);
        QCOMPARE(env.value("ALSA_CARD"), QString("Device"));
        QTest::qWait(300);
        QCOMPARE(asked(), QStringList());
        // Auto, with card0 playing: ALSA's default card, as amixer has it.
        QVERIFY(core.save_setting(QString(), "audio_output", QString()));
        QTRY_VERIFY_WITH_TIMEOUT(!lines().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(lines().size() >= 4, 5000);
        QCOMPARE(asked().value(0), QString("scontents"));
        // Already full: looked at, nothing set.
        QByteArray full = low;
        full.replace("Playback 40 [46%] [-20.00dB]", "Playback 87 [100%] [0.00dB]")
            .replace("Playback 10 [32%]", "Playback 31 [100%]")
            .replace("Mono: Playback [off]", "Mono: Playback [on]");
        QVERIFY(write(dir.path() + "/scontents", full));
        QVERIFY(core.save_setting(QString(), "audio_output", usb));
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 1, 5000);
        QTest::qWait(300);
        QCOMPARE(asked(), QStringList({ "-c Device scontents" }));
        // A test's cards without the stand-in: amixer isn't run.
        qunsetenv("OSDOS_AMIXER");
        QVERIFY(core.save_setting(QString(), "audio_output", QString()));
        QCOMPARE(AudioOutput::card(), QString());
        QTest::qWait(300);
        QVERIFY(!QFileInfo::exists(log));
#endif
    }

    // Quit's Save Logs on the image: OSDOS_MEDIA_DIR (the card's OSD-OS
    // partition) and scripts/save-logs.sh beside the app, the repository's
    // own, run in a process of its own: the reports in Logs/<date_time>/,
    // the folder told relative to the partition, one copy at a time; a
    // partition not there told as such; nothing offered without one.
    void savesLogsToTheCard() {
#ifndef Q_OS_LINUX
        QSKIP("The logs are copied on the OSD/OS image (Linux)");
#else
        QTemporaryDir app;
        QTemporaryDir data;
        QTemporaryDir card;
        QVERIFY(app.isValid() && data.isValid() && card.isValid());
        QVERIFY(QDir(app.path()).mkpath(QStringLiteral("scripts")));
        QVERIFY(QFile::copy(QStringLiteral(OSDOS_SOURCE_DIR "/scripts/save-logs.sh"),
                            app.path() + QStringLiteral("/scripts/save-logs.sh")));
        QFile report(data.path() + QStringLiteral("/stall-report.txt"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write("stalled\n");
        report.close();
        const QByteArray media = qgetenv("OSDOS_MEDIA_DIR");
        const auto restore = qScopeGuard([&media] {
            if (media.isNull())
                qunsetenv("OSDOS_MEDIA_DIR");
            else
                qputenv("OSDOS_MEDIA_DIR", media);
        });

        qunsetenv("OSDOS_MEDIA_DIR");
        AppCore core(app.path(), data.path());
        QVERIFY(!core.canSaveLogs());
        QVERIFY(!core.saveLogs());

        qputenv("OSDOS_MEDIA_DIR", card.path().toUtf8());
        QVERIFY(core.canSaveLogs());
        QSignalSpy saved(&core, &AppCore::logsSaved);
        QVERIFY(core.saveLogs());
        QVERIFY(!core.saveLogs());   // one copy at a time
        QVERIFY(saved.wait(20000));
        QCOMPARE(saved.size(), 1);
        QVERIFY2(saved.at(0).at(0).toBool(), qPrintable(saved.at(0).at(1).toString()));
        const QString folder = saved.at(0).at(1).toString();
        QVERIFY2(QRegularExpression(QStringLiteral("^Logs/\\d{4}-\\d\\d-\\d\\d_\\d\\d-\\d\\d-\\d\\d$")).match(folder).hasMatch(),
                 qPrintable(folder));
        QFile copied(card.path() + QLatin1Char('/') + folder + QStringLiteral("/stall-report.txt"));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("stalled\n"));

        // The partition gone (not mounted): said so, nothing made.
        qputenv("OSDOS_MEDIA_DIR", (card.path() + QStringLiteral("/gone")).toUtf8());
        QVERIFY(core.saveLogs());
        QVERIFY(saved.wait(20000));
        QCOMPARE(saved.size(), 2);
        QVERIFY(!saved.at(1).at(0).toBool());
        QCOMPARE(saved.at(1).at(1).toString(), QStringLiteral("The card's OSD-OS partition isn't there or can't be written"));
        QVERIFY(!QFileInfo::exists(card.path() + QStringLiteral("/gone")));
#endif
    }

    // config.json is read once and kept while the file stays as it was: a
    // setting saved by OSD/OS, or the file edited by hand (its time or size
    // changed), is read afresh.
    void configReadOnceWhileUnchanged() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppCore core(dir.path(), dir.path());
        QVERIFY(core.save_setting(QString(), "osd_theme", "VHS"));
        QCOMPARE(core.get_setting(QString(), "osd_theme").toString(), QString("VHS"));
        // The file changed under the app with its time and size as they were
        // (a value of the same length, the time put back): kept as read.
        const QString path = dir.path() + "/config.json";
        const QByteArray saved = contents(path);
        const QDateTime modified = QFileInfo(path).lastModified();
        QByteArray edited = saved;
        edited.replace("\"VHS\"", "\"VH2\"");
        QVERIFY(edited != saved);
        QCOMPARE(edited.size(), saved.size());
        const auto writeFile = [&path, &edited](const QDateTime &time) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(edited) != edited.size())
                return false;
            file.close();
            return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
        };
        QVERIFY(writeFile(modified));
        QCOMPARE(QFileInfo(path).lastModified(), modified);
        QCOMPARE(core.get_setting(QString(), "osd_theme").toString(), QString("VHS"));
        // Edited by hand, a second on: read again.
        QVERIFY(writeFile(modified.addSecs(2)));
        QCOMPARE(core.get_setting(QString(), "osd_theme").toString(), QString("VH2"));
        // Saved by OSD/OS: read again at once, whatever the file's time.
        QVERIFY(core.save_setting(QString(), "osd_theme", "Video 1"));
        QCOMPARE(core.get_setting(QString(), "osd_theme").toString(), QString("Video 1"));
        // Gone: the defaults, as before, not what was read.
        QVERIFY(QFile::remove(path));
        QCOMPARE(core.get_setting(QString(), "osd_theme").toString(), QString());
    }

    // What of a text OSD/OS Setup's file is (setupfile::parse()), checked as
    // the card's applier checks it, and its settings merged key by key.
    void setupFileChecked() {
        setupfile::Setup setup;
        QString why;
        QVERIFY(!setupfile::parse("not json", &setup, &why));
        QCOMPARE(why, QString("not JSON"));
        QVERIFY(!setupfile::parse("[1]", &setup, &why));
        QCOMPARE(why, QString("not OSD/OS Setup's settings"));
        QVERIFY(!setupfile::parse(R"({"osdos_setup": true})", &setup, &why));
        QCOMPARE(why, QString("not OSD/OS Setup's settings"));
        QVERIFY(!setupfile::parse(R"({"osdos_setup": 2})", &setup, &why));
        QCOMPARE(why, QString("of version 2, which this OSD/OS doesn't know"));
        QVERIFY(!setupfile::parse(R"({"osdos_setup": 1, "config": {"app": {"volume": 1e400}}})", &setup, &why));
        QVERIFY2(why.startsWith("not JSON"), qPrintable(why));
        QVERIFY(!setupfile::parse(QByteArray(1024 * 1024 + 1, ' '), &setup, &why));
        QCOMPARE(why, QString("bigger than a settings file is"));
        // A user without a password and a key of more than letters and
        // digits are left out, as are config's parts that aren't objects
        // and the parts a computer has no use for.
        QVERIFY(setupfile::parse(R"({"osdos_setup": 1, "retroachievements": {"user": "a", "key": "no key!"},
                                     "config": {"app": "x", "modules": {"m": {"k": 1}}, "lists": {}},
                                     "display": {"preset": "jamma"}, "system": {"password_hash": "$6$x$y",
                                     "keyboard": "tr"}})", &setup, &why));
        QVERIFY(setup.account.isEmpty());
        QCOMPARE(setup.config.keys(), QStringList({ "modules" }));
        QCOMPARE(setup.cardOnly, QStringList({ "the display output", "the password", "the keyboard" }));
        // Objects key by key, anything else in place of what was there.
        const QJsonObject into = QJsonDocument::fromJson(R"({"a": {"x": 1, "y": [1]}, "b": 2})").object();
        const QJsonObject update = QJsonDocument::fromJson(R"({"a": {"y": [2], "z": {"q": 1}}, "b": {"c": 3}})").object();
        QCOMPARE(setupfile::merged(into, update),
                 QJsonDocument::fromJson(R"({"a": {"x": 1, "y": [2], "z": {"q": 1}}, "b": {"c": 3}})").object());
        QCOMPARE(setupfile::count(update), 3);
    }

    // OSD/OS Setup's file in the data folder as the app starts
    // (setupfile::applyAtStart()): taken off the disk first, its settings
    // merged into config.json, the RetroAchievements account handed on,
    // owner-only, and what a card alone sets named. One that isn't such a
    // file goes all the same, with whatever passwords it had; a config.json
    // that can't be read is left as it is.
    void setupFileTakenAsTheAppStarts() {
        QTemporaryDir dir;
        const QString data = dir.path();
        const QString setupPath = data + "/osdos-setup.json";
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(data + "/config.json", R"({"app": {"color_scheme": "Video 1", "subtitles": {"lang": "en"}},
                                                 "modules": {"plex": {"server": "a"}}})"));
        // With a Windows editor's BOM.
        QVERIFY(write(setupPath, "\xEF\xBB\xBF" R"({"osdos_setup": 1,
            "device": {"name": "arcade-1", "kind": "arcade"},
            "wifi": {"country": "TR", "ssid": "home", "password": "secret"},
            "system": {"timezone": "Europe/Istanbul", "ssh": {"enabled": false}},
            "retroachievements": {"user": " player ", "password": "pw", "key": "ABC123"},
            "config": {"app": {"osd_theme": "Arcade", "subtitles": {"size": "Large"}},
                       "modules": {"arcade": {"engine": "MAME"}}, "other": {"x": 1}}})"));
        QCOMPARE(setupfile::places(data).first(), QDir::cleanPath(setupPath));
        const setupfile::Result result = setupfile::applyAtStart(data);
        QVERIFY2(result.ok, qPrintable(result.lines.join('\n')));
        QCOMPARE(result.applied, 2);
        QVERIFY(!QFile::exists(setupPath));
        QCOMPARE(result.lines.first(), QString("From osdos-setup.json in the data folder"));
        const QJsonObject config = QJsonDocument::fromJson(contents(data + "/config.json")).object();
        const QJsonObject app = config["app"].toObject();
        QCOMPARE(app["color_scheme"].toString(), QString("Video 1"));
        QCOMPARE(app["osd_theme"].toString(), QString("Arcade"));
        QCOMPARE(app["subtitles"].toObject()["lang"].toString(), QString("en"));
        QCOMPARE(app["subtitles"].toObject()["size"].toString(), QString("Large"));
        QCOMPARE(config["modules"].toObject()["plex"].toObject()["server"].toString(), QString("a"));
        QCOMPARE(config["modules"].toObject()["arcade"].toObject()["engine"].toString(), QString("MAME"));
        QVERIFY(!config.contains("other"));
        const QString accountPath = data + "/arcade/retroachievements-setup.json";
        const QJsonObject account = QJsonDocument::fromJson(contents(accountPath)).object();
        QCOMPARE(account["user"].toString(), QString("player"));
        QCOMPARE(account["password"].toString(), QString("pw"));
        QCOMPARE(account["key"].toString(), QString("ABC123"));
#ifdef Q_OS_UNIX
        QCOMPARE(QFile::permissions(accountPath) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
        QCOMPARE(result.lines.mid(1), QStringList({
            "OSD/OS settings: 3 set",
            "RetroAchievements: player, signed in with the password; Web API key kept",
            "Not set here: the device's name, Wi-Fi, SSH and the time zone, which an OSD/OS card alone sets" }));
        QCOMPARE(setupfile::noticeTitle(result), QString("OSD/OS Setup applied"));
        // None there: nothing to say.
        QVERIFY(setupfile::applyAtStart(data).lines.isEmpty());
        QCOMPARE(setupfile::noticeTitle(setupfile::Result()), QString());

        QVERIFY(write(setupPath, R"({"wifi": {"password": "secret"}})"));
        setupfile::Result refused = setupfile::applyAtStart(data);
        QVERIFY(!refused.ok);
        QVERIFY(!QFile::exists(setupPath));
        QVERIFY2(refused.lines.first().endsWith("deleted, not applied: not OSD/OS Setup's settings"),
                 qPrintable(refused.lines.first()));
        QCOMPARE(setupfile::noticeTitle(refused), QString("OSD/OS Setup not applied"));
        QVERIFY(write(data + "/config.json", "{\"app\": {"));
        QVERIFY(write(setupPath, R"({"osdos_setup": 1, "config": {"app": {"osd_theme": "VHS"}},
                                     "retroachievements": {"key": "K1"}})"));
        refused = setupfile::applyAtStart(data);
        QVERIFY(!refused.ok);
        QCOMPARE(refused.applied, 1);
        QCOMPARE(contents(data + "/config.json"), QByteArray("{\"app\": {"));
        QCOMPARE(setupfile::noticeTitle(refused), QString("OSD/OS Setup partly applied"));
    }

    // Settings → Paste OSD/OS Setup (AppCore::applySetupText()): the
    // settings set at once, each one that changed told as Settings tells it
    // (a value of values by its dotted keys), the account handed to the
    // Arcade module, and the notice saying so.
    void setupTextPastedAtOnce() {
        QTemporaryDir dir;
        AppCore core(dir.path(), dir.path());
        QVERIFY(core.save_setting("", "osd_theme", "VHS"));
        QVERIFY(core.save_setting("plex", "libraries.a", "On"));
        QSignalSpy app(&core, &AppCore::appSettingChanged);
        QSignalSpy module(&core, &AppCore::moduleSettingChanged);
        QSignalSpy account(&core, &AppCore::setupAccountWritten);
        QVariantMap notice = core.applySetupText(R"({"osdos_setup": 1,
            "config": {"app": {"osd_theme": "VHS", "window_mode": "On"},
                       "modules": {"plex": {"libraries": {"a": "On", "b": "Off"}}, "arcade": {"enabled": false}}},
            "retroachievements": {"key": "K1"}, "display": {"preset": "jamma"}})");
        QVERIFY(notice["ok"].toBool());
        QCOMPARE(notice["title"].toString(), QString("OSD/OS Setup pasted"));
        QCOMPARE(notice["detail"].toString(), QString("OSD/OS settings: 5 set\nRetroAchievements: Web API key kept\n"
                                                      "Not set here: the display output, which an OSD/OS card alone sets"));
        QCOMPARE(app.count(), 1);
        QCOMPARE(app.at(0), QVariantList({ "window_mode", "On" }));
        QCOMPARE(module.count(), 2);
        QCOMPARE(module.at(0), QVariantList({ "arcade", "enabled", false }));
        QCOMPARE(module.at(1), QVariantList({ "plex", "libraries.b", "Off" }));
        QCOMPARE(account.count(), 1);
        QCOMPARE(core.get_setting("plex", "libraries.a").toString(), QString("On"));
        QCOMPARE(core.get_setting("plex", "libraries.b").toString(), QString("Off"));
        QCOMPARE(QJsonDocument::fromJson(contents(dir.path() + "/arcade/retroachievements-setup.json")).object()["key"]
                     .toString(), QString("K1"));
        notice = core.applySetupText("  \n");
        QCOMPARE(notice["title"].toString(), QString("Nothing to paste"));
        notice = core.applySetupText("{\"x\": 1}");
        QVERIFY(!notice["ok"].toBool());
        QCOMPARE(notice["title"].toString(), QString("Not pasted"));
        QCOMPARE(notice["detail"].toString(), QString("What was pasted is not OSD/OS Setup's settings"));
        notice = core.applySetupText(R"({"osdos_setup": 1, "wifi": {"ssid": "home"}})");
        QCOMPARE(notice["title"].toString(), QString("Nothing set from OSD/OS Setup"));
        QCOMPARE(app.count(), 1);
    }
};
QTEST_GUILESS_MAIN(ReliabilityTest)
#include "reliability_test.moc"
