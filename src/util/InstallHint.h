#pragma once
#include <QFile>
#include <QString>
#include <QStringList>

// What to do about a program OSD/OS needs and can't find, on the system it
// runs on, for a view to put under its "… is not installed": "On Raspberry
// Pi OS: sudo apt install mame", "On macOS: brew install mame", "On FreeBSD:
// pkg install mame, as root", "On OpenBSD: doas pkg_add mame", on Windows
// where to put the program, and on another Linux its distribution's own
// package manager where the distribution is known from its os-release (apt,
// dnf, pacman, zypper), else the program's name alone. Header only: AppCore
// hands it to QML (installHint()), a backend puts it after its own message.
namespace installhint {

// The system: what the hint calls it, and how it installs a package ("apt",
// "dnf", "pacman", "zypper", "brew", "pkg", "pkg_add", "windows", or "" for
// a Linux of a kind not known here).
struct System {
    QString name;
    QString installer;
};

// A Linux from its os-release file (ID, ID_LIKE, NAME), Raspberry Pi OS
// where it is Debian's on a Pi (raspberryPi: /etc/rpi-issue is there).
inline System linuxSystem(const QString &osRelease, bool raspberryPi) {
    QString id, like, name;
    for (const QString &line : osRelease.split(QLatin1Char('\n'))) {
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                                  || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))))
            value = value.mid(1, value.size() - 2);
        if (key == QLatin1String("ID"))
            id = value.toLower();
        else if (key == QLatin1String("ID_LIKE"))
            like = value.toLower();
        else if (key == QLatin1String("NAME"))
            name = value;
    }
    // The distribution and those it is like, as words: "linuxmint ubuntu
    // debian", "opensuse-tumbleweed suse opensuse".
    const QStringList family = (id + QLatin1Char(' ') + like).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto among = [&family](std::initializer_list<const char *> names) {
        for (const char *candidate : names) {
            if (family.contains(QLatin1String(candidate)))
                return true;
        }
        return false;
    };
    System system;
    // Raspberry Pi OS is Debian's release on a Pi; Ubuntu on one is Ubuntu.
    const bool piOs = raspberryPi && (id == QLatin1String("debian") || id == QLatin1String("raspbian"));
    system.name = piOs ? QStringLiteral("Raspberry Pi OS") : name.isEmpty() ? QStringLiteral("Linux") : name;
    if (among({ "debian", "ubuntu", "raspbian" }))
        system.installer = QStringLiteral("apt");
    else if (among({ "fedora", "rhel", "centos" }))
        system.installer = QStringLiteral("dnf");
    else if (among({ "arch", "manjaro" }))
        system.installer = QStringLiteral("pacman");
    else if (among({ "suse", "opensuse" }) || id.startsWith(QLatin1String("opensuse")))
        system.installer = QStringLiteral("zypper");
    return system;
}

// The system OSD/OS runs on, read once.
inline System current() {
    static const System cached = [] {
#if defined(Q_OS_WIN)
        return System{ QStringLiteral("Windows"), QStringLiteral("windows") };
#elif defined(Q_OS_MACOS)
        return System{ QStringLiteral("macOS"), QStringLiteral("brew") };
#elif defined(Q_OS_FREEBSD)
        return System{ QStringLiteral("FreeBSD"), QStringLiteral("pkg") };
#elif defined(Q_OS_OPENBSD)
        return System{ QStringLiteral("OpenBSD"), QStringLiteral("pkg_add") };
#else
        QString release;
        for (const char *path : { "/etc/os-release", "/usr/lib/os-release" }) {
            QFile file{ QString::fromLatin1(path) };
            if (file.open(QIODevice::ReadOnly)) {
                release = QString::fromUtf8(file.readAll());
                break;
            }
        }
        return linuxSystem(release, QFile::exists(QStringLiteral("/etc/rpi-issue")));
#endif
    }();
    return cached;
}

// The package a program comes in, where that isn't its own name: the web
// player's browser (Chromium, with Widevine and its screen without a desktop
// where Raspberry Pi OS packages them; Google Chrome on a Mac, whose sign-in
// yt-dlp can read), its screen alone (cage, with wtype), and Homebrew's
// RetroArch, which is an app.
inline QString packageOf(const QString &program, const System &on) {
    if (program == QLatin1String("browser")) {
        if (on.name == QLatin1String("Raspberry Pi OS"))
            return QStringLiteral("chromium libwidevinecdm0 cage wtype");
        if (on.installer == QLatin1String("brew"))
            return QStringLiteral("--cask google-chrome");
        return QStringLiteral("chromium");
    }
    if (program == QLatin1String("cage"))
        return QStringLiteral("cage wtype");
    if (program == QLatin1String("retroarch") && on.installer == QLatin1String("brew"))
        return QStringLiteral("--cask retroarch");
    return program;
}

// The hint for a program ("mame", "retroarch", "yt-dlp", "mpv", "browser",
// "cage") on a system; "" where there is nothing to say (cage on a system
// with a desktop of its own).
inline QString forProgram(const QString &program, const System &on) {
    const QString &installer = on.installer;
    QString what;
    if (installer == QLatin1String("windows")) {
        if (program == QLatin1String("mame"))
            what = QStringLiteral("Settings → Arcade → MAME downloads it, or unpack MAME from mamedev.org beside OSD/OS "
                                  "as a folder named MAME");
        else if (program == QLatin1String("retroarch"))
            what = QStringLiteral("Settings → Arcade → FinalBurn Neo downloads RetroArch, or unpack RetroArch from "
                                  "retroarch.com beside OSD/OS as a folder named RetroArch");
        else if (program == QLatin1String("yt-dlp"))
            what = QStringLiteral("put yt-dlp.exe from github.com/yt-dlp/yt-dlp beside OSD/OS");
        else if (program == QLatin1String("mpv"))
            what = QStringLiteral("unpack mpv from mpv.io beside OSD/OS as a folder named mpv");
        else if (program == QLatin1String("browser"))
            what = QStringLiteral("Netflix and Prime Video play in Chromium on Linux and in Chrome on a Mac, not on Windows");
        else
            return QString();
    } else if (installer == QLatin1String("brew")) {
        if (program == QLatin1String("cage"))
            return QString();
        what = QStringLiteral("brew install ") + packageOf(program, on);
    } else if (installer == QLatin1String("pkg")) {
        what = QStringLiteral("pkg install ") + packageOf(program, on) + QStringLiteral(", as root");
    } else if (installer == QLatin1String("pkg_add")) {
        what = QStringLiteral("doas pkg_add ") + packageOf(program, on);
    } else if (installer == QLatin1String("apt")) {
        what = QStringLiteral("sudo apt install ") + packageOf(program, on);
    } else if (installer == QLatin1String("dnf")) {
        what = QStringLiteral("sudo dnf install ") + packageOf(program, on);
        if (program == QLatin1String("mame") || program == QLatin1String("retroarch"))
            what += QStringLiteral(" (from RPM Fusion)");
    } else if (installer == QLatin1String("pacman")) {
        what = QStringLiteral("sudo pacman -S ") + packageOf(program, on);
    } else if (installer == QLatin1String("zypper")) {
        what = QStringLiteral("sudo zypper install ") + packageOf(program, on);
    } else {
        what = QStringLiteral("install ") + packageOf(program, on)
               + QStringLiteral(" with the distribution's package manager");
    }
    return QStringLiteral("On ") + on.name + QStringLiteral(": ") + what;
}

// The hint for a program on the system OSD/OS runs on.
inline QString forProgram(const QString &program) {
    return forProgram(program, current());
}

} // namespace installhint
