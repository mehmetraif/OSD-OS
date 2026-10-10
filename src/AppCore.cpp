#include "AppCore.h"
#include "util/AtomicFile.h"
#include "util/AsyncDirectoryCache.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QVariantMap>
#include <QDebug>
#include <QRegularExpression>
#include <QNetworkInterface>
#include <QJSValue>
#include <QQmlContext>
#include <algorithm>

AppCore::AppCore(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    QDir modulesDir(appRoot + "/modules");
    const QStringList dirs = modulesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : dirs) {
        QString manifestPath = modulesDir.absoluteFilePath(folder + "/manifest.json");
        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("[AppCore] Bad manifest.json in %s: %s",
                     qPrintable(folder), qPrintable(err.errorString()));
            continue;
        }
        QJsonObject manifest = doc.object();
        QString id       = manifest["id"].toString();
        QString entryQml = manifest["entry_point_qml"].toString();
        if (id.isEmpty() || entryQml.isEmpty()) {
            qWarning("[AppCore] Skipping %s: manifest missing 'id' or 'entry_point_qml'",
                     qPrintable(folder));
            continue;
        }
        ModuleEntry m;
        m.id       = id;
        m.name     = manifest["name"].toString();
        m.folder   = folder;
        m.entryQml = entryQml;
        m.iconRel  = manifest["icon"].toString();
        m.settings = manifest["settings"].toArray().toVariantList();
        m_modules.append(m);
        qDebug("[AppCore] Loaded manifest: %s", qPrintable(id));
    }
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QJsonObject AppCore::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    // Return a sensible default if the file is missing or corrupt
    return QJsonObject{
        {"app", QJsonObject{{"color_scheme","Video 1"}}},
        {"modules", QJsonObject{}}
    };
}

bool AppCore::saveConfig(const QJsonObject &config) const {
    return writeFileAtomically(m_dataRoot + "/config.json",
                               QJsonDocument(config).toJson(QJsonDocument::Indented));
}

bool AppCore::isModuleEnabled(const ModuleEntry &m, const QJsonObject &modulesConfig) const {
    QJsonObject mCfg = modulesConfig[m.id].toObject();
    bool manifestDefault = true;
    for (const auto &sv : m.settings) {
        QVariantMap s = sv.toMap();
        if (s["key"].toString() == "enabled") {
            manifestDefault = s["default"].toString().toUpper() != "OFF";
            break;
        }
    }
    return mCfg.contains("enabled") ? mCfg["enabled"].toBool(true) : manifestDefault;
}

// ---------------------------------------------------------------------------
// Q_INVOKABLE slots
// ---------------------------------------------------------------------------

void AppCore::scan_for_modules() {
    QJsonObject config = loadConfig();
    QJsonObject modulesConfig = config["modules"].toObject();

    QVariantList displayData;
    for (const auto &m : m_modules) {
        // Respect "enabled" setting; fall back to manifest default, then true
        if (!isModuleEnabled(m, modulesConfig)) {
            qDebug("[AppCore] Module disabled: %s", qPrintable(m.name));
            continue;
        }
        // entry_point is a path relative to APP_ROOT
        QString entryPoint = QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
        QVariantMap entry;
        entry["name"]        = m.name;
        entry["entry_point"] = entryPoint;
        displayData.append(entry);
        qDebug("[AppCore] Module: %s -> %s", qPrintable(m.name), qPrintable(entryPoint));
    }

    // Extra top-level rows contributed by module backends. Probed, not connected —
    // the same idiom as get_auth_state (see get_module_auth_state): a backend that
    // declares Q_INVOKABLE QVariantList get_menu_entries() can add main-menu rows
    // without ModuleList.qml knowing anything about what they are. A backend
    // supplies only {name, params}; entry_point is filled in from its manifest
    // here, so a backend can't get that wrong.
    //
    // Appended AFTER all module rows on purpose: module row indices then stay
    // stable, so the saved menu position still restores onto the same row when a
    // contributed row is added or removed.
    for (const auto &m : m_modules) {
        if (!isModuleEnabled(m, modulesConfig)) continue;
        const QVariantList extras = menuEntriesForModule(m.id);
        for (const QVariant &v : extras) {
            QVariantMap entry = v.toMap();
            if (entry.value("name").toString().isEmpty()) {
                qWarning("[AppCore] %s contributed a menu entry with no name — skipped",
                         qPrintable(m.id));
                continue;
            }
            if (!entry.contains("entry_point"))
                entry["entry_point"] = QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
            displayData.append(entry);
            qDebug("[AppCore] Menu entry from %s: %s -> %s", qPrintable(m.id),
                   qPrintable(entry.value("name").toString()),
                   qPrintable(entry.value("entry_point").toString()));
        }
    }

    emit modulesLoaded(displayData);
}

