#include "util/SetupExporter.h"

#include "AppCore.h"
#include "display/DisplayOutput.h"
#include "util/Board.h"
#include "util/DeviceName.h"
#include "util/SetupExport.h"
#include "wifi/WifiManager.h"

#include <QDir>
#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

SetupExporter::SetupExporter(const QString &dataRoot, AppCore *appCore, DisplayOutput *displayOutput,
                             WifiManager *wifi, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_appCore(appCore), m_displayOutput(displayOutput), m_wifi(wifi) {}

void SetupExporter::make(int maxVersion) {
    clear();
    const int run = ++m_run;
    m_busy = true;
    emit changed();
    // What the card's setup sets is an OSD/OS card's alone (the display
    // output's presets are there): a computer's own name, Wi-Fi and the
    // rest are no card's.
    if (m_wifi && m_displayOutput && m_displayOutput->available()) {
        m_wifi->readJoined([this, maxVersion, run](const QVariantMap &joined) { finish(joined, maxVersion, run); });
        return;
    }
    finish({}, maxVersion, run);
}

void SetupExporter::clear() {
    ++m_run;
    m_links.clear();
    m_carried.clear();
    m_missing.clear();
    m_busy = false;
    emit changed();
}

void SetupExporter::finish(const QVariantMap &joined, int maxVersion, int run) {
    // Cleared meanwhile (the page closed): nothing kept.
    if (run != m_run)
        return;

    // The page's form: its own rows, built into the app, and the modules'.
    QFile extraFile(QStringLiteral(":/setup/schema-extra.json"));
    const QJsonObject extra = extraFile.open(QIODevice::ReadOnly)
                                  ? QJsonDocument::fromJson(extraFile.readAll()).object() : QJsonObject();
    QHash<QString, QJsonObject> manifests;
    const QVariantList modules = m_appCore ? m_appCore->get_installed_modules().toList() : QVariantList();
    for (const QVariant &m : modules) {
        const QString id = m.toMap().value(QStringLiteral("id")).toString();
        manifests.insert(id, QJsonObject{ { QStringLiteral("settings"),
                                            QJsonArray::fromVariantList(
                                                m_appCore->get_module_settings_schema(id).toList()) } });
    }
    const setupexport::Form form = setupexport::formOf(extra, manifests);
    const QJsonObject config = m_appCore ? QJsonObject::fromVariantMap(m_appCore->get_settings().toMap())
                                         : QJsonObject();

    // The Arcade's RetroAchievements account, as it keeps it.
    QFile accountFile(m_dataRoot + QStringLiteral("/arcade/retroachievements.json"));
    const QJsonObject account = accountFile.open(QIODevice::ReadOnly)
                                    ? QJsonDocument::fromJson(accountFile.read(64 * 1024)).object() : QJsonObject();

    setupexport::System system;
    m_missing.clear();
    m_carried.clear();
    if (m_displayOutput && m_displayOutput->available()) {
        system.name = device::name();
        static const QHash<board::Family, QString> kModels{ { board::Family::Pi3, QStringLiteral("pi3") },
                                                            { board::Family::Pi4, QStringLiteral("pi4") },
                                                            { board::Family::Pi5, QStringLiteral("pi5") } };
        system.model = kModels.value(board::family());
        system.preset = m_displayOutput->current();
        system.wifiCountry = m_wifi ? m_wifi->country() : QString();
        if (!joined.value(QStringLiteral("ssid")).toString().isEmpty()) {
            if (joined.value(QStringLiteral("read")).toBool()) {
                system.ssid = joined.value(QStringLiteral("ssid")).toString();
                system.wifiPassword = joined.value(QStringLiteral("password")).toString();
                system.wifiHidden = joined.value(QStringLiteral("hidden")).toBool();
            } else {
                m_missing << QStringLiteral("Wi-Fi: %1's password couldn't be read")
                                 .arg(joined.value(QStringLiteral("ssid")).toString());
            }
        }
        setupexport::readSystem(system, legacy::env("SYSTEM_ROOT"), QDir::homePath());
        m_missing << QStringLiteral("pi's password: only its hash is kept, which the app can't read");
    }

    // The Arcade's favourites, by set.
    QStringList favorites;
    const QVariantList listed = m_appCore ? m_appCore->get_list(QStringLiteral("com.osdos.arcade"), QStringLiteral("favorites"))
                                          : QVariantList();
    for (const QVariant &v : listed) {
        const QString path = v.toMap().value(QStringLiteral("path")).toString();
        if (path.startsWith(QLatin1String("game:")) && !favorites.contains(path.mid(5)))
            favorites << path.mid(5);
    }
    const QJsonObject setup = setupexport::build(config, form, account, system, favorites);
    if (!system.wifiPassword.isEmpty())
        m_carried << QStringLiteral("the Wi-Fi password");
    if (!account.value(QStringLiteral("token")).toString().isEmpty())
        m_carried << QStringLiteral("the RetroAchievements sign-in");
    if (!account.value(QStringLiteral("key")).toString().isEmpty())
        m_carried << QStringLiteral("the Web API key");
    if (system.ssh == 1 && !system.sshKeys.isEmpty())
        m_carried << QStringLiteral("SSH keys");

    m_links = setupexport::links(setup, setupexport::capacity(std::clamp(maxVersion, 1, 40)));
    if (m_links.isEmpty())
        m_missing << QStringLiteral("The screen is too small for the codes");
    m_busy = false;
    emit changed();
}

QImage QrImageProvider::requestImage(const QString &id, QSize *size, const QSize &) {
    const QImage image = setupexport::codeImage(QUrl::fromPercentEncoding(id.toUtf8()));
    if (size)
        *size = image.size();
    return image;
}
