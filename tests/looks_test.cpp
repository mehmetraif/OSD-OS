#include "AppCore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

// Skins and themes (AppCore::skins(), skin(), themes(), theme()): read from
// the app's folder (assets) and the data folder's, the data folder's first.
// A skin dresses the window; a theme is the whole look, a skin among it.
// And the licences About shows (AppCore::licenseText()), read from the app's
// folder alone.
class LooksTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_app;
    QTemporaryDir m_data;

    // A folder of a look with its JSON, under root ("<app>/assets" or the
    // data folder): in its kind's folder ("skins", "themes"), in a file of
    // the JSON's name ("skin.json", "theme.json").
    static QString writeLook(const QString &root, const QString &folder, const QString &file,
                             const QString &id, const QByteArray &json) {
        const QString dir = root + "/" + folder + "/" + id;
        if (!QDir().mkpath(dir))
            return {};
        QFile f(dir + "/" + file);
        if (!f.open(QIODevice::WriteOnly) || f.write(json) != json.size())
            return {};
        return dir;
    }
    static QString writeSkin(const QString &root, const QString &id, const QByteArray &json) {
        return writeLook(root, "skins", "skin.json", id, json);
    }
    static QString writeTheme(const QString &root, const QString &id, const QByteArray &json) {
        return writeLook(root, "themes", "theme.json", id, json);
    }
    // JSON written with ' for ": moc can't read raw string literals.
    static QByteArray json(const char *text) { return QByteArray(text).replace('\'', '"'); }
    static bool writeFile(const QString &path, const QByteArray &data) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    // A picture a skin can use: a 1×1 PNG.
    static QByteArray png() {
        return QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==");
    }
    static QString url(const QString &path) { return QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath()).toString(); }
    static QStringList names(const QVariantList &looks) {
        QStringList list;
        for (const QVariant &v : looks)
            list.append(v.toMap().value("name").toString());
        return list;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_app.isValid());
        QVERIFY(m_data.isValid());
        const QString assets = m_app.path() + "/assets";
        // The app's own: a skin, and themes.
        const QString dos = writeSkin(assets, "dos", json("{ 'name': 'DOS', 'window': { 'image': 'window.png', 'border': 4 } }"));
        QVERIFY(!dos.isEmpty());
        QVERIFY(writeFile(dos + "/window.png", png()));
        const QString trinitron = writeTheme(assets, "trinitron", json("{ "
            "'name': 'Trinitron', 'colors': 'Video 1', 'skin': 'dos', "
            "'effects': { 'screen': 'CRT', 'transition': 'Cube' }, 'music': 'tune.ogg' }"));
        QVERIFY(!trinitron.isEmpty());
        QVERIFY(writeFile(trinitron + "/tune.ogg", "a tune"));
        QVERIFY(!writeTheme(assets, "plain", json("{ 'name': 'Plain', 'colors': null, 'effects': { 'screen': 'Off' } }")).isEmpty());
        // Music of every kind the menu music plays: a tracker's module, MIDI.
        const QString chip = writeTheme(assets, "chip", json("{ 'name': 'Chip', 'music': 'chip.xm' }"));
        QVERIFY(!chip.isEmpty());
        QVERIFY(writeFile(chip + "/chip.xm", "Extended Module: "));
        const QString organ = writeTheme(assets, "organ", json("{ 'name': 'Organ', 'music': 'Organ.MID' }"));
        QVERIFY(!organ.isEmpty());
        QVERIFY(writeFile(organ + "/Organ.MID", "MThd"));
        QVERIFY(!writeTheme(assets, "kept", json("{ 'name': 'Kept' }")).isEmpty());

        // The data folder's skins: one of its own, every part of it, some
        // that can't be used; and one made before skins had their name, a
        // theme.json of window pictures in the data folder's themes.
        const QString mine = writeSkin(m_data.path(), "mine", json("{ "
            "'name': 'Mine', "
            "'window': { 'image': 'frame.png', 'border': [1, 2, 3, 4], 'tile': 'repeat' }, "
            "'titleBar': '../escape.png', "
            "'hintBar': 'link.png', "
            "'selection': { 'image': 'frame.png', 'border': 'wide' }, "
            "'icons': { 'youtube': 'yt.svg', 'Plex': 'plex.png', '../x': 'plex.png', "
                       "'settings': '../escape.png', 'weather': 'notes.txt' } "
        "}"));
        QVERIFY(!mine.isEmpty());
        QVERIFY(writeFile(mine + "/frame.png", png()));
        QVERIFY(writeFile(mine + "/plex.png", png()));
        QVERIFY(writeFile(mine + "/notes.txt", "not a picture"));
        QVERIFY(writeFile(mine + "/yt.svg", "<svg xmlns='http://www.w3.org/2000/svg'/>"));
        QVERIFY(writeFile(m_data.path() + "/skins/escape.png", png()));
        QVERIFY(QFile::link(dos + "/window.png", mine + "/link.png"));
        const QString early = writeTheme(m_data.path(), "early", json("{ 'name': 'Early', 'window': 'frame.png' }"));
        QVERIFY(!early.isEmpty());
        QVERIFY(writeFile(early + "/frame.png", png()));

        // The data folder's themes: one in place of the app's, one not JSON
        // (the app's of its name in its place), one of its own in every way,
        // one wrong in every way.
        QVERIFY(!writeTheme(m_data.path(), "trinitron", json("{ 'name': 'My Trinitron', 'colors': 'Amber' }")).isEmpty());
        QVERIFY(!writeTheme(m_data.path(), "kept", "{ nope").isEmpty());
        const QString own = writeTheme(m_data.path(), "own", json("{ "
            "'name': 'Own', "
            "'colors': { 'primary': '#FFD0A0', 'surface': '#402010', 'accent': '#123456' }, "
            "'skin': { 'window': { 'image': 'frame.png', 'border': 2 }, 'icons': { 'logo': 'logo.png' } }, "
            "'effects': { "
                "'text': { 'rainbow': 0.5, 'glow': 7, 'flicker': -1, 'shimmer': 'lots', 'scanlines': 1 }, "
                "'background': { 'shader': 'rain.frag.qsb', 'area': 'foot', 'animate': true }, "
                "'selector': 'Welding', "
                "'screen': { 'scanlines': 0.4, 'rainbow': 1, 'animate': true, 'shader': 'crt.frag.qsb' }, "
                "'transition': 'Ripple' }, "
            "'music': 'tune.opus' "
        "}"));
        QVERIFY(!own.isEmpty());
        QVERIFY(writeFile(own + "/frame.png", png()));
        QVERIFY(writeFile(own + "/logo.png", png()));
        QVERIFY(writeFile(own + "/rain.frag.qsb", "not compiled, but a .qsb in its folder"));
        QVERIFY(writeFile(own + "/crt.frag.qsb", "not compiled, but a .qsb in its folder"));
        QVERIFY(writeFile(own + "/tune.opus", "a tune"));
        QVERIFY(!writeTheme(m_data.path(), "wrong", json("{ "
            "'colors': { 'primary': 'white', 'surface': '#000' }, "
            "'skin': 42, "
            "'effects': { 'text': 5, 'background': { 'area': 'sideways' }, "
                         "'screen': { 'shader': '../own/crt.frag.qsb' } }, "
            "'music': '../own/tune.opus' "
        "}")).isEmpty());
    }

    // By name, each once, the data folder's in place of the app's: an early
    // skin among the skins, not the themes.
    void skinsListed() {
        AppCore core(m_app.path(), m_data.path());
        QCOMPARE(names(core.skins()), QStringList({ "DOS", "Early", "Mine" }));
    }

    // A skin's parts and icons, from its folder only; no colours, the
    // scheme's stay.
    void skinOfItsOwn() {
        AppCore core(m_app.path(), m_data.path());
        const QVariantMap skin = core.skin("mine");
        const QString dir = m_data.path() + "/skins/mine";
        QCOMPARE(skin.value("id").toString(), QString("mine"));
        QCOMPARE(skin.value("name").toString(), QString("Mine"));
        QVERIFY(!skin.contains("colors"));
        const QVariantMap window = skin.value("window").toMap();
        QCOMPARE(window.value("source").toString(), url(dir + "/frame.png"));
        QCOMPARE(window.value("border").toList(), QVariantList({ 1, 2, 3, 4 }));
        QCOMPARE(window.value("tile").toString(), QString("repeat"));
        // A border that isn't one: stretched whole.
        QCOMPARE(skin.value("selection").toMap().value("border").toList(), QVariantList({ 0, 0, 0, 0 }));
        // Out of its folder, by a path or a link: refused.
        QVERIFY(!skin.contains("titleBar"));
        QVERIFY(!skin.contains("hintBar"));
        // Icons by name, the name in lower case; one not a name, one out of
        // its folder and one not a picture left out.
        const QVariantMap icons = skin.value("icons").toMap();
        QCOMPARE(icons.keys(), QStringList({ "plex", "youtube" }));
        QCOMPARE(icons.value("youtube").toString(), url(dir + "/yt.svg"));
        QCOMPARE(icons.value("plex").toString(), url(dir + "/plex.png"));
    }

    // A skin from before skins had their name, read as one.
    void earlySkin() {
        AppCore core(m_app.path(), m_data.path());
        const QVariantMap skin = core.skin("early");
        QCOMPARE(skin.value("name").toString(), QString("Early"));
        QCOMPARE(skin.value("window").toMap().value("source").toString(),
                 url(m_data.path() + "/themes/early/frame.png"));
        QVERIFY(core.theme("early").isEmpty());
    }

    // By name, each once, the data folder's in place of the app's; one not
    // JSON left out for the app's of its name.
    void themesListed() {
        AppCore core(m_app.path(), m_data.path());
        QCOMPARE(names(core.themes()), QStringList({ "Chip", "Kept", "My Trinitron", "Organ", "Own", "Plain", "wrong" }));
        QCOMPARE(core.theme("kept").value("name").toString(), QString("Kept"));
    }

    // Names of a scheme, a skin and presets: the skin read, the others for
    // Main.qml to look up.
    void themeOfNames() {
        // Without the data folder's in its place.
        AppCore core(m_app.path(), m_app.path() + "/nothing");
        const QVariantMap theme = core.theme("trinitron");
        QCOMPARE(theme.value("id").toString(), QString("trinitron"));
        QCOMPARE(theme.value("name").toString(), QString("Trinitron"));
        QCOMPARE(theme.value("colors").toString(), QString("Video 1"));
        const QVariantMap skin = theme.value("skin").toMap();
        QCOMPARE(skin.value("id").toString(), QString("dos"));
        QCOMPARE(skin.value("window").toMap().value("border").toList(), QVariantList({ 4, 4, 4, 4 }));
        const QVariantMap effects = theme.value("effects").toMap();
        QCOMPARE(effects.keys(), QStringList({ "screen", "transition" }));
        QCOMPARE(effects.value("screen").toString(), QString("CRT"));
        QCOMPARE(effects.value("transition").toString(), QString("Cube"));
        QCOMPARE(theme.value("music").toString(), url(m_app.path() + "/assets/themes/trinitron/tune.ogg"));
    }

    // Its own colours, skin, effects and music, from its folder only.
    void themeOfItsOwn() {
        AppCore core(m_app.path(), m_data.path());
        const QVariantMap theme = core.theme("own");
        const QString dir = m_data.path() + "/themes/own";
        QCOMPARE(theme.value("colors").toMap(), QVariantMap({ { "primary", "#FFD0A0" }, { "surface", "#402010" } }));

        const QVariantMap skin = theme.value("skin").toMap();
        QCOMPARE(skin.value("window").toMap().value("source").toString(), url(dir + "/frame.png"));
        QCOMPARE(skin.value("window").toMap().value("border").toList(), QVariantList({ 2, 2, 2, 2 }));
        QCOMPARE(skin.value("icons").toMap().value("logo").toString(), url(dir + "/logo.png"));

        const QVariantMap effects = theme.value("effects").toMap();
        // Each kind's own numbers, within bounds; one not a number, and
        // another kind's, left out.
        QCOMPARE(effects.value("text").toMap(),
                 QVariantMap({ { "rainbow", 0.5 }, { "glow", 1.0 }, { "flicker", 0.0 } }));
        QCOMPARE(effects.value("background").toMap(),
                 QVariantMap({ { "shader", url(dir + "/rain.frag.qsb") }, { "area", "foot" }, { "animate", true } }));
        QCOMPARE(effects.value("selector").toString(), QString("Welding"));
        QCOMPARE(effects.value("screen").toMap(),
                 QVariantMap({ { "scanlines", 0.4 }, { "animate", true }, { "shader", url(dir + "/crt.frag.qsb") } }));
        QCOMPARE(effects.value("transition").toString(), QString("Ripple"));
        QCOMPARE(theme.value("music").toString(), url(dir + "/tune.opus"));
    }

    // A module's and a MIDI file's music too, of any case.
    void themeMusicKinds() {
        AppCore core(m_app.path(), m_app.path() + "/nothing");
        QCOMPARE(core.theme("chip").value("music").toString(), url(m_app.path() + "/assets/themes/chip/chip.xm"));
        QCOMPARE(core.theme("organ").value("music").toString(), url(m_app.path() + "/assets/themes/organ/Organ.MID"));
    }

    // What can't be used is there, empty: Video 1's colours, OSD/OS's own
    // window, no effect. Null is as left out.
    void themeWrong() {
        AppCore core(m_app.path(), m_data.path());
        const QVariantMap wrong = core.theme("wrong");
        QCOMPARE(wrong.value("name").toString(), QString("wrong"));
        QVERIFY(wrong.contains("colors"));
        QVERIFY(wrong.value("colors").toMap().isEmpty());
        QVERIFY(wrong.contains("skin"));
        QVERIFY(wrong.value("skin").toMap().isEmpty());
        const QVariantMap effects = wrong.value("effects").toMap();
        QCOMPARE(effects.keys(), QStringList({ "background", "screen", "text" }));
        for (const QVariant &effect : effects)
            QVERIFY(effect.toMap().isEmpty());
        // Out of its folder.
        QVERIFY(!wrong.contains("music"));

        const QVariantMap plain = core.theme("plain");
        QVERIFY(!plain.contains("colors"));
        QVERIFY(!plain.contains("skin"));
        QVERIFY(!plain.contains("music"));
        QCOMPARE(plain.value("effects").toMap(), QVariantMap({ { "screen", "Off" } }));
    }

    // An id is a folder's name, nothing else.
    // LICENSE, or a file of the app's by its path, its paragraphs each on a
    // line; nothing outside the app's folder.
    void licenses() {
        QVERIFY(QDir().mkpath(m_app.filePath("assets/licenses")));
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write(m_app.filePath("LICENSE"), "GNU GENERAL PUBLIC LICENSE\n   Version 3\n\nPreamble\n");
        write(m_app.filePath("assets/licenses/zlib.txt"), "zlib\n\nThis software is provided\n'as-is'.\n");
        write(m_data.filePath("secret.txt"), "secret");
        AppCore core(m_app.path(), m_data.path());
        QCOMPARE(core.licenseText(), QStringLiteral("GNU GENERAL PUBLIC LICENSE Version 3\n\nPreamble"));
        QCOMPARE(core.licenseText("assets/licenses/zlib.txt"), QStringLiteral("zlib\n\nThis software is provided 'as-is'."));
        QCOMPARE(core.licenseText("assets/licenses/none.txt"), QString());
        const QString outside = QDir(m_app.path()).relativeFilePath(m_data.filePath("secret.txt"));
        QVERIFY(outside.startsWith(QLatin1String("..")));
        QCOMPARE(core.licenseText(outside), QString());
        QCOMPARE(core.licenseText("assets/../" + outside), QString());
        QCOMPARE(core.licenseText(m_data.filePath("secret.txt")), QString());
    }

    void ids() {
        AppCore core(m_app.path(), m_data.path());
        for (const QString &id : { QString(), QString("."), QString(".."), QString("../themes/own"),
                                   QString("own/"), QString("gone") }) {
            QVERIFY2(core.theme(id).isEmpty(), qPrintable(id));
            QVERIFY2(core.skin(id).isEmpty(), qPrintable(id));
        }
        QCOMPARE(core.theme("own").value("name").toString(), QString("Own"));
        QCOMPARE(core.skin("mine").value("name").toString(), QString("Mine"));
    }
};

QTEST_GUILESS_MAIN(LooksTest)
#include "looks_test.moc"
