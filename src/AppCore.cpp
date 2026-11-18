#include "AppCore.h"
#include "util/AtomicFile.h"
#include "util/AsyncDirectoryCache.h"
#include "util/FileText.h"
#include "util/InstallHint.h"
#include "util/LogSaver.h"
#include "util/Looks.h"
#include "util/ModuleEnabled.h"
#include "util/NetInterfaces.h"
#include "util/SetupFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaProperty>
#include <QUrl>
#include <QVariantMap>
#include <QDebug>
#include <QRegularExpression>
#include <QJSValue>
#include <QQmlContext>
#include <algorithm>

AppCore::AppCore(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    // What save_setting_soon() holds back is written once its steps rest,
    // and as the app quits, though the last step was a moment before.
    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(kSettleMs);
    connect(m_settle, &QTimer::timeout, this, [this]() { flush_settings(); });
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() { flush_settings(); });
    m_logSaver = new LogSaver(appRoot, dataRoot, this);
    connect(m_logSaver, &LogSaver::saved, this, &AppCore::logsSaved);

    QDir modulesDir(appRoot + "/modules");
    const QStringList dirs = modulesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : dirs) {
        QJsonObject manifest;
        QString why;
        if (!filetext::readJsonObject(modulesDir.absoluteFilePath(folder + "/manifest.json"), &manifest, &why)) {
            if (!why.isEmpty())
                qWarning("[AppCore] Bad manifest.json in %s: %s", qPrintable(folder), qPrintable(why));
            continue;
        }
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
        m.settingsGroup = manifest["settings_group"].toString();
        // On unless the manifest's own "enabled" toggle says OFF.
        for (const QVariant &sv : std::as_const(m.settings)) {
            const QVariantMap s = sv.toMap();
            if (s["key"].toString() == "enabled") {
                m.enabledByDefault = moduleEnabledByDefault(s["default"].toString());
                break;
            }
        }
        m_modules.append(m);
        qDebug("[AppCore] Loaded manifest: %s", qPrintable(id));
    }
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

// Left with a setting held back (the app's own end comes through
// aboutToQuit first): written, with no one left to tell if it can't be.
AppCore::~AppCore() {
    writeHeld(false);
}

QJsonObject AppCore::loadConfig(bool *broken) const {
    QJsonObject config = storedConfig(broken);
    for (const HeldSetting &held : m_held)
        putSetting(&config, held.moduleId, held.key, held.value);
    return config;
}

QJsonObject AppCore::storedConfig(bool *broken) const {
    const QString path = m_dataRoot + "/config.json";
    // As last read, while the file is as it was then (its time and size):
    // the settings are asked for by the dozen as a video starts, and the
    // file is a few dozen kilobytes to parse each time. A save of OSD/OS's
    // own (saveConfig()) or an edit by hand reads it anew.
    const QFileInfo info(path);
    if (m_configCacheValid && info.exists() && info.lastModified() == m_configCacheModified
            && info.size() == m_configCacheSize) {
        if (broken)
            *broken = false;
        return m_configCache;
    }
    QJsonObject config;
    QString why;
    const bool ok = filetext::readJsonObject(path, &config, &why);
    // One that is there but can't be read (a stray comma from editing it by
    // hand) must not be written over: these defaults would then replace every
    // setting in it. Readers get the defaults meanwhile; save_setting refuses.
    if (broken)
        *broken = !ok && !why.isEmpty();
    m_configCacheValid = ok && info.exists();
    if (m_configCacheValid) {
        m_configCache = config;
        m_configCacheModified = info.lastModified();
        m_configCacheSize = info.size();
    }
    if (ok)
        return config;
    return QJsonObject{
        {"app", QJsonObject{{"color_scheme","Video 1"}}},
        {"modules", QJsonObject{}}
    };
}

