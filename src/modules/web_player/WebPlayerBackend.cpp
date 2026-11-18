#include "WebPlayerBackend.h"
#include "../scripts/ScriptLauncher.h"
#include "../../AppCore.h"
#include "../../util/DisplayHandoff.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

namespace {

// The browsers web-player.sh looks for, in its order. Keep the two in step.
const char *const kBrowsers[] = { "chromium", "chromium-browser",
                                  "google-chrome-stable", "google-chrome" };

// How long close() gives the browser to close before stopping it: a page may
// ask to stay, and nothing here can answer.
constexpr int kCloseGraceMs = 5000;

bool haveBrowser() {
#ifdef Q_OS_MACOS
    // web-player.sh falls back to Safari, which every Mac has.
    return true;
#else
    for (const char *name : kBrowsers) {
        if (!QStandardPaths::findExecutable(QLatin1String(name)).isEmpty())
            return true;
    }
    return false;
#endif
}

} // namespace

WebPlayerBackend::WebPlayerBackend(const Service &service,
                                   const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                                   DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_service(service.id), m_url(service.homeUrl),
      m_signInUrl(service.signInUrl), m_needsChromium(service.needsChromium),
      m_appRoot(appRoot), m_dataRoot(dataRoot), m_appCore(appCore)
{
    if (!service.catalog.providerName.isEmpty())
        m_catalog = new TmdbCatalog(m_dataRoot, m_appCore, service.catalog, this);
    // The catalogue's settings as they stand.
    if (m_catalog && m_appCore) {
        for (const QString &key : { QStringLiteral("region"), QStringLiteral("catalog_language") })
            applySetting(key, m_appCore->get_setting(moduleId(), key));
    }

    m_launcher = new ScriptLauncher(m_appRoot, m_dataRoot, handoff, this);
    m_launcher->setHandoffOwner(m_service);
    connect(m_launcher, &ScriptLauncher::runningChanged,
            this, &WebPlayerBackend::runningChanged);
    connect(m_launcher, &ScriptLauncher::finished,
            this, &WebPlayerBackend::finished);

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    m_closeTimer->setInterval(kCloseGraceMs);
    connect(m_closeTimer, &QTimer::timeout, this, &WebPlayerBackend::stop);
    connect(m_launcher, &ScriptLauncher::finished, m_closeTimer, &QTimer::stop);
}

QString WebPlayerBackend::script() const {
    return m_appRoot + QStringLiteral("/scripts/web-player.sh");
}

bool WebPlayerBackend::running() const {
    return m_launcher->isBusy();
}

QString WebPlayerBackend::browserProfile() const {
    // web-player.sh's PROFILE: keep the two in step.
    return m_dataRoot + QLatin1Char('/') + m_service + QStringLiteral("/browser");
}

void WebPlayerBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                        const QVariant &value) {
    if (moduleId == this->moduleId())
        applySetting(key, value);
}

void WebPlayerBackend::applySetting(const QString &key, const QVariant &value) {
    if (!m_catalog)
        return;
    if (key == QLatin1String("region")) {
        m_catalog->setRegion(value.toString());
    } else if (key == QLatin1String("catalog_language")) {
        const QString language = value.toString();
        if (!language.isEmpty())
            m_catalog->setLanguage(language == QLatin1String("Turkish") ? QStringLiteral("tr-TR")
                                                                        : QStringLiteral("en-US"));
    }
}

