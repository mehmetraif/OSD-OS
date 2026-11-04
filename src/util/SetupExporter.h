#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>

class AppCore;
class DisplayOutput;
class WifiManager;

// Settings → System → Export to OSD/OS Setup (views/SetupExport.qml), as the
// context property "setupExport": make() gathers the device's settings
// (util/SetupExport.h) into links, each drawn as a QR code by the "qr" image
// provider (image://qr/<the link, percent-encoded>). The links hold
// passwords: they are made when the page opens and cleared as it closes.
class SetupExporter : public QObject {
    Q_OBJECT
    // The codes' links, in order: one, or the parts of one.
    Q_PROPERTY(QStringList links READ links NOTIFY changed)
    // Gathering (the Wi-Fi password is NetworkManager's to give).
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What the codes carry that gets a card in, to be careful with, as the
    // page says of a file it reads: "the Wi-Fi password", "SSH keys"…
    Q_PROPERTY(QStringList carried READ carried NOTIFY changed)
    // What couldn't go, a line each.
    Q_PROPERTY(QStringList missing READ missing NOTIFY changed)

public:
    SetupExporter(const QString &dataRoot, AppCore *appCore, DisplayOutput *displayOutput, WifiManager *wifi,
                  QObject *parent = nullptr);

    QStringList links() const { return m_links; }
    bool busy() const { return m_busy; }
    QStringList carried() const { return m_carried; }
    QStringList missing() const { return m_missing; }

    // The links for codes of at most this version (1-40), the largest the
    // screen shows legibly.
    Q_INVOKABLE void make(int maxVersion);
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    void finish(const QVariantMap &joined, int maxVersion, int run);

    QString m_dataRoot;
    AppCore *m_appCore = nullptr;
    DisplayOutput *m_displayOutput = nullptr;
    WifiManager *m_wifi = nullptr;
    QStringList m_links;
    QStringList m_carried;
    QStringList m_missing;
    bool m_busy = false;
    int m_run = 0;   // make()'s, so that one cleared meanwhile ends there
};

// image://qr/<text>: the text as a QR code, one pixel a module.
class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