QVariantList AppCore::menuEntriesForModule(const QString &moduleId) const {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) return {};
    if (it.value()->metaObject()->indexOfMethod(
            QMetaObject::normalizedSignature("get_menu_entries()")) < 0) {
        return {};
    }
    QVariantList result;
    bool ok = QMetaObject::invokeMethod(
        it.value(), "get_menu_entries",
        Qt::DirectConnection,
        Q_RETURN_ARG(QVariantList, result)
    );
    if (!ok) return {};
    return result;
}

QVariant AppCore::get_settings() {
    return loadConfig().toVariantMap();
}

QVariant AppCore::get_setting(const QString &moduleId, const QString &key) {
    QJsonObject config = loadConfig();
    QJsonObject target;
    if (moduleId.isEmpty())
        target = config["app"].toObject();
    else
        target = config["modules"].toObject()[moduleId].toObject();

    // Mirror save_setting's dot-notation handling: "libraries.somekey" reads
    // target["libraries"]["somekey"], not a literal "libraries.somekey" key.
    QStringList parts = key.split('.', Qt::KeepEmptyParts);
    if (parts.size() == 2)
        return target[parts[0]].toObject()[parts[1]].toVariant();
    return target[key].toVariant();
}

void AppCore::save_setting(const QString &moduleId, const QString &key, const QVariant &rawValue) {
    // A JS object or array from QML arrives wrapped as a QJSValue, which
    // QJsonValue::fromVariant() would store as null.
    const QVariant value = rawValue.metaType() == QMetaType::fromType<QJSValue>()
                               ? rawValue.value<QJSValue>().toVariant() : rawValue;
    QJsonObject config = loadConfig();

    // Navigate to the target section
    auto getTarget = [&]() -> QJsonObject {
        if (moduleId.isEmpty())
            return config["app"].toObject();
        return config["modules"].toObject()[moduleId].toObject();
    };
    auto setTarget = [&](const QJsonObject &target) {
        if (moduleId.isEmpty()) {
            config["app"] = target;
        } else {
            QJsonObject modules = config["modules"].toObject();
            modules[moduleId] = target;
            config["modules"] = modules;
        }
    };

    QJsonObject target = getTarget();

    // Handle dot-notation: "libraries.somekey" -> target["libraries"]["somekey"]
    QStringList parts = key.split('.', Qt::KeepEmptyParts);
    if (parts.size() == 2) {
        QJsonObject sub = target[parts[0]].toObject();
        sub[parts[1]] = QJsonValue::fromVariant(value);
        target[parts[0]] = sub;
    } else {
        target[key] = QJsonValue::fromVariant(value);
    }

    setTarget(target);
    if (saveConfig(config))
        qDebug("[AppCore] Setting saved: %s.%s = %s",
               qPrintable(moduleId.isEmpty() ? "app" : moduleId),
               qPrintable(key), qPrintable(value.toString()));
    else
        qWarning("[AppCore] Setting not saved: %s.%s",
                 qPrintable(moduleId.isEmpty() ? "app" : moduleId), qPrintable(key));

    if (moduleId.isEmpty())
        emit appSettingChanged(key, value.toString());
    else
        emit moduleSettingChanged(moduleId, key, value);
}

QVariant AppCore::get_module_info(const QString &moduleId) {
    for (const auto &m : m_modules) {
        if (m.id == moduleId) {
            QString iconPath = QStringLiteral("%1/modules/%2/%3")
                                   .arg(m_appRoot, m.folder, m.iconRel);
            QString iconUrl = QUrl::fromLocalFile(iconPath).toString();
            return QVariantMap{{"name", m.name}, {"icon", iconUrl}};
        }
    }
    return QVariantMap{};
}

