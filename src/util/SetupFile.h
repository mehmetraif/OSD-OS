#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// OSD/OS Setup's file (osdos-setup.json, made on osdos.tv/setup from
// site/setup) on a computer: the parts the app sets itself, its own
// settings and the Arcade's RetroAchievements account, as an OSD/OS card's
// applier sets them before the app starts there
// (os/stage-osdos/02-system/files/osdos-setup, its stage_app() and
// user_files()). The rest of the file, the device's name, the display
// output, Wi-Fi, the password, SSH, the time zone and the keyboard, is the
// card's: named as not set here.
namespace setupfile {

// The file's name, wherever it is put.
inline const QString kFileName = QStringLiteral("osdos-setup.json");

struct Setup {
    // config.app and config.modules, either or both.
    QJsonObject config;
    // The RetroAchievements account: user and password, the Web API key, or
    // both, as the card's retroachievements() keeps them.
    QJsonObject account;
    // The parts of the file only a card sets, by name, in the file's order.
    QStringList cardOnly;
};

// What became of a file: a line for each part, whether all of them were
// set, and how many of the app's two (its settings, the account) were.
struct Result {
    QStringList lines;
    bool ok = true;
    int applied = 0;
};

// The notice's title for a file taken as the app started: "OSD/OS Setup
// applied", partly, not, or nothing in it for the app.
QString noticeTitle(const Result &result);

// The text as OSD/OS Setup's: false, with why ("not JSON"), when it isn't one
// this OSD/OS takes. Checked as the card checks it: 1 MB at most, UTF-8 with
// a Windows editor's BOM or without, no number JSON can't hold, and version 1.
bool parse(const QByteArray &data, Setup *setup, QString *why);

// Objects merged key by key, anything else replaced; what `update` leaves
// out stays as it is (the card's merge()).
QJsonObject merged(const QJsonObject &into, const QJsonObject &update);

// The settings an object holds: its values, an object's counted by its own.
int count(const QJsonObject &values);

// The RetroAchievements account handed to the Arcade module, which takes it
// as it starts (ArcadeBackend::takeSetupAccount()): the key kept, and
// RetroArch signing in with the password. Owner-only, beside its other
// files in <data>/arcade. False, with why, when it couldn't be written.
bool writeAccount(const QJsonObject &account, const QString &dataRoot, QString *why);

// The lines saying what of a Setup was set: its settings (how many), its
// account, and what a card alone sets.
QString settingsLine(int settings);
QString accountLine(const QJsonObject &account);
QString cardOnlyLine(const QStringList &parts);

// Where the app looks for the file as it starts: its data folder, then its
// own folder, where the file is put as on a card: the folder of osdos.exe on
// Windows, the one the app is in on a Mac, the AppImage's.
QStringList places(const QString &dataRoot);

// As the app starts, before anything has read config.json: each file found
// in places() taken at once, deleted with its passwords before anything of
// it is set, then applied: config.json merged, the account handed on. One
// that can't be deleted is left as it is and not applied, as it would be at
// every start. No lines when there is none.
Result applyAtStart(const QString &dataRoot);

} // namespace setupfile
