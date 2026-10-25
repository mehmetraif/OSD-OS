#include "AppCore.h"
#include "audio/AudioOutput.h"
#include "modules/playlists/ServerDownload.h"
#include "modules/weather/WeatherBackend.h"
#include "update/UpdateManager.h"
#include "util/AtomicFile.h"
#include "util/DurableFile.h"
#include "util/AsyncDirectoryCache.h"
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
    // plugged in, a USB sound device no driver took (by its product name,
    // once for its two sound interfaces, a keyboard left out), one the driver
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

        QVERIFY(write(usb + "/1-1.3/product", "USB Audio Device\n"));
        QVERIFY(write(usb + "/1-1.3:1.0/bInterfaceClass", "01\n"));
        QVERIFY(write(usb + "/1-1.3:1.1/bInterfaceClass", "01\n"));
        QVERIFY(write(usb + "/1-1.4/product", "Keyboard\n"));
        QVERIFY(write(usb + "/1-1.4:1.0/bInterfaceClass", "03\n"));
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
};
QTEST_GUILESS_MAIN(ReliabilityTest)
#include "reliability_test.moc"