bool AppCore::saveConfig(const QJsonObject &config) {
    const QString path = m_dataRoot + "/config.json";
    m_configCacheValid = false;
    if (!writeFileAtomically(path, QJsonDocument(config).toJson(QJsonDocument::Indented)))
        return false;
    // What was held back was in it.
    m_held.clear();
    m_settle->stop();
    // What was written is what the next read would find: kept, so that the
    // file isn't read and parsed again after each save.
    const QFileInfo info(path);
    if (info.exists()) {
        m_configCache = config;
        m_configCacheModified = info.lastModified();
        m_configCacheSize = info.size();
        m_configCacheValid = true;
    }
    return true;
}

bool AppCore::isModuleEnabled(const ModuleEntry &m, const QJsonObject &modulesConfig) const {
    const QJsonObject mCfg = modulesConfig[m.id].toObject();
    return mCfg.contains("enabled") ? mCfg["enabled"].toBool(true) : m.enabledByDefault;
}

const ModuleEntry *AppCore::findModule(const QString &moduleId) const {
    for (const ModuleEntry &m : m_modules) {
        if (m.id == moduleId)
            return &m;
    }
    return nullptr;
}

QString AppCore::entryPointOf(const ModuleEntry &m) {
    return QStringLiteral("modules/%1/%2").arg(m.folder, m.entryQml);
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
        const QString entryPoint = entryPointOf(m);
        QVariantMap entry;
        entry["name"]        = m.name;
        entry["entry_point"] = entryPoint;
        // The manifest's settings_group: the main menu's group in Settings →
        // Look → Menu Layout's Camcorder ("tv", "arcade", "" for none).
        entry["group"]       = m.settingsGroup;
        // Whose row it is and its logo: inside a module, Camcorder's column
        // is the modules of its group by their logos (Main.qml's frame).
        entry["id"]          = m.id;
        entry["icon"]        = QUrl::fromLocalFile(QStringLiteral("%1/modules/%2/%3")
                                                       .arg(m_appRoot, m.folder, m.iconRel)).toString();
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
                entry["entry_point"] = entryPointOf(m);
            entry["group"] = m.settingsGroup;
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

// A JS object or array from QML arrives wrapped as a QJSValue, which
// QJsonValue::fromVariant() would store as null.
static QVariant plainValue(const QVariant &raw) {
    return raw.metaType() == QMetaType::fromType<QJSValue>() ? raw.value<QJSValue>().toVariant() : raw;
}

bool AppCore::save_setting(const QString &moduleId, const QString &key, const QVariant &rawValue) {
    return saveSettings(moduleId, { { key, plainValue(rawValue) } });
}

bool AppCore::save_settings(const QString &moduleId, const QVariantMap &values) {
    QList<std::pair<QString, QVariant>> pairs;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        pairs.append({ it.key(), plainValue(it.value()) });
    return pairs.isEmpty() || saveSettings(moduleId, pairs);
}

// One read of config.json and one write for all of them: a write is synced to
// the card, tens of milliseconds and more each on an SD card (AtomicFile.h),
// and a theme chosen saved nine settings one after another.
bool AppCore::saveSettings(const QString &moduleId, const QList<std::pair<QString, QVariant>> &values) {
    bool broken = false;
    QJsonObject config = loadConfig(&broken);
    if (broken) {
        qWarning("[AppCore] Setting not saved: config.json is there but isn't valid JSON, "
                 "and saving %s.%s would write the defaults over it",
                 qPrintable(moduleId.isEmpty() ? "app" : moduleId), qPrintable(values.value(0).first));
        return false;
    }

    for (const auto &[key, value] : values)
        putSetting(&config, moduleId, key, value);

    // Only a setting on the card is one the backends take up: one that
    // couldn't be saved would be gone at the next start, and get_setting()
    // says the old one meanwhile.
    if (!saveConfig(config)) {
        qWarning("[AppCore] Setting not saved: %s.%s",
                 qPrintable(moduleId.isEmpty() ? "app" : moduleId), qPrintable(values.value(0).first));
        return false;
    }
    for (const auto &[key, value] : values) {
        qDebug("[AppCore] Setting saved: %s.%s = %s",
               qPrintable(moduleId.isEmpty() ? "app" : moduleId),
               qPrintable(key), qPrintable(value.toString()));
        tellSetting(moduleId, key, value);
    }
    return true;
}

void AppCore::putSetting(QJsonObject *config, const QString &moduleId, const QString &key, const QVariant &value) {
    // The target section
    QJsonObject target = moduleId.isEmpty() ? config->value("app").toObject()
                                            : config->value("modules").toObject()[moduleId].toObject();
    // Dot-notation: "libraries.somekey" -> target["libraries"]["somekey"]
    const QStringList parts = key.split('.', Qt::KeepEmptyParts);
    if (parts.size() == 2) {
        QJsonObject sub = target[parts[0]].toObject();
        sub[parts[1]] = QJsonValue::fromVariant(value);
        target[parts[0]] = sub;
    } else {
        target[key] = QJsonValue::fromVariant(value);
    }
    if (moduleId.isEmpty()) {
        config->insert("app", target);
    } else {
        QJsonObject modules = config->value("modules").toObject();
        modules[moduleId] = target;
        config->insert("modules", modules);
    }
}

void AppCore::tellSetting(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId.isEmpty())
        emit appSettingChanged(key, value.toString());
    else
        emit moduleSettingChanged(moduleId, key, value);
    // A module turned on or off: its status icons come or go.
    if (!moduleId.isEmpty() && key == QLatin1String("enabled"))
        updateStatusIcons();
}

