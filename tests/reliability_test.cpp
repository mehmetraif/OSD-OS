#include "modules/playlists/ServerDownload.h"
#include "modules/weather/WeatherBackend.h"
#include "update/UpdateManager.h"
#include "util/AtomicFile.h"
#include "util/DurableFile.h"
#include "util/AsyncDirectoryCache.h"
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

    void durableWriteErrorsAreReported() {
#ifdef Q_OS_UNIX
        QTemporaryDir tmp;
        QVERIFY(!syncPath(tmp.path() + "/missing").isEmpty());
        QVERIFY(!syncPath("/dev/null").isEmpty()); // fsync returns EINVAL
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
};
QTEST_GUILESS_MAIN(ReliabilityTest)
#include "reliability_test.moc"
