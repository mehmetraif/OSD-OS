#include "Looks.h"
#include "FileText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>
#include <algorithm>

namespace {

// A colour scheme of the user's, its five colours each #rrggbb; empty, with a
// line in the log, for one that can't be used.
QVariantMap importColorScheme(const QJsonObject &obj) {
    static const QStringList kRequiredKeys = {"primary","secondary","tertiary","surface","accent"};
    static const QRegularExpression kHexColor("^#[0-9A-Fa-f]{6}$");

    QVariantMap result;
    for (const QString &key : kRequiredKeys) {
        if (!obj.contains(key) || !obj[key].isString()) {
            qWarning("[AppCore] custom_color_scheme.json: missing or non-string key '%s'", qPrintable(key));
            return {};
        }
        QString value = obj[key].toString();
        if (!kHexColor.match(value).hasMatch()) {
            qWarning("[AppCore] custom_color_scheme.json: invalid hex color for '%s': %s",
                     qPrintable(key), qPrintable(value));
            return {};
        }
        result[key] = value;
    }
    return result;
}

// A theme.json of window pictures only, a skin made before skins had their
// name (in the data folder's themes): read as a skin, not as a theme.
bool isEarlySkin(const QJsonObject &obj) {
    for (const char *key : { "colors", "skin", "effects", "music" }) {
        if (obj.contains(QLatin1String(key)))
            return false;
    }
    for (const char *key : { "window", "titleBar", "hintBar", "selection" }) {
        if (obj.contains(QLatin1String(key)))
            return true;
    }
    return false;
}

// A folder looks of a kind are in: its JSON's name there, and which of its
// theme.json files it takes (an early skin's or a theme's).
struct LookRoot {
    enum Take { Any, EarlySkins, NoEarlySkins };
    QString dir;
    QString file;
    Take take;
};

// Where the looks of a kind ("skin", "theme") are, the data folder's first:
// one there takes the place of the app's of the same folder name. Skins are
// in the data folder's themes too, where they were before they had their name.
QList<LookRoot> lookRoots(const QString &appRoot, const QString &dataRoot, const QString &kind) {
    if (kind == QLatin1String("skin"))
        return { { dataRoot + QStringLiteral("/skins"), QStringLiteral("skin.json"), LookRoot::Any },
                 { dataRoot + QStringLiteral("/themes"), QStringLiteral("theme.json"), LookRoot::EarlySkins },
                 { appRoot + QStringLiteral("/assets/skins"), QStringLiteral("skin.json"), LookRoot::Any } };
    return { { dataRoot + QStringLiteral("/themes"), QStringLiteral("theme.json"), LookRoot::NoEarlySkins },
             { appRoot + QStringLiteral("/assets/themes"), QStringLiteral("theme.json"), LookRoot::NoEarlySkins } };
}

// A look's JSON in its folder, read as an object; false (and a line in the log
// for one that is there but not JSON) when it can't be, or isn't of the kind
// the folder it is in takes.
bool readLookJson(const LookRoot &root, const QString &id, QJsonObject *out) {
    const QString path = root.dir + QLatin1Char('/') + id + QLatin1Char('/') + root.file;
    QJsonObject obj;
    QString why;
    if (!filetext::readJsonObject(path, &obj, &why)) {
        if (!why.isEmpty())
            qWarning("[AppCore] %s: %s", qPrintable(path), qPrintable(why));
        return false;
    }
    if ((root.take == LookRoot::EarlySkins && !isEarlySkin(obj))
            || (root.take == LookRoot::NoEarlySkins && isEarlySkin(obj)))
        return false;
    *out = obj;
    return true;
}

// The look's name, or its folder's when it gives none, or one too long for a
// line of Settings.
QString lookName(const QJsonObject &obj, const QString &id) {
    const QString name = obj.value(QStringLiteral("name")).toString().simplified();
    return name.isEmpty() || name.size() > 28 ? id : name;
}

// [{ id, name }] for Settings, by name.
QVariantList listByName(const QMap<QString, QJsonObject> &found) {
    QVariantList list;
    for (auto it = found.cbegin(); it != found.cend(); ++it)
        list.append(QVariantMap{ { "id", it.key() }, { "name", lookName(it.value(), it.key()) } });
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value("name").toString(), b.toMap().value("name").toString(),
                                Qt::CaseInsensitive) < 0;
    });
    return list;
}