// A slider stepped across its bar saved at every step: ten writes of the
// file, each synced to the card (AtomicFile.h), tens of milliseconds and more
// each on an SD card, on the app's thread with the key held. The value is
// told at each step and the file written once.
bool AppCore::save_setting_soon(const QString &moduleId, const QString &key, const QVariant &rawValue) {
    bool broken = false;
    storedConfig(&broken);
    if (broken) {
        qWarning("[AppCore] Setting not saved: config.json is there but isn't valid JSON, "
                 "and saving %s.%s would write the defaults over it",
                 qPrintable(moduleId.isEmpty() ? "app" : moduleId), qPrintable(key));
        return false;
    }
    const QVariant value = plainValue(rawValue);
    const auto held = std::find_if(m_held.begin(), m_held.end(), [&](const HeldSetting &h) {
        return h.moduleId == moduleId && h.key == key;
    });
    if (held != m_held.end())
        held->value = value;
    else
        m_held.append({ moduleId, key, value });
    m_settle->start();
    tellSetting(moduleId, key, value);
    return true;
}

bool AppCore::flush_settings() {
    return writeHeld(true);
}

bool AppCore::writeHeld(bool tell) {
    m_settle->stop();
    if (m_held.isEmpty())
        return true;
    const QList<HeldSetting> held = m_held;
    bool broken = false;
    const QJsonObject config = loadConfig(&broken);
    if (!broken && saveConfig(config)) {
        qDebug("[AppCore] Settings saved: %s.%s = %s, changed in steps%s",
               qPrintable(held.last().moduleId.isEmpty() ? "app" : held.last().moduleId),
               qPrintable(held.last().key), qPrintable(held.last().value.toString()),
               held.size() > 1 ? ", and the others held with it" : "");
        return true;
    }
    // Not on the card: what the file has is what holds, as after a
    // save_setting() that failed. Whatever took the new values up is told the
    // saved ones, and the pages show their rows as saved.
    m_held.clear();
    qWarning("[AppCore] Setting not saved: %s.%s, changed in steps, is as config.json has it again",
             qPrintable(held.last().moduleId.isEmpty() ? "app" : held.last().moduleId), qPrintable(held.last().key));
    if (!tell)
        return false;
    for (const HeldSetting &h : held)
        tellSetting(h.moduleId, h.key, get_setting(h.moduleId, h.key));
    emit settingsNotSaved();
    return false;
}

void AppCore::setSetupNotice(const QString &title, const QString &detail) {
    if (title == m_setupNotice && detail == m_setupNoticeDetail)
        return;
    m_setupNotice = title;
    m_setupNoticeDetail = detail;
    emit setupNoticeChanged();
}

