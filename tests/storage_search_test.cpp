#include "AppCore.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "util/AtomicFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

class StorageSearchTest : public QObject {
    Q_OBJECT
private slots:
    void asyncFolderEntriesMatchAndRefresh() {
        QTemporaryDir tmp;
        const QString media = tmp.path() + "/media";
        QVERIFY(QDir().mkpath(media + "/lost+found"));
        QVERIFY(QDir().mkpath(media + "/Folder"));
        QVERIFY(writeFileAtomically(media + "/Film.mp4", "film"));
        QVERIFY(writeFileAtomically(media + "/ignored.txt", "ignored"));
        LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
        backend.setMediaRoot(media);
        QSignalSpy ready(&backend, &LocalFilesBackend::entriesReady);
        QVERIFY(!backend.requestEntries(media).isValid());
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(backend.requestEntries(media), backend.entries(media));
        QVERIFY(writeFileAtomically(media + "/Another.mp4", "film"));
        backend.clearDirectoryCache();
        QVERIFY(!backend.requestEntries(media).isValid());
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(backend.requestEntries(media), backend.entries(media));
    }
    void first200SortedMatches() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString media = tmp.path() + "/media";
        QVERIFY(QDir().mkpath(media));
        for (int i = 399; i >= 0; --i) {
            QFile file(media + QString("/Movie%1.mp4").arg(i, 4, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
        backend.setMediaRoot(media);
        QSignalSpy ready(&backend, &LocalFilesBackend::searchReady);
        QVERIFY(!backend.search("search/movie", "MOVIE").isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        const auto results = backend.search("search/movie", "MOVIE").toList();
        QCOMPARE(results.size(), 200);
        for (int i = 0; i < results.size(); ++i)
            QCOMPARE(results[i].toMap().value("name").toString(),
                     QString("Movie%1.mp4").arg(i, 4, 10, QLatin1Char('0')));
    }

    void supersededSearchDoesNotPublish() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString media = tmp.path() + "/media";
        QVERIFY(QDir().mkpath(media));
        QFile file(media + "/Newest.mp4");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
        backend.setMediaRoot(media);
        QSignalSpy ready(&backend, &LocalFilesBackend::searchReady);
        backend.search("search/old", "old");
        backend.search("search/new", "newest");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QCOMPARE(ready.first().first().toString(), QString("search/new"));
        QCOMPARE(backend.search("search/new", "newest").toList().size(), 1);
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        QCOMPARE(ready.count(), 1);
    }

    void destructionDuringSearch() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
            backend.search("search/test", "test");
        }
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }

    void settingsAndHistorySurviveReload() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        {
            AppCore core(tmp.path(), tmp.path());
            core.save_setting("", "color_scheme", "Video 2");
            LocalFilesBackend backend(tmp.path(), tmp.path(), &core);
            backend.savePosition("film.mp4", 12345, -1);
            backend.savePosition("other.mp4", 67890, 2);
            backend.clearPosition("other.mp4");
        }
        AppCore restored(tmp.path(), tmp.path());
        QCOMPARE(restored.get_setting("", "color_scheme").toString(), QString("Video 2"));
        LocalFilesBackend backend(tmp.path(), tmp.path(), &restored);
        QCOMPARE(backend.getSavedPosition("film.mp4").value("pos").toInt(), 12345);
        QVERIFY(backend.getSavedPosition("other.mp4").isEmpty());
    }

    void failedReplacementPreservesExistingFiles() {
#ifndef Q_OS_UNIX
        QSKIP("Requires Unix directory permissions");
#else
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppCore core(tmp.path(), tmp.path());
        LocalFilesBackend backend(tmp.path(), tmp.path(), &core);
        core.save_setting("", "color_scheme", "Video 2");
        backend.savePosition("film.mp4", 12345, -1);
        const auto originalPermissions = QFileInfo(tmp.path()).permissions();
        QVERIFY(QFile::setPermissions(tmp.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        // QSaveFile must not fall back to truncating the existing, writable file.
        QFile probe(tmp.path() + "/probe");
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            QFile::setPermissions(tmp.path(), originalPermissions);
            QSKIP("Current user can bypass directory permissions");
        }
        core.save_setting("", "color_scheme", "Video 3");
        backend.savePosition("film.mp4", 54321, -1);
        const bool restored = QFile::setPermissions(tmp.path(), originalPermissions);
        QVERIFY(restored);
        QCOMPARE(core.get_setting("", "color_scheme").toString(), QString("Video 2"));
        QCOMPARE(backend.getSavedPosition("film.mp4").value("pos").toInt(), 12345);
#endif
    }

    // A token or a key is owner-only from its first write on; any other file
    // written again keeps the permissions it had.
    void atomicWritesKeepPermissions() {
#ifndef Q_OS_UNIX
        QSKIP("Requires Unix file permissions");
#else
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const auto groupOrOther = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                  | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        const QString token = tmp.path() + "/token.json";
        QVERIFY(writeFileAtomically(token, "{\"t\":1}", QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(QFileInfo(token).permissions() & groupOrOther, QFileDevice::Permissions());
        QVERIFY(writeFileAtomically(token, "{\"t\":2}"));
        QCOMPARE(QFileInfo(token).permissions() & groupOrOther, QFileDevice::Permissions());
        QFile written(token);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("{\"t\":2}"));

        const QString shared = tmp.path() + "/shared.json";
        QVERIFY(writeFileAtomically(shared, "[]"));
        QVERIFY(QFile::setPermissions(shared, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        QVERIFY(writeFileAtomically(shared, "[1]"));
        QVERIFY(QFileInfo(shared).permissions() & QFileDevice::ReadOther);
        // Nothing left beside them.
        QCOMPARE(QDir(tmp.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
#endif
    }
};

QTEST_GUILESS_MAIN(StorageSearchTest)
#include "storage_search_test.moc"