// A file of the look's own, as a URL: a path in its folder (none out of it,
// through a link either), of one of these types. Empty, with a line in the
// log for one named but not found or not of these, when there is none. look
// names it there: "skin dos", "theme matrix".
QString lookFile(const QDir &dir, const QJsonValue &value, const QStringList &types,
                 const QString &look, const QString &what) {
    if (!value.isString())
        return {};
    const QString path = QFileInfo(dir.filePath(value.toString())).canonicalFilePath();
    const QFileInfo file(path);
    if (path.isEmpty() || !path.startsWith(dir.canonicalPath() + QLatin1Char('/')) || !file.isFile()
            || !types.contains(file.suffix().toLower())) {
        qWarning("[AppCore] %s: its %s, \"%s\", is not a %s file in its folder", qPrintable(look),
                 qPrintable(what), qPrintable(value.toString()), qPrintable(types.join(QLatin1Char('/'))));
        return {};
    }
    return QUrl::fromLocalFile(path).toString();
}

// One of a skin's pictures, for QML: "file.png", or { "image": "file.png",
// "border": 4 or [left, top, right, bottom], "tile": "stretch" | "repeat" |
// "round" }, read as { source, border: [left, top, right, bottom], tile }. A
// PNG, GIF or BMP, whose pixels the colour scheme's two colours take. Empty
// for none.
QVariantMap skinImage(const QDir &dir, const QJsonValue &value, const QString &look, const char *what) {
    static const QStringList types = { "png", "gif", "bmp" };
    const QJsonObject obj = value.isObject() ? value.toObject()
                                             : QJsonObject{{QStringLiteral("image"), value}};
    const QString source = lookFile(dir, obj.value(QStringLiteral("image")), types, look, QLatin1String(what));
    if (source.isEmpty())
        return {};
    // In the picture's own pixels, the art pixels it is drawn on.
    auto side = [](const QJsonValue &v) { return qBound(0, v.toInt(), 512); };
    QVariantList border = { 0, 0, 0, 0 };
    const QJsonValue b = obj.value(QStringLiteral("border"));
    const QJsonArray sides = b.toArray();
    if (b.isDouble()) {
        border = { side(b), side(b), side(b), side(b) };
    } else if (sides.size() == 4 && std::all_of(sides.begin(), sides.end(),
                                                [](const QJsonValue &v) { return v.isDouble(); })) {
        border = { side(sides[0]), side(sides[1]), side(sides[2]), side(sides[3]) };
    } else if (!b.isUndefined()) {
        qWarning("[AppCore] %s: its %s's border is not a number or four: drawn whole, stretched",
                 qPrintable(look), what);
    }
    static const QStringList tiles = { "stretch", "repeat", "round" };
    const QString tile = obj.value(QStringLiteral("tile")).toString();
    return { { "source", source }, { "border", border },
             { "tile", tiles.contains(tile) ? tile : QStringLiteral("stretch") } };
}

// What a skin dresses, from its folder (a theme's own skin, from the
// theme's): a picture for any of { window, titleBar, hintBar, selection }, and
// icons { name: URL }, each drawn in place of OSD/OS's of that name (a module's
// by its folder's name: "youtube"), those there are that can be used.
QVariantMap skinParts(const QDir &dir, const QJsonObject &obj, const QString &look) {
    static const QStringList iconTypes = { "png", "svg", "gif", "bmp", "jpg", "jpeg" };
    static const QRegularExpression iconName(QStringLiteral("^[a-z0-9_-]{1,40}$"));
    QVariantMap parts;
    for (const char *key : { "window", "titleBar", "hintBar", "selection" }) {
        const QVariantMap part = skinImage(dir, obj.value(QLatin1String(key)), look, key);
        if (!part.isEmpty())
            parts[key] = part;
    }
    const QJsonObject icons = obj.value(QStringLiteral("icons")).toObject();
    QVariantMap iconUrls;
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        const QString name = it.key().toLower();
        if (!iconName.match(name).hasMatch()) {
            qWarning("[AppCore] %s: \"%s\" is not an icon's name", qPrintable(look), qPrintable(it.key()));
            continue;
        }
        const QString url = lookFile(dir, it.value(), iconTypes, look, QStringLiteral("icon ") + name);
        if (!url.isEmpty())
            iconUrls[name] = url;
    }
    if (!iconUrls.isEmpty())
        parts[QStringLiteral("icons")] = iconUrls;
    return parts;
}

// A theme's own colours: { "primary": "#rrggbb", "surface": "#rrggbb" }, the
// two everything is drawn in (a colour scheme's other three may be there
// too, unused). Empty, with a line in the log, when they aren't both there.
QVariantMap themeColors(const QJsonValue &value, const QString &look) {
    static const QRegularExpression kHexColor("^#[0-9A-Fa-f]{6}$");
    const QJsonObject obj = value.toObject();
    QVariantMap colors;
    for (const char *key : { "primary", "surface" }) {
        const QString color = obj.value(QLatin1String(key)).toString();
        if (!kHexColor.match(color).hasMatch()) {
            qWarning("[AppCore] %s: its colors are not a color scheme's name or a #rrggbb primary and surface:"
                     " drawn in Video 1's", qPrintable(look));
            return {};
        }
        colors[key] = color;
    }
    return colors;
}