void AppCore::dismissSetupNotice() {
    setSetupNotice(QString(), QString());
}

QVariantMap AppCore::applySetupText(const QString &text) {
    const auto notice = [](bool ok, const QString &title, const QStringList &lines) {
        return QVariantMap{ { QStringLiteral("ok"), ok }, { QStringLiteral("title"), title },
                            { QStringLiteral("detail"), lines.join(QLatin1Char('\n')) } };
    };
    if (text.trimmed().isEmpty())
        return notice(false, QStringLiteral("Nothing to paste"),
                      { QStringLiteral("There is no text to paste: copy the settings on OSD/OS Setup's page first (osdos.tv/setup, Copy for a computer)") });
    setupfile::Setup setup;
    QString why;
    if (!setupfile::parse(text.toUtf8(), &setup, &why))
        return notice(false, QStringLiteral("Not pasted"), { QStringLiteral("What was pasted is ") + why });
    QStringList lines;
    bool ok = true;
    int applied = 0;
    if (!setup.config.isEmpty()) {
        bool broken = false;
        const QJsonObject config = loadConfig(&broken);
        const QJsonObject after = setupfile::merged(config, setup.config);
        if (broken) {
            lines << QStringLiteral("OSD/OS settings: not set: config.json couldn't be read, and is left as it is");
            ok = false;
        } else if (!saveConfig(after)) {
            lines << QStringLiteral("OSD/OS settings: not set: config.json couldn't be written");
            ok = false;
        } else {
            lines << setupfile::settingsLine(setupfile::count(setup.config));
            ++applied;
            tellChanges(config, after);
        }
    }
    if (!setup.account.isEmpty()) {
        if (setupfile::writeAccount(setup.account, m_dataRoot, &why)) {
            lines << setupfile::accountLine(setup.account);
            ++applied;
            emit setupAccountWritten();
        } else {
            lines << QStringLiteral("RetroAchievements: not set: ") + why;
            ok = false;
        }
    }
    if (!setup.favorites.isEmpty()) {
        if (setupfile::writeFavorites(setup.favorites, m_dataRoot, &why)) {
            lines << setupfile::favoritesLine(int(setup.favorites.size()));
            ++applied;
            emit setupFavoritesWritten();
        } else {
            lines << QStringLiteral("Arcade favorites: not added: ") + why;
            ok = false;
        }
    }
    if (!setup.cardOnly.isEmpty())
        lines << setupfile::cardOnlyLine(setup.cardOnly);
    if (lines.isEmpty())
        lines << QStringLiteral("Nothing in it to set");
    for (const QString &line : std::as_const(lines))
        qInfo("[Setup] Pasted: %s", qPrintable(line));
    if (applied == 0)
        return notice(false, ok ? QStringLiteral("Nothing set from OSD/OS Setup") : QStringLiteral("Not pasted"), lines);
    return notice(ok, ok ? QStringLiteral("OSD/OS Setup pasted") : QStringLiteral("OSD/OS Setup partly pasted"), lines);
}

bool AppCore::canSaveLogs() const {
    return m_logSaver->available();
}

bool AppCore::saveLogs() {
    return m_logSaver->save();
}

void AppCore::tellChanges(const QJsonObject &before, const QJsonObject &after) {
    const QJsonObject appBefore = before.value(QLatin1String("app")).toObject();
    const QJsonObject appAfter = after.value(QLatin1String("app")).toObject();
    for (auto it = appAfter.begin(); it != appAfter.end(); ++it) {
        if (appBefore.value(it.key()) != it.value())
            emit appSettingChanged(it.key(), it.value().toVariant().toString());
    }
    bool turned = false;
    const QJsonObject modulesBefore = before.value(QLatin1String("modules")).toObject();
    const QJsonObject modulesAfter = after.value(QLatin1String("modules")).toObject();
    for (auto module = modulesAfter.begin(); module != modulesAfter.end(); ++module) {
        const QJsonObject was = modulesBefore.value(module.key()).toObject();
        const QJsonObject now = module.value().toObject();
        for (auto it = now.begin(); it != now.end(); ++it) {
            if (was.value(it.key()) == it.value())
                continue;
            if (it.value().isObject()) {
                const QJsonObject wasValues = was.value(it.key()).toObject();
                const QJsonObject values = it.value().toObject();
                for (auto value = values.begin(); value != values.end(); ++value) {
                    if (wasValues.value(value.key()) != value.value())
                        emit moduleSettingChanged(module.key(), it.key() + QLatin1Char('.') + value.key(),
                                                  value.value().toVariant());
                }
            } else {
                emit moduleSettingChanged(module.key(), it.key(), it.value().toVariant());
            }
            turned = turned || it.key() == QLatin1String("enabled");
        }
    }
    if (turned)
        updateStatusIcons();
}

