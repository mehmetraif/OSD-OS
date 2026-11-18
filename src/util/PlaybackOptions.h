#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The choices of the playback settings Local Files and the NFC Reader both
// have, for their options_slot (dynamicOptionsReady), the same in each.
namespace playbackoptions {

// Resume Playback.
inline QVariantList resumePlayback() {
    QVariantList options;
    QVariantMap ask; ask["id"] = "ask"; ask["label"] = "Ask";
    QVariantMap yes; yes["id"] = "yes"; yes["label"] = "Always";
    QVariantMap no;  no["id"]  = "no";  no["label"]  = "Never";
    options << ask << yes << no;
    return options;
}

// Auto Show Subtitles. It was once On or Off, saved as true or false: `old`
// is the choice each of those is now.
inline QVariantList autoSubtitles() {
    QVariantList options;
    QVariantMap forced; forced["id"] = "forced"; forced["label"] = "Forced Only"; forced["old"] = false;
    QVariantMap on;     on["id"] = "on";         on["label"] = "On";              on["old"] = true;
    QVariantMap off;    off["id"] = "off";       off["label"] = "Off";
    options << forced << on << off;
    return options;
}

// Subtitle Language: Any (no preference, "-"), then the languages of ISO
// 639-1, each name once, from the one list the app has of them, in Local
// Files' folder (where the site's build reads it too, scripts/build-site.py).
// None where the list can't be read.
inline QVariantList subtitleLanguages(const QString &appRoot) {
    QStringList addedLabels;
    QVariantList options;

    QFile file(appRoot + "/modules/local_files/iso639-1.json");
    if (!file.open(QIODevice::ReadOnly))
        return options;

    options.append(QVariantMap{{"id","-"},{"label","Any"}});

    QVariantList locList = QJsonDocument::fromJson(file.readAll()).toVariant().toList();
    for (const QVariant loc : locList)
    {
        QVariantMap langOption = QVariantMap{{"id",loc.toJsonObject()["id"].toString()},{"label",loc.toJsonObject()["label"].toString()}};
        if (langOption["label"].toString() == "" || addedLabels.contains(langOption["label"].toString())) continue;
        addedLabels.append(langOption["label"].toString());
        options.append(langOption);
    }
    return options;
}

} // namespace playbackoptions
