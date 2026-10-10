#pragma once
#ifndef APP_BUILD
#define APP_BUILD ""
#endif
#include "util/LegacyNames.h"
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QJsonObject>
#include <QMap>
#include <QCoreApplication>

class QQmlContext;

struct ModuleEntry {
    QString id;
    QString name;
    QString folder;      // subdirectory under modules/
    QString entryQml;    // relative to module folder, e.g. "views/Root.qml"
    QString iconRel;     // relative to module folder, e.g. "assets/images/logo.svg"
    QVariantList settings;
};

class AsyncDirectoryCache;

class AppCore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    // The build: the commit it was made from and the day (CMake's APP_BUILD).
    Q_PROPERTY(QString appBuild READ appBuild CONSTANT)
public:
    explicit AppCore(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    QString appVersion() const { return QCoreApplication::applicationVersion(); }
    QString appBuild() const { return QStringLiteral(APP_BUILD); }

    // True when launched by the autostart systemd service (which injects OSDOS_AUTOSTART=1).
    // Gates the quit overlay's "Exit to Terminal" option, which only makes sense on a
    // headless RPi running under the service. See scripts/install.sh and osdos-stop.
    Q_INVOKABLE bool isAutostartSession() const {
        return legacy::envIsSet("AUTOSTART");
    }
    // The quit overlay's "Restart": exit 12, which the service's stop helper
    // (osdos-stop) takes as a reboot since launcher API 2. An older install's
    // helper would power the Pi off instead, so it isn't offered there.
    Q_INVOKABLE bool canRestartSystem() const {
        return isAutostartSession() && legacy::envInt("LAUNCHER_API") >= 2;
    }

    Q_INVOKABLE void scan_for_modules();
    Q_INVOKABLE QVariant get_settings();
    Q_INVOKABLE QVariant get_setting(const QString &moduleId, const QString &key);
    Q_INVOKABLE void save_setting(const QString &moduleId, const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant get_module_info(const QString &moduleId);
    Q_INVOKABLE QVariant get_module_settings_schema(const QString &moduleId);
    Q_INVOKABLE void invoke_module_action(const QString &moduleId, const QString &slotName);
    Q_INVOKABLE QVariant get_installed_modules();
    Q_INVOKABLE QVariantMap getCustomColorScheme() const;
    Q_INVOKABLE QVariantMap getCustomColorSchemes() const;
    // Settings → Skin: how the window is dressed, apart from the colours: the
    // skins there are, [{ id, name }] by name. A skin is a folder with a
    // skin.json, the app's own (assets/skins) or the data folder's (skins),
    // one there in place of the app's of the same name; its id is the
    // folder's name. One from before skins had their name, a theme.json of
    // window pictures in the data folder's themes, is a skin too.
    Q_INVOKABLE QVariantList skins() const;
    // One skin read for QML (Main.qml's root.skin): { id, name, window,
    // titleBar, hintBar, selection, icons }, a picture of each of the
    // window's parts it dresses ({ source URL, border, tile }) and icons
    // ({ name: URL }) in place of OSD/OS's of those names, only those it has
    // that can be used; empty for none, or for one gone or unreadable.
    Q_INVOKABLE QVariantMap skin(const QString &id) const;
    // Settings → Theme: the whole look in one folder with a theme.json, found
    // as skins are (assets/themes, the data folder's themes). The themes
    // there are, [{ id, name }] by name.
    Q_INVOKABLE QVariantList themes() const;
    // One theme read for QML (Main.qml's root.theme): { id, name, colors,
    // skin, effects: { text, background, selector, screen, transition },
    // music }, those it has. colors is a scheme's name or { primary, surface
    // }; skin as skin() reads one; each effect a preset's name or its own
    // ({ its numbers, 0 to 1; animate; shader, a .qsb of its own }); music a
    // sound file's URL. Empty for none, or for one gone or unreadable.
    Q_INVOKABLE QVariantMap theme(const QString &id) const;
    // One of the effects' shaders built into the app (shaders/<name>.frag),
    // as a URL; "" in a build without them (one made without Qt Shader
    // Tools).
    Q_INVOKABLE QString effectShader(const QString &name) const;
    // The file picker's (views/FilePicker.qml) places, the folders its tree
    // starts from: home, where drives and partitions are mounted (/media,
    // /run/media/<user>, /Volumes) and the root, those there are, as
    // [{ name, path }].
    Q_INVOKABLE QVariantList filePlaces() const;
    // A folder's entries for the file picker, [{ name, path, isFolder }]: its
    // folders, then its files of these types (lower case; none, no files),
    // each by name, hidden ones left out.
    Q_INVOKABLE QVariantList folderEntries(const QString &path, const QStringList &fileTypes) const;
    Q_INVOKABLE QVariant requestFolderEntries(const QString &path, const QStringList &fileTypes);
    Q_INVOKABLE void clearFolderCache();
    Q_INVOKABLE QString localIpAddress() const;
    // The licence's text (LICENSE next to the app, the GNU GPL v3), its
    // paragraphs each on one line so a view wraps them to its width; "" when
    // the file is missing.
    Q_INVOKABLE QString licenseText() const;
    Q_INVOKABLE QString startupModuleEntryPoint() const;
    // The QML entry point of an enabled module ("modules/<folder>/<entry>"),
    // as startupModuleEntryPoint() gives the startup module's; "" for an
    // unknown or disabled one. The startup favourite opens its module with it.
    Q_INVOKABLE QString moduleEntryPoint(const QString &moduleId) const;
    Q_INVOKABLE QString get_module_auth_state(const QString &moduleId);
    // Enabled state of a module by id, resolved the same way the module list
    // resolves it (config override, else manifest default, else true). Unknown
    // ids are not enabled.
    Q_INVOKABLE bool is_module_enabled(const QString &moduleId) const;
    // APP_ROOT-relative QML entry point of a module by id ("modules/plex/views/Root.qml"),
    // or empty when the module is unknown. Lets one module route into another
    // without hardcoding a path across module boundaries.
    Q_INVOKABLE QString module_entry_point(const QString &moduleId) const;
    // The module a view's source belongs to (".../modules/<folder>/..."), or
    // "" for one of the app's own views.
    Q_INVOKABLE QString moduleIdForSource(const QString &source) const;

    // A module's own lists of entries ({ name, path, … } maps, newest first),
    // such as what was watched recently and the favourites, kept in
    // <dataRoot>/lists.json. An entry is known by its path.
    Q_INVOKABLE QVariantList get_list(const QString &moduleId, const QString &name) const;
    // Puts the entry first, in place of an earlier one with its path, and
    // keeps the newest `limit`.
    Q_INVOKABLE void add_to_list(const QString &moduleId, const QString &name,
                                 const QVariantMap &entry, int limit = 50);
    Q_INVOKABLE void remove_from_list(const QString &moduleId, const QString &name,
                                      const QString &path);
    Q_INVOKABLE bool list_contains(const QString &moduleId, const QString &name,
                                   const QString &path) const;

    // Registers a module backend: stores it for action routing, exposes it to QML under
    // contextProperty, and connects its optional signals/slots by introspection (only
    // those the backend actually declares). The module ID is stated once, here.
    void registerModule(const QString &moduleId, const QString &contextProperty,
                        QObject *backend, QQmlContext *ctx);

signals:
    void folderEntriesReady(const QString &path);
    void modulesLoaded(const QVariantList &modules);
    void appSettingChanged(const QString &key, const QString &value);
    void moduleSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);
    void dynamicOptionsReady(const QString &moduleId, const QString &key, const QVariant &options);
    void moduleAuthStateChanged(const QString &moduleId);

private slots:
    // Receive a backend's signal and re-emit it with the module ID prepended, recovering
    // the module ID via sender() reverse-lookup. Lets registerModule connect any backend
    // generically, with no per-module forwarding lambdas.
    void onBackendDynamicOptions(const QString &key, const QVariant &options);
    void onBackendAuthStateChanged();

private:
    AsyncDirectoryCache *m_directoryCache = nullptr;
    QJsonObject loadConfig() const;
    // False when it couldn't be written (the reason is in the log).
    bool saveConfig(const QJsonObject &config) const;
    QJsonObject loadLists() const;
    void saveLists(const QJsonObject &lists) const;
    QString moduleIdForBackend(QObject *backend) const;
    // Extra top-level menu rows a module's backend wants to contribute. Probed,
    // not connected — see the comment at the call site in scan_for_modules.
    QVariantList menuEntriesForModule(const QString &moduleId) const;
    // Resolve a module's enabled state: config override if present, else the
    // manifest default (an "enabled" setting whose default is "OFF"), else true.
    bool isModuleEnabled(const ModuleEntry &m, const QJsonObject &modulesConfig) const;
    QVariantMap importColorScheme(QJsonObject &obj) const;
    // The folder of the skin or the theme (kind: "skin", "theme") with this
    // id whose skin.json or theme.json reads, the data folder's before the
    // app's, and that JSON; empty when there is none (or the id isn't a
    // folder's name).
    QString lookDir(const QString &kind, const QString &id, QJsonObject *json) const;
    // The skins or the themes there are, id → its JSON, the first found of
    // each id.
    QMap<QString, QJsonObject> looks(const QString &kind) const;

    QString m_appRoot;
    QString m_dataRoot;
    QList<ModuleEntry> m_modules;
    QMap<QString, QObject*> m_backends;
};