// One of a theme's effects: a preset's name ("CRT", "Matrix", "Off"), or its
// own: numbers, each 0 to 1 (knobs: those this kind takes, none when left
// out), "animate": true for a shader that moves, and "shader", a .qsb in its
// folder; a background's "area" too, "window" (as left out) or "foot", along
// the window's foot and up into it from below, as a fire burns. Read with
// each number within bounds and the shader as a URL.
QVariant themeEffect(const QDir &dir, const QJsonValue &value, const QStringList &knobs,
                     const QString &look, const QString &kind) {
    if (value.isString())
        return value.toString();
    if (!value.isObject()) {
        qWarning("[AppCore] %s: its %s effect is not a name or an object: none", qPrintable(look),
                 qPrintable(kind));
        return QVariantMap();
    }
    const QJsonObject obj = value.toObject();
    QVariantMap effect;
    for (const QString &key : knobs) {
        const QJsonValue v = obj.value(key);
        if (v.isDouble())
            effect[key] = qBound(0.0, v.toDouble(), 1.0);
        else if (!v.isUndefined())
            qWarning("[AppCore] %s: its %s effect's %s is not a number from 0 to 1: none", qPrintable(look),
                     qPrintable(kind), qPrintable(key));
    }
    if (obj.value(QStringLiteral("animate")).toBool())
        effect["animate"] = true;
    if (kind == QLatin1String("background")) {
        const QJsonValue area = obj.value(QStringLiteral("area"));
        if (area.toString() == QLatin1String("foot"))
            effect["area"] = area.toString();
        else if (!area.isUndefined() && area.toString() != QLatin1String("window"))
            qWarning("[AppCore] %s: its background effect's area is not \"window\" or \"foot\": the window",
                     qPrintable(look));
    }
    const QString shader = lookFile(dir, obj.value(QStringLiteral("shader")), { "qsb" }, look,
                                    kind + QStringLiteral(" shader"));
    if (!shader.isEmpty())
        effect["shader"] = shader;
    return effect;
}

// The folder of the skin or the theme (kind: "skin", "theme") with this
// id whose skin.json or theme.json reads, the data folder's before the
// app's, and that JSON; empty when there is none (or the id isn't a
// folder's name).
QString lookDir(const QString &appRoot, const QString &dataRoot, const QString &kind, const QString &id,
                QJsonObject *json) {
    // A folder's name, no way up or across.
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String("..")
            || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\')))
        return {};
    for (const LookRoot &root : lookRoots(appRoot, dataRoot, kind)) {
        if (readLookJson(root, id, json))
            return root.dir + QLatin1Char('/') + id;
    }
    return {};
}

// The skins or the themes there are, id → its JSON, the first found of
// each id.
QMap<QString, QJsonObject> looksOf(const QString &appRoot, const QString &dataRoot, const QString &kind) {
    QMap<QString, QJsonObject> found;
    for (const LookRoot &root : lookRoots(appRoot, dataRoot, kind)) {
        const QStringList ids = QDir(root.dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : ids) {
            QJsonObject obj;
            if (!found.contains(id) && readLookJson(root, id, &obj))
                found.insert(id, obj);
        }
    }
    return found;
}

} // namespace