bool AppCore::is_module_enabled(const QString &moduleId) const {
    const QJsonObject modulesConfig = loadConfig()["modules"].toObject();
    for (const auto &m : m_modules) {
        if (m.id == moduleId) return isModuleEnabled(m, modulesConfig);
    }
    return false;
}

QString AppCore::module_entry_point(const QString &moduleId) const {
    for (const auto &m : m_modules) {
        if (m.id == moduleId)
            return QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
    }
    return {};
}

QVariant AppCore::get_module_settings_schema(const QString &moduleId) {
    for (const auto &m : m_modules) {
        if (m.id == moduleId)
            return m.settings;
    }
    return QVariantList{};
}

void AppCore::invoke_module_action(const QString &moduleId, const QString &slotName) {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) {
        qWarning("[AppCore] invoke_module_action: no backend for '%s'", qPrintable(moduleId));
        return;
    }
    bool ok = QMetaObject::invokeMethod(it.value(), slotName.toLatin1().constData(),
                                        Qt::QueuedConnection);
    if (!ok)
        qWarning("[AppCore] invoke_module_action: slot '%s' not found on backend '%s'",
                 qPrintable(slotName), qPrintable(moduleId));
}

void AppCore::registerModule(const QString &moduleId, const QString &contextProperty,
                             QObject *backend, QQmlContext *ctx) {
    m_backends[moduleId] = backend;
    if (ctx)
        ctx->setContextProperty(contextProperty, backend);
    if (!backend) return;

    const QMetaObject *bmo = backend->metaObject();
    const QMetaObject *amo = this->metaObject();

    // dynamicOptionsReady(key, options) -> onBackendDynamicOptions (re-emit with moduleId)
    int sig = bmo->indexOfSignal(
        QMetaObject::normalizedSignature("dynamicOptionsReady(QString,QVariant)"));
    if (sig >= 0) {
        int slot = amo->indexOfSlot(
            QMetaObject::normalizedSignature("onBackendDynamicOptions(QString,QVariant)"));
        QMetaObject::connect(backend, sig, this, slot);
    }

    // authStateChanged() -> onBackendAuthStateChanged (re-emit with moduleId)
    sig = bmo->indexOfSignal(QMetaObject::normalizedSignature("authStateChanged()"));
    if (sig >= 0) {
        int slot = amo->indexOfSlot(
            QMetaObject::normalizedSignature("onBackendAuthStateChanged()"));
        QMetaObject::connect(backend, sig, this, slot);
    }

    // moduleSettingChanged(moduleId, key, value) -> backend.onSettingChanged(...)
    int slot = bmo->indexOfSlot(
        QMetaObject::normalizedSignature("onSettingChanged(QString,QString,QVariant)"));
    if (slot >= 0) {
        int s = amo->indexOfSignal(
            QMetaObject::normalizedSignature("moduleSettingChanged(QString,QString,QVariant)"));
        QMetaObject::connect(this, s, backend, slot);
    }
}

QString AppCore::moduleIdForBackend(QObject *backend) const {
    for (auto it = m_backends.constBegin(); it != m_backends.constEnd(); ++it) {
        if (it.value() == backend) return it.key();
    }
    return QString{};
}

void AppCore::onBackendDynamicOptions(const QString &key, const QVariant &options) {
    QString moduleId = moduleIdForBackend(sender());
    if (!moduleId.isEmpty())
        emit dynamicOptionsReady(moduleId, key, options);
}

void AppCore::onBackendAuthStateChanged() {
    QString moduleId = moduleIdForBackend(sender());
    if (!moduleId.isEmpty())
        emit moduleAuthStateChanged(moduleId);
}

QString AppCore::get_module_auth_state(const QString &moduleId) {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) return QString{};
    if (it.value()->metaObject()->indexOfMethod(
            QMetaObject::normalizedSignature("get_auth_state()")) < 0) {
        return QString{};
    }
    QString result;
    bool ok = QMetaObject::invokeMethod(
        it.value(), "get_auth_state",
        Qt::DirectConnection,
        Q_RETURN_ARG(QString, result)
    );
    if (!ok) return QString{};
    return result;
}