QVariant AppCore::get_module_info(const QString &moduleId) {
    const ModuleEntry *m = findModule(moduleId);
    if (!m)
        return QVariantMap{};
    const QString iconPath = QStringLiteral("%1/modules/%2/%3").arg(m_appRoot, m->folder, m->iconRel);
    return QVariantMap{{"name", m->name}, {"icon", QUrl::fromLocalFile(iconPath).toString()}};
}

bool AppCore::is_module_enabled(const QString &moduleId) const {
    const ModuleEntry *m = findModule(moduleId);
    return m && isModuleEnabled(*m, loadConfig()["modules"].toObject());
}

QString AppCore::module_entry_point(const QString &moduleId) const {
    const ModuleEntry *m = findModule(moduleId);
    return m ? entryPointOf(*m) : QString();
}

QVariant AppCore::get_module_settings_schema(const QString &moduleId) {
    const ModuleEntry *m = findModule(moduleId);
    return m ? QVariant(m->settings) : QVariant(QVariantList{});
}

bool AppCore::invoke_module_action(const QString &moduleId, const QString &slotName) {
    auto it = m_backends.find(moduleId);
    if (it == m_backends.end()) {
        qWarning("[AppCore] invoke_module_action: no backend for '%s'", qPrintable(moduleId));
        return false;
    }
    bool ok = QMetaObject::invokeMethod(it.value(), slotName.toLatin1().constData(),
                                        Qt::QueuedConnection);
    if (!ok)
        qWarning("[AppCore] invoke_module_action: slot '%s' not found on backend '%s'",
                 qPrintable(slotName), qPrintable(moduleId));
    return ok;
}

void AppCore::registerModule(const QString &moduleId, const QString &contextProperty,
                             QObject *backend, QQmlContext *ctx) {
    // Only a backend there is: the lookups by id dereference what is kept.
    if (!backend) return;
    m_backends[moduleId] = backend;
    if (ctx)
        ctx->setContextProperty(contextProperty, backend);

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

    // statusIcons' notify signal -> updateStatusIcons(), which reads them again
    const int icons = bmo->indexOfProperty("statusIcons");
    if (icons >= 0 && bmo->property(icons).hasNotifySignal()) {
        QMetaObject::connect(backend, bmo->property(icons).notifySignalIndex(), this,
                             amo->indexOfSlot("updateStatusIcons()"));
        updateStatusIcons();
    }
}

void AppCore::updateStatusIcons() {
    const QJsonObject modulesConfig = loadConfig()["modules"].toObject();
    QStringList icons;
    for (const auto &m : m_modules) {
        QObject *backend = m_backends.value(m.id);
        if (backend && isModuleEnabled(m, modulesConfig))
            icons << backend->property("statusIcons").toStringList();
    }
    if (icons != m_statusIcons) {
        m_statusIcons = icons;
        emit statusIconsChanged();
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
            {"settings_group", m.settingsGroup},
            {"enabled",      isModuleEnabled(m, modulesConfig)}
        });
    }
    return result;
}

QVariantMap AppCore::getCustomColorScheme() const {
    return looks::customColorScheme(m_dataRoot);
}

