#include "MpvLocator.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace mpvbin {

QString locate()
{
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    // mpv.exe beside the app, or mpv's own folder (its builds unpack into one)
    // put beside it, before the PATH.
    for (const QString &candidate : { appDir + "/mpv.exe", appDir + "/mpv/mpv.exe" }) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
#else
    const QString siblingMpv = appDir + "/mpv";
    if (QFileInfo(siblingMpv).isExecutable())
        return siblingMpv;
#endif

    return QStandardPaths::findExecutable("mpv");
}

} // namespace mpvbin