QVariant AppCore::get_installed_modules() {
    QJsonObject modulesConfig = loadConfig()["modules"].toObject();
    QVariantList result;
    for (const auto &m : m_modules) {
        result.append(QVariantMap{
            {"id",           m.id},
            {"name",         m.name},
            {"has_settings", !m.settings.isEmpty()},
            {"enabled",      isModuleEnabled(m, modulesConfig)}
        });
    }
    return result;
}

QVariantMap AppCore::importColorScheme(QJsonObject &obj) const {
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

QVariantMap AppCore::getCustomColorScheme() const {
    QFile f(m_dataRoot + "/custom_color_scheme.json");
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[AppCore] custom_color_scheme.json: invalid JSON");
        return {};
    }

    QJsonObject obj = doc.object();
    QVariantMap result = this->importColorScheme(obj);
    return result;
}

QVariantMap AppCore::getCustomColorSchemes() const {
    static const QRegularExpression validThemeName("^[\\w\\d !#-/:-@\\[-_{-~]{3,28}$", QRegularExpression::CaseInsensitiveOption);

    QFile f(m_dataRoot + "/custom_color_schemes.json");
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[AppCore] custom_color_schemes.json: invalid JSON");
        return {};
    }

    QJsonObject obj = doc.object();
    QVariantMap result;
    for (const QString &theme : obj.keys()) {
        if (!validThemeName.match(theme).hasMatch()) {
            qWarning("[AppCore] custom_color_schemes.json: invalid theme name '%s' detected - only 28 letters, numbers, and ASCII symbols (other than backtick and double-quote)",
                    qPrintable(theme));
            continue;
        }
        QJsonObject tObj = obj[theme].toObject();
        QVariantMap tResult = this->importColorScheme(tObj);
        if (tResult.count() == 5) {
            result[theme] = tResult;
            qDebug("[AppCore] custom_color_schemes.json: loaded '%s' custom theme", qPrintable(theme));
        }
    }
    return result;
}

namespace {

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
    QFile f(root.dir + QLatin1Char('/') + id + QLatin1Char('/') + root.file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[AppCore] %s: %s", qPrintable(f.fileName()),
                 err.error != QJsonParseError::NoError ? qPrintable(err.errorString()) : "not an object");
        return false;
    }
    const QJsonObject obj = doc.object();
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

} // namespace

QString AppCore::lookDir(const QString &kind, const QString &id, QJsonObject *json) const {
    // A folder's name, no way up or across.
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String("..")
            || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\')))
        return {};
    for (const LookRoot &root : lookRoots(m_appRoot, m_dataRoot, kind)) {
        if (readLookJson(root, id, json))
            return root.dir + QLatin1Char('/') + id;
    }
    return {};
}

QMap<QString, QJsonObject> AppCore::looks(const QString &kind) const {
    QMap<QString, QJsonObject> found;
    for (const LookRoot &root : lookRoots(m_appRoot, m_dataRoot, kind)) {
        const QStringList ids = QDir(root.dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &id : ids) {
            QJsonObject obj;
            if (!found.contains(id) && readLookJson(root, id, &obj))
                found.insert(id, obj);
        }
    }
    return found;
}

QVariantList AppCore::skins() const {
    return listByName(looks(QStringLiteral("skin")));
}