namespace looks {

QVariantMap customColorScheme(const QString &dataRoot) {
    QJsonObject obj;
    QString why;
    if (!filetext::readJsonObject(dataRoot + "/custom_color_scheme.json", &obj, &why)) {
        if (!why.isEmpty())
            qWarning("[AppCore] custom_color_scheme.json: %s", qPrintable(why));
        return {};
    }
    return importColorScheme(obj);
}

QVariantMap customColorSchemes(const QString &dataRoot) {
    static const QRegularExpression validThemeName("^[\\w\\d !#-/:-@\\[-_{-~]{3,28}$", QRegularExpression::CaseInsensitiveOption);

    QJsonObject obj;
    QString why;
    if (!filetext::readJsonObject(dataRoot + "/custom_color_schemes.json", &obj, &why)) {
        if (!why.isEmpty())
            qWarning("[AppCore] custom_color_schemes.json: %s", qPrintable(why));
        return {};
    }
    QVariantMap result;
    for (const QString &theme : obj.keys()) {
        if (!validThemeName.match(theme).hasMatch()) {
            qWarning("[AppCore] custom_color_schemes.json: invalid theme name '%s' detected - only 28 letters, numbers, and ASCII symbols (other than backtick and double-quote)",
                    qPrintable(theme));
            continue;
        }
        QJsonObject tObj = obj[theme].toObject();
        QVariantMap tResult = importColorScheme(tObj);
        if (tResult.count() == 5) {
            result[theme] = tResult;
            qDebug("[AppCore] custom_color_schemes.json: loaded '%s' custom theme", qPrintable(theme));
        }
    }
    return result;
}

QVariantList skins(const QString &appRoot, const QString &dataRoot) {
    return listByName(looksOf(appRoot, dataRoot, QStringLiteral("skin")));
}

QVariantMap skin(const QString &appRoot, const QString &dataRoot, const QString &id) {
    QJsonObject obj;
    const QString path = lookDir(appRoot, dataRoot, QStringLiteral("skin"), id, &obj);
    if (path.isEmpty()) {
        if (!id.isEmpty())
            qWarning("[AppCore] skin %s: not found, none used", qPrintable(id));
        return {};
    }
    // The window's parts it has a picture of, and its icons: the colours stay
    // the scheme's.
    QVariantMap skin = skinParts(QDir(path), obj, QStringLiteral("skin ") + id);
    skin["id"] = id;
    skin["name"] = lookName(obj, id);
    qInfo("[AppCore] skin %s: %s", qPrintable(id), qPrintable(path));
    return skin;
}

QVariantList themes(const QString &appRoot, const QString &dataRoot) {
    return listByName(looksOf(appRoot, dataRoot, QStringLiteral("theme")));
}

QVariantMap theme(const QString &appRoot, const QString &dataRoot, const QString &id) {
    static const QStringList textKnobs = { "rainbow", "shimmer", "flicker", "glow" };
    static const QStringList screenKnobs = { "scanlines", "curvature", "glow", "bleed", "noise", "vignette" };
    static const QStringList musicTypes = { "ogg", "opus", "mp3", "flac", "wav", "m4a", "aac",
                                            "mid", "midi", "xm", "mod", "s3m", "it" };
    QJsonObject obj;
    const QString path = lookDir(appRoot, dataRoot, QStringLiteral("theme"), id, &obj);
    if (path.isEmpty()) {
        if (!id.isEmpty())
            qWarning("[AppCore] theme %s: not found, none used", qPrintable(id));
        return {};
    }
    // Each part a colour scheme's, a skin's or an effect preset's name, or its
    // own, its files in its folder. One that can't be used is drawn as with
    // none of it: Video 1's colours, OSD/OS's own window, no effect.
    const QDir dir(path);
    const QString look = QStringLiteral("theme ") + id;
    QVariantMap theme = { { "id", id }, { "name", lookName(obj, id) } };
    const QJsonValue colors = obj.value(QStringLiteral("colors"));
    if (colors.isString())
        theme["colors"] = colors.toString();
    else if (!colors.isUndefined() && !colors.isNull())
        theme["colors"] = themeColors(colors, look);
    const QJsonValue skin = obj.value(QStringLiteral("skin"));
    if (skin.isString()) {
        theme["skin"] = looks::skin(appRoot, dataRoot, skin.toString());
    } else if (skin.isObject()) {
        theme["skin"] = skinParts(dir, skin.toObject(), look);
    } else if (!skin.isUndefined() && !skin.isNull()) {
        qWarning("[AppCore] %s: its skin is not a skin's name or an object: none", qPrintable(look));
        theme["skin"] = QVariantMap();
    }
    const QJsonObject effects = obj.value(QStringLiteral("effects")).toObject();
    QVariantMap themeEffects;
    const struct { const char *kind; QStringList knobs; } kinds[] = {
        { "text", textKnobs }, { "background", {} }, { "selector", {} }, { "screen", screenKnobs },
        { "transition", {} } };
    for (const auto &k : kinds) {
        const QJsonValue v = effects.value(QLatin1String(k.kind));
        if (!v.isUndefined() && !v.isNull())
            themeEffects[k.kind] = themeEffect(dir, v, k.knobs, look, QLatin1String(k.kind));
    }
    theme["effects"] = themeEffects;
    const QString music = lookFile(dir, obj.value(QStringLiteral("music")), musicTypes, look,
                                   QStringLiteral("music"));
    if (!music.isEmpty())
        theme["music"] = music;
    // The Arcade screen's look: "orb", in 3D (the Arcade module's
    // OrbLook.qml). Any other is left out, and the screen keeps its own.
    const QString arcade = obj.value(QStringLiteral("arcade")).toString();
    if (arcade == QLatin1String("orb"))
        theme["arcade"] = arcade;
    else if (obj.contains(QStringLiteral("arcade")))
        qWarning("[AppCore] %s: its arcade, \"%s\", is not a look the Arcade screen has", qPrintable(look),
                 qPrintable(arcade));
    qInfo("[AppCore] theme %s: %s", qPrintable(id), qPrintable(path));
    return theme;
}

QString effectShader(const QString &name) {
    const QString shader = QStringLiteral(":/shaders/") + name + QStringLiteral(".frag.qsb");
    return QFile::exists(shader) ? QStringLiteral("qrc") + shader : QString();
}

} // namespace looks
