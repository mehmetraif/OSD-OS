// Settings → System → Export to OSD/OS Setup (src/util/SetupExport): the
// device's settings as OSD/OS Setup's page takes them, the settings at their
// defaults and those not on its form left out; the system as Linux's files
// say; the file cut into QR codes' links that put it back together whole;
// and the codes as large as the links need, never past the version asked.

#include "util/SetupExport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <qrcodegen.hpp>

class SetupExportTest : public QObject {
    Q_OBJECT

    static QJsonObject json(const char *text) { return QJsonDocument::fromJson(text).object(); }

    static void writeFile(const QString &path, const QByteArray &data) {
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
    }

    // The page's file back from the links' text, as the page reads it.
    static QJsonObject joined(const QStringList &links) {
        QString text;
        for (const QString &link : links)
            text += link.section(QLatin1Char('.'), -1);
        const QByteArray packed = QByteArray::fromBase64(text.toLatin1(), QByteArray::Base64UrlEncoding);
        // qUncompress() wants the length first, which it only takes as a hint.
        QByteArray sized(4, '\0');
        sized[0] = char(0x00);
        sized[1] = char(0x10);
        return QJsonDocument::fromJson(qUncompress(sized + packed)).object();
    }

private slots:
    // The form: OSD/OS's rows and each module's as the page has them, with
    // their defaults; rows without values of their own left out.
    void form() {
        const QJsonObject extra = json(R"({
            "app": [{ "title": "General", "settings": [
                { "key": "osd_theme", "default": "" },
                { "key": "monitor", "default": "Horizontal" },
                { "key": "hint_bar", "type": "toggle", "on": "On", "off": "Off", "default": "On" } ] }],
            "modules": { "com.osdos.arcade": {
                "options": { "start_filter": [["All Games", "all"]], "menu_filters": [["Favorites", "favorites"]] },
                "defaults": { "start_filter": "all" },
                "extra": [{ "key": "online_name", "default": "" }] } } })");
        const QHash<QString, QJsonObject> manifests{
            { QStringLiteral("com.osdos.arcade"), json(R"({ "settings": [
                { "key": "enabled", "type": "toggle", "default": "OFF" },
                { "key": "screen", "type": "list_single", "options": ["Arcade", "List"], "default": "Arcade" },
                { "key": "start_filter", "type": "list_single", "options_source": "dynamic" },
                { "key": "menu_filters", "type": "multiselect_submenu" },
                { "key": "play_at_startup", "type": "list_single", "options_source": "dynamic" },
                { "key": "border", "type": "toggle", "default": "ON" },
                { "key": "achievements_account", "type": "module_view" },
                { "key": "emulator_video", "type": "submenu", "settings": [
                    { "key": "vertical_monitor", "type": "list_single", "options": ["Horizontal", "Turned Right"] } ] } ] })") }
        };
        const setupexport::Form form = setupexport::formOf(extra, manifests);
        QCOMPARE(form.app.value(QStringLiteral("monitor")).toString(), QStringLiteral("Horizontal"));
        QVERIFY(form.app.contains(QStringLiteral("osd_theme")));
        const auto arcade = form.modules.value(QStringLiteral("com.osdos.arcade"));
        QCOMPARE(arcade.value(QStringLiteral("enabled")), QJsonValue(false));
        QCOMPARE(arcade.value(QStringLiteral("screen")).toString(), QStringLiteral("Arcade"));
        QCOMPARE(arcade.value(QStringLiteral("start_filter")).toString(), QStringLiteral("all"));
        QCOMPARE(arcade.value(QStringLiteral("border")), QJsonValue(true));
        QCOMPARE(arcade.value(QStringLiteral("vertical_monitor")).toString(), QStringLiteral("Horizontal"));
        QVERIFY(arcade.contains(QStringLiteral("menu_filters")));
        QVERIFY(arcade.contains(QStringLiteral("online_name")));
        // A list the page has no choices of, and a page of the app's.
        QVERIFY(!arcade.contains(QStringLiteral("play_at_startup")));
        QVERIFY(!arcade.contains(QStringLiteral("achievements_account")));

        // The page's own rows, as the app has them built in, take every row
        // the page's form has of OSD/OS's own.
        QFile real(QStringLiteral(SOURCE_DIR "/site/setup/schema-extra.json"));
        QVERIFY(real.open(QIODevice::ReadOnly));
        const setupexport::Form all = setupexport::formOf(QJsonDocument::fromJson(real.readAll()).object(), {});
        QVERIFY(all.app.contains(QStringLiteral("monitor")));
        QVERIFY(all.app.contains(QStringLiteral("color_scheme")));
    }

    // The page's file: OSD/OS's settings on the form that aren't at their
    // defaults, the account, and the system.
    void build() {
        setupexport::Form form;
        form.app = { { QStringLiteral("monitor"), QStringLiteral("Horizontal") },
                     { QStringLiteral("hint_bar"), QStringLiteral("On") },
                     { QStringLiteral("osd_theme"), QJsonValue() } };
        form.modules.insert(QStringLiteral("com.osdos.arcade"),
                            { { QStringLiteral("enabled"), false }, { QStringLiteral("border"), true },
                              { QStringLiteral("screen"), QStringLiteral("Arcade") } });
        const QJsonObject config = json(R"({
            "app": { "monitor": "Turned Right", "hint_bar": "On", "osd_theme": "orb", "volume": 80 },
            "modules": {
                "com.osdos.arcade": { "enabled": true, "border": "ON", "screen": "List", "engines": { "sf2": "mame" } },
                "com.osdos.plex": { "enabled": true } } })");
        setupexport::System system;
        system.name = QStringLiteral("cabinet");
        system.model = QStringLiteral("pi5");
        system.preset = QStringLiteral("jamma-240p");
        system.wifiCountry = QStringLiteral("TR");
        system.ssid = QStringLiteral("Home");
        system.wifiPassword = QStringLiteral("secret pass");
        system.timezone = QStringLiteral("Europe/Istanbul");
        system.keyboard = QStringLiteral("tr");
        system.ssh = 1;
        system.sshKeys = { QStringLiteral("ssh-ed25519 AAAAC3Nza me@pc") };
        const QJsonObject account = json(R"({ "user": "player", "token": "tok123", "key": "Key9" })");

        const QJsonObject setup = setupexport::build(config, form, account, system);
        QCOMPARE(setup.value(QStringLiteral("osdos_setup")).toInt(), 1);
        QCOMPARE(setup.value(QStringLiteral("device")).toObject(),
                 json(R"({ "name": "cabinet", "model": "pi5" })"));
        QCOMPARE(setup.value(QStringLiteral("display")).toObject(), json(R"({ "preset": "jamma-240p" })"));
        QCOMPARE(setup.value(QStringLiteral("wifi")).toObject(),
                 json(R"({ "country": "TR", "ssid": "Home", "password": "secret pass", "hidden": false })"));
        QCOMPARE(setup.value(QStringLiteral("system")).toObject(),
                 json(R"({ "timezone": "Europe/Istanbul", "keyboard": "tr",
                           "ssh": { "enabled": true, "password_login": false,
                                    "authorized_keys": ["ssh-ed25519 AAAAC3Nza me@pc"] } })"));
        QCOMPARE(setup.value(QStringLiteral("retroachievements")).toObject(), account);
        // At their defaults (On, and ON for true), not on the form (volume,
        // engines), or a module the page hasn't: left out.
        QCOMPARE(setup.value(QStringLiteral("config")).toObject(),
                 json(R"({ "app": { "monitor": "Turned Right", "osd_theme": "orb" },
                           "modules": { "com.osdos.arcade": { "enabled": true, "screen": "List" } } })"));

        // A computer's: its settings and the account alone.
        const QJsonObject computer = setupexport::build(config, form, QJsonObject(), setupexport::System());
        QCOMPARE(QStringList(computer.keys()), QStringList({ QStringLiteral("config"), QStringLiteral("osdos_setup") }));
        // The board only with the output; SSH off without its keys.
        setupexport::System off;
        off.model = QStringLiteral("pi4");
        off.ssh = 0;
        off.sshKeys = system.sshKeys;
        const QJsonObject offSetup = setupexport::build(QJsonObject(), form, QJsonObject(), off);
        QVERIFY(!offSetup.contains(QStringLiteral("device")));
        QCOMPARE(offSetup.value(QStringLiteral("system")).toObject(),
                 json(R"({ "ssh": { "enabled": false, "password_login": false } })"));
    }

    // The time zone, the keyboard and SSH, from Linux's files.
    void system() {
        QTemporaryDir root;
        QTemporaryDir home;
        QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/etc")));
        QVERIFY(QFile::link(QStringLiteral("/usr/share/zoneinfo/Europe/Istanbul"), root.path() + QStringLiteral("/etc/localtime")));
        writeFile(root.path() + QStringLiteral("/etc/default/keyboard"), "XKBMODEL=\"pc105\"\nXKBLAYOUT=\"tr,us\"\n");
        setupexport::System system;
        setupexport::readSystem(system, root.path(), home.path());
        QCOMPARE(system.timezone, QStringLiteral("Europe/Istanbul"));
        QCOMPARE(system.keyboard, QStringLiteral("tr"));
        // No SSH there to turn on.
        QCOMPARE(system.ssh, -1);

        writeFile(root.path() + QStringLiteral("/usr/lib/systemd/system/ssh.service"), "[Unit]\n");
        setupexport::readSystem(system, root.path(), home.path());
        QCOMPARE(system.ssh, 0);
        QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/etc/systemd/system/multi-user.target.wants")));
        QVERIFY(QFile::link(QStringLiteral("/usr/lib/systemd/system/ssh.service"),
                            root.path() + QStringLiteral("/etc/systemd/system/multi-user.target.wants/ssh.service")));
        writeFile(root.path() + QStringLiteral("/etc/ssh/sshd_config.d/00-osdos-setup.conf"),
                  "# OSD/OS Setup: pi's password logs in too.\nPasswordAuthentication yes\n");
        writeFile(home.path() + QStringLiteral("/.ssh/authorized_keys"),
                  "# mine\nssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI me@pc\n\nnot a key\n"
                  "ecdsa-sha2-nistp256 AAAAE2VjZHNh= phone\n");
        system = setupexport::System();
        setupexport::readSystem(system, root.path(), home.path());
        QCOMPARE(system.ssh, 1);
        QVERIFY(system.sshPasswordLogin);
        QCOMPARE(system.sshKeys, QStringList({ QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI me@pc"),
                                               QStringLiteral("ecdsa-sha2-nistp256 AAAAE2VjZHNh= phone") }));

        // /etc/timezone where /etc/localtime isn't a link.
        QVERIFY(QFile::remove(root.path() + QStringLiteral("/etc/localtime")));
        writeFile(root.path() + QStringLiteral("/etc/timezone"), "America/New_York\n");
        setupexport::readSystem(system, root.path(), home.path());
        QCOMPARE(system.timezone, QStringLiteral("America/New_York"));
    }

    // The capacities: as many bytes as a code of each version holds at the
    // lowest error correction, and not one more.
    void capacities() {
        for (int version = 1; version <= 40; ++version) {
            const int bytes = setupexport::capacity(version);
            const auto fits = [version](int size) {
                try {
                    qrcodegen::QrCode::encodeSegments({ qrcodegen::QrSegment::makeBytes(std::vector<std::uint8_t>(size, 'a')) },
                                                      qrcodegen::QrCode::Ecc::LOW, 1, version, -1, false);
                    return true;
                } catch (const qrcodegen::data_too_long &) {
                    return false;
                }
            };
            QVERIFY2(fits(bytes), qPrintable(QString::number(version)));
            QVERIFY2(!fits(bytes + 1), qPrintable(QString::number(version)));
        }
        QCOMPARE(setupexport::capacity(0), 0);
        QCOMPARE(setupexport::capacity(41), 0);
    }

    // The links: the page's address, the part, the parts and the file's id,
    // then its text; each no longer than asked, together the file whole.
    void links() {
        QJsonObject setup = json(R"({ "osdos_setup": 1, "wifi": { "ssid": "Home", "password": "secret" } })");
        QJsonObject app;
        for (int i = 0; i < 60; ++i)
            app.insert(QStringLiteral("setting_%1").arg(i), QStringLiteral("value %1 %2").arg(i).arg(i * 7919 % 1000));
        setup.insert(QStringLiteral("config"), QJsonObject{ { QStringLiteral("app"), app } });

        const QStringList one = setupexport::links(setup, setupexport::capacity(40));
        QCOMPARE(one.size(), 1);
        QVERIFY(one.first().startsWith(setupexport::kPage + QStringLiteral("#s=1.1.")));
        QCOMPARE(joined(one), setup);

        const int small = setupexport::capacity(10);
        const QStringList parts = setupexport::links(setup, small);
        QVERIFY(parts.size() > 1);
        const QString id = parts.first().section(QLatin1Char('.'), 3, 3);
        QCOMPARE(id.size(), 6);
        for (int i = 0; i < parts.size(); ++i) {
            QVERIFY(parts[i].size() <= small);
            QVERIFY(parts[i].startsWith(setupexport::kPage + QStringLiteral("#s=%1.%2.%3.").arg(i + 1).arg(parts.size()).arg(id)));
            // Drawn as a code of no more than that version.
            const QImage code = setupexport::codeImage(parts[i]);
            QVERIFY(!code.isNull());
            QVERIFY(code.width() <= 17 + 4 * 10 + 4);
        }
        QCOMPARE(joined(parts), setup);
        // Another file, another id.
        setup.insert(QStringLiteral("osdos_setup"), 2);
        QVERIFY(setupexport::links(setup, small).first().section(QLatin1Char('.'), 3, 3) != id);
        // Too small for even the address.
        QVERIFY(setupexport::links(setup, 20).isEmpty());
    }

    // A code: a module a pixel, dark on white, the quiet zone white.
    void code() {
        const QImage code = setupexport::codeImage(QStringLiteral("https://osdos.tv/setup/#s=1.1.abcdef.x"), 2);
        QCOMPARE(code.width(), code.height());
        QCOMPARE((code.width() - 4 - 17) % 4, 0);
        QCOMPARE(qGray(code.pixel(0, 0)), 255);
        QCOMPARE(qGray(code.pixel(1, 1)), 255);
        // A finder pattern's corner, dark.
        QCOMPARE(qGray(code.pixel(2, 2)), 0);
        // Nothing holds this much.
        QVERIFY(setupexport::codeImage(QString(4000, QLatin1Char('x'))).isNull());
    }
};

QTEST_GUILESS_MAIN(SetupExportTest)
#include "setup_export_test.moc"