QVariantMap AppCore::skin(const QString &id) const {
    QJsonObject obj;
    const QString path = lookDir(QStringLiteral("skin"), id, &obj);
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

QVariantList AppCore::themes() const {
    return listByName(looks(QStringLiteral("theme")));
}

QVariantMap AppCore::theme(const QString &id) const {
    static const QStringList textKnobs = { "rainbow", "shimmer", "flicker", "glow" };
    static const QStringList screenKnobs = { "scanlines", "curvature", "glow", "bleed", "noise", "vignette" };
    static const QStringList musicTypes = { "ogg", "opus", "mp3", "flac", "wav", "m4a", "aac",
                                            "mid", "midi", "xm", "mod", "s3m", "it" };
    QJsonObject obj;
    const QString path = lookDir(QStringLiteral("theme"), id, &obj);
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
        theme["skin"] = this->skin(skin.toString());
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
    qInfo("[AppCore] theme %s: %s", qPrintable(id), qPrintable(path));
    return theme;
}

QString AppCore::effectShader(const QString &name) const {
    const QString shader = QStringLiteral(":/shaders/") + name + QStringLiteral(".frag.qsb");
    return QFile::exists(shader) ? QStringLiteral("qrc") + shader : QString();
}

QVariantList AppCore::filePlaces() const {
    QVariantList places;
    auto add = [&places](const QString &name, const QString &path) {
        if (QFileInfo(path).isDir())
            places.append(QVariantMap{ { "name", name }, { "path", path } });
    };
    add(QStringLiteral("Home"), QDir::homePath());
    // Where drives and partitions are mounted: the OSD/OS image's OSD-OS
    // partition and USB drives, udisks', macOS's.
    add(QStringLiteral("Media"), QStringLiteral("/media"));
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    if (!user.isEmpty())
        add(QStringLiteral("Drives"), QStringLiteral("/run/media/") + user);
    add(QStringLiteral("Volumes"), QStringLiteral("/Volumes"));
    add(QStringLiteral("Root"), QStringLiteral("/"));
    return places;
}

static QVariantList readPickerDirectory(const QString &path, const QStringList &fileTypes) {
    QVariantList entries;
    const QDir dir(path);
    if (path.isEmpty() || !dir.exists())
        return entries;
    // Hidden ones are left out, as the menus would only fill with them.
    const QFileInfoList folders = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                    QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &f : folders)
        entries.append(QVariantMap{ { "name", f.fileName() }, { "path", f.absoluteFilePath() }, { "isFolder", true } });
    if (fileTypes.isEmpty())
        return entries;
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &f : files) {
        if (fileTypes.contains(f.suffix().toLower()))
            entries.append(QVariantMap{ { "name", f.fileName() }, { "path", f.absoluteFilePath() }, { "isFolder", false } });
    }
    return entries;
}

QVariantList AppCore::folderEntries(const QString &path, const QStringList &fileTypes) const {
    return readPickerDirectory(path, fileTypes);
}

void AppCore::clearFolderCache() {
    if (m_directoryCache) m_directoryCache->clear();
}

QVariant AppCore::requestFolderEntries(const QString &path, const QStringList &fileTypes) {
    if (!m_directoryCache) {
        m_directoryCache = new AsyncDirectoryCache(this);
        m_directoryCache->ready = [this](const QString &key) { emit folderEntriesReady(key.section(QChar(0), 0, 0)); };
    }
    return m_directoryCache->fetch(path + QChar(0) + fileTypes.join(','),
                                  [path, fileTypes]() { return readPickerDirectory(path, fileTypes); });
}

// A device typically has several addresses (RPi: eth0 + wlan0; SteamOS: wlan0 plus
// Docker/Flatpak bridges; macOS: en0 plus awdl/bridge/utun VPN interfaces), so pick
// rather than take the first: skip loopback, virtual/tunnel and down interfaces, keep
// only routable IPv4, and prefer wired over wireless over anything else.
QString AppCore::licenseText() const {
    QFile f(m_appRoot + "/LICENSE");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    // The file is wrapped at 70 columns. A paragraph is its lines up to a
    // blank one; each becomes one line, for the view to wrap to its width.
    static const QRegularExpression blank(QStringLiteral("\n[ \t]*\n"));
    static const QRegularExpression wrap(QStringLiteral("[ \t]*\n[ \t]*"));
    QStringList paragraphs;
    const QStringList blocks = QString::fromUtf8(f.readAll()).split(blank, Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        QString p = block;
        p.replace(wrap, QStringLiteral(" "));
        p = p.trimmed();
        if (!p.isEmpty())
            paragraphs << p;
    }
    return paragraphs.join(QStringLiteral("\n\n"));
}

