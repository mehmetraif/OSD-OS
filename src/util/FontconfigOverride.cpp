#include "FontconfigOverride.h"
#include "AtomicFile.h"
#include "RuntimeFiles.h"

namespace fcoverride {

QString write(const QString &fontsDir) {
    // Among the run's files (util/RuntimeFiles.h), not under a fixed name in
    // the temp folder, where another account could put its own first.
    const QString path = runtimefiles::path(QStringLiteral("osdos-fonts.conf"));
    if (path.isEmpty())
        return {};
    const QByteArray config = QString(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n"
        "  <dir>%1</dir>\n"
        "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
        "  <include ignore_missing=\"yes\">/usr/local/etc/fonts/fonts.conf</include>\n"   // FreeBSD's
        "</fontconfig>\n"
    ).arg(fontsDir.toHtmlEscaped()).toUtf8();   // an & or < in the path is XML's
    // One cut short is no configuration at all: mpv is given none then, and
    // keeps the system's fonts.
    return writeFileAnew(path, config) ? path : QString();
}

} // namespace fcoverride
