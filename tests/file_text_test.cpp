// The small files read whole (util/FileText.h): a file's bytes, up to a
// limit, none for a file that isn't there; and a JSON object, told apart
// from a file that isn't there or is empty (no reason) and one that is there
// but broken or not an object (the reason, for the log).
#include "util/FileText.h"
#include <QTemporaryDir>
#include <QtTest>

class FileTextTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &bytes) {
        const QString path = m_dir.path() + QLatin1Char('/') + name;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return QString();
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void text() {
        const QString path = write(QStringLiteral("value"), "enabled\n");
        QCOMPARE(filetext::readText(path), QByteArray("enabled\n"));
        QCOMPARE(filetext::readText(path, 3), QByteArray("ena"));
        QCOMPARE(filetext::readText(m_dir.path() + QStringLiteral("/none")), QByteArray());
        QCOMPARE(filetext::readText(m_dir.path()), QByteArray());   // a folder
#ifdef Q_OS_LINUX
        // /proc's files say they are empty, and aren't.
        QVERIFY(filetext::readText(QStringLiteral("/proc/self/status")).startsWith("Name:"));
        QCOMPARE(filetext::readText(QStringLiteral("/proc/self/status"), 5), QByteArray("Name:"));
#endif
    }

    void jsonObject() {
        QJsonObject object{ { QStringLiteral("kept"), true } };
        QString why = QStringLiteral("before");
        QVERIFY(filetext::readJsonObject(write(QStringLiteral("good.json"), R"({"a": 1})"), &object, &why));
        QCOMPARE(object, QJsonObject({ { QStringLiteral("a"), 1 } }));
        QVERIFY(why.isEmpty());

        // Not there, or empty: no reason, the object as it was.
        object = QJsonObject{ { QStringLiteral("kept"), true } };
        why = QStringLiteral("before");
        QVERIFY(!filetext::readJsonObject(m_dir.path() + QStringLiteral("/none.json"), &object, &why));
        QVERIFY(why.isEmpty());
        QVERIFY(object.value(QStringLiteral("kept")).toBool());
        QVERIFY(!filetext::readJsonObject(write(QStringLiteral("empty.json"), " \n"), &object, &why));
        QVERIFY(why.isEmpty());

        // There but broken, or not an object: why.
        QVERIFY(!filetext::readJsonObject(write(QStringLiteral("broken.json"), R"({"a": 1,)"), &object, &why));
        QVERIFY(!why.isEmpty());
        QVERIFY(object.value(QStringLiteral("kept")).toBool());
        QVERIFY(!filetext::readJsonObject(write(QStringLiteral("list.json"), "[1, 2]"), &object, &why));
        QCOMPARE(why, QStringLiteral("not a JSON object"));
        // Without asking why.
        QVERIFY(!filetext::readJsonObject(write(QStringLiteral("broken2.json"), "{"), &object));
    }
};

QTEST_GUILESS_MAIN(FileTextTest)
#include "file_text_test.moc"