QString AppCore::localIpAddress() const {
    // Interface names that are virtual/tunnel/link-local by convention on the three
    // targets. Qt's type() misses some of these (Docker bridges report as Ethernet).
    static const QRegularExpression kVirtualIface(
        "^(docker|br-|bridge|veth|virbr|vmnet|vboxnet|utun|tun|tap|ipsec|zt|awdl|llw|anpi|ap\\d)",
        QRegularExpression::CaseInsensitiveOption);

    QString best;
    int bestScore = -1;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)) continue;
        if (!flags.testFlag(QNetworkInterface::IsRunning)) continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (iface.type() == QNetworkInterface::Virtual) continue;
        if (kVirtualIface.match(iface.name()).hasMatch()) continue;

        int score = 0;
        if (iface.type() == QNetworkInterface::Ethernet) score = 2;
        else if (iface.type() == QNetworkInterface::Wifi) score = 1;
        if (score <= bestScore) continue;

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (addr.isLoopback() || addr.isLinkLocal()) continue;
            best = addr.toString();
            bestScore = score;
            break;
        }
    }
    return best;
}

QString AppCore::startupModuleEntryPoint() const {
    // Keyed by module id (robust to display-name changes); "None"/empty = disabled.
    QString moduleId = loadConfig()["app"].toObject()["startup_module"].toString();
    if (moduleId.isEmpty() || moduleId == "None") return {};
    return moduleEntryPoint(moduleId);
}

QString AppCore::moduleEntryPoint(const QString &moduleId) const {
    QJsonObject modulesConfig = loadConfig()["modules"].toObject();
    for (const auto &m : m_modules) {
        // Skip a disabled module so we never auto-launch into one that isn't
        // present in the module list (e.g. set as startup, then disabled later).
        if (m.id == moduleId && isModuleEnabled(m, modulesConfig)) {
            return QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
        }
    }
    return {};
}

QString AppCore::moduleIdForSource(const QString &source) const {
    static const QRegularExpression folderRe(QStringLiteral("(?:^|/)modules/([^/]+)/"));
    const QRegularExpressionMatch match = folderRe.match(source);
    if (!match.hasMatch())
        return {};
    for (const auto &m : m_modules) {
        if (m.folder == match.captured(1))
            return m.id;
    }
    return {};
}

// ---------------------------------------------------------------------------
// A module's lists: <dataRoot>/lists.json, { "<moduleId>": { "<name>": [entries] } }
// ---------------------------------------------------------------------------

QJsonObject AppCore::loadLists() const {
    QFile f(m_dataRoot + QStringLiteral("/lists.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void AppCore::saveLists(const QJsonObject &lists) const {
    writeFileAtomically(m_dataRoot + QStringLiteral("/lists.json"),
                        QJsonDocument(lists).toJson(QJsonDocument::Compact));
}

QVariantList AppCore::get_list(const QString &moduleId, const QString &name) const {
    return loadLists().value(moduleId).toObject().value(name).toArray().toVariantList();
}

void AppCore::add_to_list(const QString &moduleId, const QString &name,
                          const QVariantMap &entry, int limit) {
    const QString path = entry.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return;
    QJsonObject lists = loadLists();
    QJsonObject module = lists.value(moduleId).toObject();
    QJsonArray kept{ QJsonObject::fromVariantMap(entry) };
    for (const QJsonValue &v : module.value(name).toArray()) {
        if (kept.size() >= qMax(1, limit))
            break;
        if (v.toObject().value(QStringLiteral("path")).toString() != path)
            kept.append(v);
    }
    module.insert(name, kept);
    lists.insert(moduleId, module);
    saveLists(lists);
}

void AppCore::remove_from_list(const QString &moduleId, const QString &name, const QString &path) {
    QJsonObject lists = loadLists();
    QJsonObject module = lists.value(moduleId).toObject();
    QJsonArray kept;
    for (const QJsonValue &v : module.value(name).toArray()) {
        if (v.toObject().value(QStringLiteral("path")).toString() != path)
            kept.append(v);
    }
    module.insert(name, kept);
    lists.insert(moduleId, module);
    saveLists(lists);
}

bool AppCore::list_contains(const QString &moduleId, const QString &name, const QString &path) const {
    for (const QJsonValue &v : loadLists().value(moduleId).toObject().value(name).toArray()) {
        if (v.toObject().value(QStringLiteral("path")).toString() == path)
            return true;
    }
    return false;
}
