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

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

class StorageSearchTest : public QObject {
    Q_OBJECT
private slots:
    // The folder Local Files opens while its setting names none: Movies in
    // OSDOS_MEDIA_DIR (the image's card partition, where the Arcade's Games
    // folder is too), made as it is opened; without it, Movies in the data
    // folder's media.
    void defaultFolderIsMovies() {
        QTemporaryDir tmp;
        const QString media = tmp.path() + "/card";
        QVERIFY(QDir().mkpath(media + "/Games/Arcade"));
        qputenv("OSDOS_MEDIA_DIR", media.toUtf8());
        {
            LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
            QCOMPARE(backend.mediaRoot(), media + "/Movies");
            QVERIFY(QFileInfo(media + "/Movies").isDir());
        }
        qunsetenv("OSDOS_MEDIA_DIR");
        LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
        QCOMPARE(backend.mediaRoot(), tmp.path() + "/media/Movies");
        // A folder chosen in Settings, then the setting reset: Movies again.
        backend.setMediaRoot(media);
        QCOMPARE(backend.mediaRoot(), media);
        backend.setMediaRoot(QString());
        QCOMPARE(backend.mediaRoot(), tmp.path() + "/media/Movies");
    }

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

    // A drive's recycle bin, lost+found and the like are hidden from the
    // browser; a search must not list what is in them either.
    void searchSkipsSystemFolders() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString media = tmp.path() + "/media";
        QVERIFY(QDir().mkpath(media + "/$RECYCLE.BIN/S-1-5-21/Holiday 2019"));
        QVERIFY(QDir().mkpath(media + "/lost+found"));
        QVERIFY(QDir().mkpath(media + "/Films"));
        QVERIFY(writeFileAtomically(media + "/$RECYCLE.BIN/S-1-5-21/Holiday 2019/Clip.mp4", "film"));
        QVERIFY(writeFileAtomically(media + "/lost+found/Clip.mp4", "film"));
        QVERIFY(writeFileAtomically(media + "/Films/Clip.mp4", "film"));
        LocalFilesBackend backend(tmp.path(), tmp.path(), nullptr);
        backend.setMediaRoot(media);
        QSignalSpy ready(&backend, &LocalFilesBackend::searchReady);
        QVERIFY(!backend.search("search/clip", "clip").isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        const QVariantList results = backend.search("search/clip", "clip").toList();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().toMap().value("path").toString(), media + "/Films/Clip.mp4");
        QVERIFY(!backend.search("search/holiday", "holiday").isValid());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
        QVERIFY(backend.search("search/holiday", "holiday").toList().isEmpty());
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

    // config.json edited by hand and left unreadable (a stray comma) is not
    // written over: a save would put the defaults in place of every setting
    // in it. The lists the same way. An empty file is as none.
    void unreadableConfigIsNotWrittenOver() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString config = tmp.path() + "/config.json";
        const QByteArray broken = "{\"app\": {\"color_scheme\": \"Video 2\",}}";
        QVERIFY(writeFileAtomically(config, broken));
        AppCore core(tmp.path(), tmp.path());
        QSignalSpy app(&core, &AppCore::appSettingChanged);
        QVERIFY(!core.save_setting("", "color_scheme", "Video 3"));
        QCOMPARE(app.count(), 0);
        QFile left(config);
        QVERIFY(left.open(QIODevice::ReadOnly));
        QCOMPARE(left.readAll(), broken);
        QVERIFY(writeFileAtomically(config, "{\"app\": {\"color_scheme\": \"Video 2\"}}"));
        QVERIFY(core.save_setting("", "color_scheme", "Video 3"));
        QCOMPARE(core.get_setting("", "color_scheme").toString(), QString("Video 3"));

        const QString lists = tmp.path() + "/lists.json";
        const QByteArray brokenLists = "{\"com.osdos.local_files\": {\"favorites\": [}";
        QVERIFY(writeFileAtomically(lists, brokenLists));
        core.add_to_list("com.osdos.local_files", "favorites", {{"path", "/a.mp4"}});
        QFile leftLists(lists);
        QVERIFY(leftLists.open(QIODevice::ReadOnly));
        QCOMPARE(leftLists.readAll(), brokenLists);
        QVERIFY(writeFileAtomically(lists, ""));
        core.add_to_list("com.osdos.local_files", "favorites", {{"path", "/a.mp4"}});
        QVERIFY(core.list_contains("com.osdos.local_files", "favorites", "/a.mp4"));
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

    // A setting config.json couldn't take (a full card) is neither reported
    // saved nor signalled: the backends keep what get_setting() still says.
    // (A file size limit, which holds for root too.)
    void unsavedSettingIsNotSignalled() {
#ifndef Q_OS_UNIX
        QSKIP("Requires a Unix file size limit");
#else
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppCore core(tmp.path(), tmp.path());
        QSignalSpy app(&core, &AppCore::appSettingChanged);
        QSignalSpy module(&core, &AppCore::moduleSettingChanged);
        QVERIFY(core.save_setting("", "color_scheme", "Video 2"));
        QVERIFY(core.save_setting("com.osdos.arcade", "video", "BGFX"));
        QCOMPARE(app.size(), 1);
        QCOMPARE(module.size(), 1);
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{};
        QCOMPARE(getrlimit(RLIMIT_FSIZE, &limit), 0);
        const rlimit full{ 1, limit.rlim_max };
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &full), 0);
        const bool appSaved = core.save_setting("", "color_scheme", "Video 3");
        const bool moduleSaved = core.save_setting("com.osdos.arcade", "video", "Software");
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &limit), 0);
        QVERIFY(!appSaved);
        QVERIFY(!moduleSaved);
        QCOMPARE(app.size(), 1);
        QCOMPARE(module.size(), 1);
        QCOMPARE(core.get_setting("", "color_scheme").toString(), QString("Video 2"));
        QCOMPARE(core.get_setting("com.osdos.arcade", "video").toString(), QString("BGFX"));
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