bool WebPlayerBackend::launch(const QString &url) {
    m_lastError.clear();

    const QString script = this->script();
    if (!QFileInfo::exists(script)) {
        m_lastError = QStringLiteral("web-player.sh is missing from the app");
        return false;
    }
    if (!haveBrowser()) {
        m_lastError = QStringLiteral("Chromium is not installed");
        return false;
    }
#ifdef Q_OS_MACOS
    // web-player.sh's Safari fallback keeps its sign-in where nothing reads it.
    if (m_needsChromium && !QFileInfo::exists(QStringLiteral("/Applications/Google Chrome.app"))) {
        m_lastError = QStringLiteral("Google Chrome is not installed");
        return false;
    }
#endif
    // Without a desktop there is nothing for a browser to open a window on;
    // cage gives it a screen of its own.
    if (DisplayHandoff::isHeadless()
        && QStandardPaths::findExecutable(QStringLiteral("cage")).isEmpty()) {
        m_lastError = QStringLiteral("cage is not installed");
        return false;
    }

    ScriptEntry entry;
    entry.path        = script;
    entry.basename    = QStringLiteral("web-player.sh");
    entry.meta.name   = m_service;
    entry.meta.mode   = QStringLiteral("takeover");
    // The browser is a family of processes; the screen comes back once all
    // of them have gone.
    entry.meta.wait   = QStringLiteral("pgroup");
    entry.meta.args   = QStringLiteral("\"%1\" \"%2\" \"%3\"")
                            .arg(m_service, url.isEmpty() ? m_url : url, scaling());

    QString error;
    if (!m_launcher->start(entry, &error)) {
        m_lastError = error;
        return false;
    }
    if (!m_opened) {
        m_opened = true;
        emit openedChanged();
    }
    return true;
}

QString WebPlayerBackend::scaling() const {
    // Read as the player opens: the app's settings never reach a module
    // backend's onSettingChanged.
    const auto setting = [this](const QString &moduleId, const QString &key) {
        return m_appCore ? m_appCore->get_setting(moduleId, key).toString() : QString();
    };
    QString value = setting(moduleId(), QStringLiteral("video_scaling"));
    if (value.isEmpty() || value == QLatin1String("Default")) {
        value = setting(QString(), QStringLiteral("video_scaling"));
        if (value.isEmpty() && setting(QString(), QStringLiteral("auto_crop")) == QLatin1String("On"))
            value = QStringLiteral("Pan & Scan");
    }
    if (value == QLatin1String("14:9"))
        return QStringLiteral("14:9");
    if (value == QLatin1String("Pan & Scan"))
        return QStringLiteral("panscan");
    if (value == QLatin1String("Anamorphic"))
        return QStringLiteral("anamorphic");
    return QStringLiteral("letterbox");
}

QString WebPlayerBackend::output() const {
    return m_launcher->outputText();
}

void WebPlayerBackend::close() {
    if (!m_launcher->isBusy() || m_closeTimer->isActive())
        return;
    // The way Ctrl+W closes it, the browser saves what it holds first:
    // Chromium writes new cookies, a sign-in among them, only every half
    // minute, and stopping it outright loses them. web-player.sh --close
    // fails where it can't ask that (no wtype, a cage too old, a desktop).
    m_closeTimer->start();
    auto *closer = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DATA_ROOT"), m_dataRoot);
    closer->setProcessEnvironment(env);
    connect(closer, &QProcess::finished, this,
            [this, closer](int exitCode, QProcess::ExitStatus status) {
                closer->deleteLater();
                if (status != QProcess::NormalExit || exitCode != 0)
                    stop();
            });
    connect(closer, &QProcess::errorOccurred, this,
            [this, closer](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                closer->deleteLater();
                stop();
            });
    closer->start(QStringLiteral("/bin/sh"), { script(), QStringLiteral("--close"), m_service });
}

void WebPlayerBackend::stop() {
    m_closeTimer->stop();
    if (m_launcher->isBusy())
        m_launcher->requestStop();
}

void WebPlayerBackend::signOut() {
    if (m_launcher->isBusy()) {
        qWarning("[WebPlayer] Not signing out of %s while the browser is open",
                 qPrintable(m_service));
        return;
    }
    if (QDir(browserProfile()).removeRecursively())
        qInfo("[WebPlayer] Signed out of %s: browser profile removed", qPrintable(m_service));
}
