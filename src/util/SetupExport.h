#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

// Settings → System → Export to OSD/OS Setup (views/SetupExport.qml): this
// device's settings as OSD/OS Setup's page (osdos.tv/setup, site/setup)
// takes them, in QR codes. A phone opens the page from them, its form filled
// in, to change what it needs and make a card's file (osdos-setup.json) of.
//
// The settings go as the page's own file would carry them (buildSetup() in
// site/setup/js/core.mjs): OSD/OS's settings on the page's form that differ
// from their defaults, the Arcade's RetroAchievements account (its user,
// the token RetroAchievements gave for the password, which is kept nowhere,
// and the Web API key), the Arcade's favourites, and what the card's setup
// sets: the device's name, the display output, Wi-Fi with its password, the
// time zone, the keyboard and SSH with its keys. pi's password can't go:
// only its hash is kept, where root alone reads it.
//
// The file is compressed (zlib) and written in base64url after the page's
// address and #s=, which a browser never sends on to the page's server: in
// one code, or cut into parts, one code each, as many as the screen's lines
// call for (links()). The page puts the parts back together in any order.
namespace setupexport {

// The page the codes open.
inline const QString kPage = QStringLiteral("https://osdos.tv/setup/");

// What the page's form has: OSD/OS's own settings and each module's, by key,
// with their defaults (Null where there is none), from the page's own rows
// (site/setup/schema-extra.json, built into the app) and the modules'
// manifests, as scripts/build-site.py makes the form of them.
struct Form {
    QHash<QString, QJsonValue> app;
    QHash<QString, QHash<QString, QJsonValue>> modules;
};
// extra: schema-extra.json; manifests: each module's manifest, by its id.
Form formOf(const QJsonObject &extra, const QHash<QString, QJsonObject> &manifests);

// What the device says of itself that the card's setup sets
// (os/stage-osdos/02-system/files/osdos-setup), "" where it isn't known.
struct System {
    QString name;           // the host name
    QString model;          // "pi3", "pi4", "pi5"
    QString preset;         // the display output in force
    QString wifiCountry;    // ISO 3166
    QString ssid;           // the Wi-Fi network joined, with its password
    QString wifiPassword;
    bool    wifiHidden = false;
    QString timezone;       // "Europe/Istanbul"
    QString keyboard;       // an XKB layout: "us", "tr"
    int     ssh = -1;       // -1 unknown, 0 off, 1 on
    bool    sshPasswordLogin = false;
    QStringList sshKeys;    // ~/.ssh/authorized_keys' public keys
};
// The time zone, the keyboard and SSH as Linux's files under root ("" for /)
// say, the user's keys in home; the rest is left for the caller.
void readSystem(System &system, const QString &root, const QString &home);

// The page's file (osdos_setup 1) of config.json's settings on the form, the
// account ({ user, token, key }, as the Arcade keeps it), the system, and the
// Arcade's favourites by set (favorites: { "com.osdos.arcade": [...] }).
QJsonObject build(const QJsonObject &config, const Form &form, const QJsonObject &account, const System &system,
                  const QStringList &favorites = {});

// The byte capacity of a QR code of a version (1-40) at the lowest error
// correction, the most it holds.
int capacity(int version);

// The file as the codes' links, each at most maxBytes long: the page's
// address, #s=, then <part>.<parts>.<id>.<text> (id the same for every part
// of one file, to tell two exports' parts apart). Empty when even one part
// can't be had (maxBytes too small for the address).
QStringList links(const QJsonObject &setup, int maxBytes);

// A link drawn as a QR code: one pixel a module, dark on white, with a quiet
// zone of quiet modules round it; the smallest version that holds it. A null
// image when nothing holds it.
QImage codeImage(const QString &text, int quiet = 2);

} // namespace setupexport
