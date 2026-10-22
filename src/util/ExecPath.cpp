#include "ExecPath.h"

#include <QByteArray>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace execpath {

void primeSystemPath()
{
#ifdef Q_OS_MACOS
    // In this order of preference: Apple silicon's Homebrew before an Intel
    // one's, whose mpv would run under Rosetta. Prepended together, so the
    // order holds (one at a time, the last prepended would come first).
    const QStringList extraPaths = { QStringLiteral("/opt/homebrew/bin"),
                                     QStringLiteral("/usr/local/bin") };
    const QString current = qEnvironmentVariable("PATH");
    const QStringList entries = current.split(QLatin1Char(':'));
    QStringList missing;
    for (const QString &p : extraPaths) {
        if (!entries.contains(p))
            missing << p;
    }
    if (!missing.isEmpty())
        qputenv("PATH", (missing.join(QLatin1Char(':')) + QLatin1Char(':') + current).toUtf8());
#endif
}

} // namespace execpath
