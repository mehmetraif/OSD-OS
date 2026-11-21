#pragma once
#include "ProgramInstaller.h"
#include <QString>
#include <QUrl>

// Settings → Arcade → FinalBurn Neo's download, for an install the OSD/OS
// image didn't bring it with: the core from libretro's build server, and its
// list of games and its licence from FinalBurn Neo's own sources, each put
// in place (libretro::downloadedCore() and the rest) only once it is whole:
// the core taken out of its zip, the list read to be one. Unpacking needs
// minizip (OSDOS_MINIZIP), as the High Scores module does.
class FbneoInstaller : public Installer {
    Q_OBJECT
public:
    struct Source {
        QUrl core;      // a zip with the core in it
        QUrl dat;
        QUrl license;
    };
    // libretro's nightly build of the core for this machine (Linux on arm64
    // or x86_64, a Mac on either), FinalBurn Neo's arcade DAT and licence
    // from its sources; no core URL for a machine libretro builds none for.
    static Source defaultSource();

    explicit FbneoInstaller(const QString &dataRoot, QObject *parent = nullptr);

    // Downloads the three; finished() follows. Nothing is changed before all
    // three are whole: a download cut short leaves what was there.
    void start(const Source &source);

private:
    void fetch(int index);
    void unpack();
    QString what() const override { return QStringLiteral("FinalBurn Neo"); }
    // What the files before it brought: tens of megabytes on the card.
    void removeParts() override;

    QString m_dataRoot;
    Source m_source;
};