QVariantMap AppCore::getCustomColorSchemes() const {
    return looks::customColorSchemes(m_dataRoot);
}

QVariantList AppCore::skins() const {
    return looks::skins(m_appRoot, m_dataRoot);
}

QVariantMap AppCore::skin(const QString &id) const {
    return looks::skin(m_appRoot, m_dataRoot, id);
}

QVariantList AppCore::themes() const {
    return looks::themes(m_appRoot, m_dataRoot);
}

QVariantMap AppCore::theme(const QString &id) const {
    return looks::theme(m_appRoot, m_dataRoot, id);
}

QString AppCore::effectShader(const QString &name) const {
    return looks::effectShader(name);
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
#ifdef Q_OS_WIN
    // Windows has no one root: "/" is the drive OSD/OS runs from alone. Each
    // drive there is, a second disk's, a USB stick's or a mapped network
    // drive's, is a place by its letter ("D:"); one with nothing in it (a
    // card reader's empty slot) is no folder, and is left out.
    const QFileInfoList drives = QDir::drives();
    for (const QFileInfo &drive : drives) {
        const QString path = drive.absoluteFilePath();   // "D:/"
        add(path.left(2), path);
    }
#else
    add(QStringLiteral("Root"), QStringLiteral("/"));
#endif
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

QString AppCore::licenseText(const QString &file) const {
    // Only the app's own files: a path within its folder.
    const QString name = file.isEmpty() ? QStringLiteral("LICENSE") : QDir::cleanPath(file);
    if (QDir::isAbsolutePath(name) || name == QLatin1String("..") || name.startsWith(QLatin1String("../")))
        return QString();
    QFile f(m_appRoot + QLatin1Char('/') + name);
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

QString AppCore::installHint(const QString &program) const {
    return installhint::forProgram(program);
}

// A device typically has several addresses (RPi: eth0 + wlan0; SteamOS: wlan0 plus
// Docker/Flatpak bridges; macOS: en0 plus awdl/bridge/utun VPN interfaces), so pick
// rather than take the first: skip loopback, virtual/tunnel and down interfaces, keep
// only routable IPv4, and prefer wired over wireless over anything else.
QString AppCore::localIpAddress() const {
    // Interface names that are virtual/tunnel/link-local by convention on the three
    // targets. Qt's type() misses some of these (Docker bridges report as Ethernet).
    static const QRegularExpression kVirtualIface(
        "^(docker|br-|bridge|veth|virbr|vmnet|vboxnet|utun|tun|tap|ipsec|zt|awdl|llw|anpi|ap\\d)",
        QRegularExpression::CaseInsensitiveOption);

    QString best;
    int bestScore = -1;

    const QList<netif::Interface> interfaces = netif::list();
    for (const netif::Interface &iface : interfaces) {
        if (!iface.up || !iface.running) continue;
        if (iface.kind == netif::Kind::Loopback || iface.kind == netif::Kind::Virtual) continue;
        if (kVirtualIface.match(iface.name).hasMatch()) continue;

        int score = 0;
        if (iface.kind == netif::Kind::Ethernet) score = 2;
        else if (iface.kind == netif::Kind::Wifi) score = 1;
        if (score <= bestScore) continue;

        for (const netif::Address &entry : iface.addresses) {
            const QHostAddress addr = entry.ip;
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
    // Not a disabled module: never auto-launch into one that isn't in the
    // module list (set as startup, then disabled later).
    return is_module_enabled(moduleId) ? module_entry_point(moduleId) : QString();
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

QJsonObject AppCore::loadLists(bool *broken) const {
    const QString path = m_dataRoot + QStringLiteral("/lists.json");
    // As last read, while the file is as it was then (its time and size), as
    // config.json is: a list is asked for at every page of the Arcade
    // screen's games, and the file was read and parsed whole each time.
    const QFileInfo info(path);
    if (m_listsCacheValid && info.exists() && info.lastModified() == m_listsCacheModified
            && info.size() == m_listsCacheSize) {
        if (broken)
            *broken = false;
        return m_listsCache;
    }
    QJsonObject lists;
    QString why;
    const bool ok = filetext::readJsonObject(path, &lists, &why);
    // As config.json: one there but unreadable is not written over.
    if (broken)
        *broken = !ok && !why.isEmpty();
    m_listsCacheValid = ok && info.exists();
    if (m_listsCacheValid) {
        m_listsCache = lists;
        m_listsCacheModified = info.lastModified();
        m_listsCacheSize = info.size();
    }
    return lists;
}

// The lists as they are, for a change to be saved over them; false, with a
// line in the log, when the file is there but can't be read.
bool AppCore::listsToChange(QJsonObject *lists) const {
    bool broken = false;
    *lists = loadLists(&broken);
    if (broken)
        qWarning("[AppCore] lists.json is there but isn't valid JSON: not written over");
    return !broken;
}

// False, with a line in the log, when the file couldn't be written (a full
// card): the lists are then as the file has them, as a setting is after a
// save_setting() that failed.
bool AppCore::saveLists(const QJsonObject &lists, const QString &moduleId, const QString &name) const {
    m_listsCacheValid = false;
    if (!writeFileAtomically(m_dataRoot + QStringLiteral("/lists.json"),
                             QJsonDocument(lists).toJson(QJsonDocument::Compact))) {
        qWarning("[AppCore] List not saved: %s.%s", qPrintable(moduleId), qPrintable(name));
        return false;
    }
    return true;
}

QVariantList AppCore::get_list(const QString &moduleId, const QString &name) const {
    return loadLists().value(moduleId).toObject().value(name).toArray().toVariantList();
}

// A list with an entry put first: the one of its path there taken out, and
// no more than limit kept.
static QJsonArray withFirst(const QJsonArray &list, const QVariantMap &entry, int limit) {
    const QString path = entry.value(QStringLiteral("path")).toString();
    QJsonArray kept{ QJsonObject::fromVariantMap(entry) };
    for (const QJsonValue &v : list) {
        if (kept.size() >= qMax(1, limit))
            break;
        if (v.toObject().value(QStringLiteral("path")).toString() != path)
            kept.append(v);
    }
    return kept;
}

bool AppCore::add_to_list(const QString &moduleId, const QString &name,
                          const QVariantMap &entry, int limit) {
    return add_all_to_list(moduleId, name, QVariantList{ entry }, limit);
}

bool AppCore::add_all_to_list(const QString &moduleId, const QString &name,
                              const QVariantList &entries, int limit) {
    QJsonObject lists;
    QJsonObject module;
    QJsonArray list;
    bool read = false;
    for (const QVariant &one : entries) {
        const QVariantMap entry = one.toMap();
        if (entry.value(QStringLiteral("path")).toString().isEmpty())
            continue;
        // Read at the first entry there is to add: nothing is, nothing is
        // read or written.
        if (!read) {
            if (!listsToChange(&lists))
                return false;
            module = lists.value(moduleId).toObject();
            list = module.value(name).toArray();
            read = true;
        }
        list = withFirst(list, entry, limit);
    }
    if (!read)
        return true;
    module.insert(name, list);
    lists.insert(moduleId, module);
    return saveLists(lists, moduleId, name);
}

bool AppCore::remove_from_list(const QString &moduleId, const QString &name, const QString &path) {
    QJsonObject lists;
    if (!listsToChange(&lists))
        return false;
    QJsonObject module = lists.value(moduleId).toObject();
    QJsonArray kept;
    for (const QJsonValue &v : module.value(name).toArray()) {
        if (v.toObject().value(QStringLiteral("path")).toString() != path)
            kept.append(v);
    }
    module.insert(name, kept);
    lists.insert(moduleId, module);
    return saveLists(lists, moduleId, name);
}

bool AppCore::list_contains(const QString &moduleId, const QString &name, const QString &path) const {
    for (const QJsonValue &v : loadLists().value(moduleId).toObject().value(name).toArray()) {
        if (v.toObject().value(QStringLiteral("path")).toString() == path)
            return true;
    }
    return false;
}
