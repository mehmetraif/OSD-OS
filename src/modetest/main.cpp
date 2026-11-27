// osdos-modetest: shows a few video modes made for single games on a Pi's DPI
// output (a JAMMA or SCART RGB board), each for a few seconds with a picture
// that says which it is, then puts the mode it found back.
//
// It answers, on the device, what docs/TODO-display-modes.md asks: whether
// the output, the board and the monitor hold a game's own lines and refresh
// rate, set while the system runs. Run it with nothing else on the screen: on
// the OSD/OS image, Settings -> Quit -> Exit to Terminal, then
// /opt/osdos/bin/osdos-modetest. No Qt: libdrm alone.
//
// It never shows a mode whose line rate is not the one the screen is on
// already (modetest::safeFor()): a monitor made for 15 kHz can be damaged by
// another rate.
//
// It also works a game's own mode out as the app does (--game, GameMode),
// and runs a command on one mode (--run): the mode set on a black screen,
// the command started with the screen and with osdos_keepmode.so preloaded,
// as the app starts an emulator with Game's Own Mode on, and the mode it
// found put back once the command ends. That tries by hand, one emulator at
// a time, what the app does at each game's start: whether the emulator keeps
// the mode it finds, and how its picture sits on it.
#include "DrmMode.h"
#include "GameMode.h"
#include "ModeLine.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using modetest::ModeLine;
using modetest::fromDrm;
using modetest::toDrm;

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

const char *kUsage =
    "osdos-modetest: shows video modes made for single games on this screen, one after another,\n"
    "and puts back the mode it found.\n"
    "\n"
    "  osdos-modetest                  the built-in modes, six seconds each\n"
    "  osdos-modetest --list           the screens found and the modes they are on; changes nothing\n"
    "  --seconds <n>                   how long each mode is shown (1 to 60)\n"
    "  --connector <name>              the output to use (DPI-1, as --list names them)\n"
    "  --mode '<modeline>'             a mode of your own in place of the built-in ones; may be given\n"
    "                                  more than once: \"name\" clock hdisplay hsync-start hsync-end htotal\n"
    "                                  vdisplay vsync-start vsync-end vtotal -hsync -vsync\n"
    "  --game <lines>@<Hz>             a game's own mode, worked out as OSD/OS works it out for a 15 kHz\n"
    "                                  arcade monitor, 1920 across: --game 224@59.185606. With a width,\n"
    "                                  that many across: --game 320x224@59.185606. May be given more\n"
    "                                  than once, with --mode or without\n"
    "  --monitor '<range>'             the monitor's ranges for --game, as Switchres writes them, in\n"
    "                                  place of its arcade_15; before the --game it is for\n"
    "  --modelines                     prints the modes as modelines and changes nothing\n"
    "  --run '<command>'               with one mode: sets it on a black screen, runs the command with\n"
    "                                  the screen (through /bin/sh, osdos_keepmode.so preloaded, which\n"
    "                                  makes an emulator keep the mode it finds), says which mode its\n"
    "                                  picture came up on, and puts back the mode it found once it ends\n"
    "\n"
    "Only modes within 5% of the line rate the screen is on are shown. Run it with nothing else on\n"
    "the screen: on the OSD/OS image, Settings -> Quit -> Exit to Terminal.\n";

// "224@59.185606" or "320x224@59.185606" as a game's mode on a monitor of
// that range: 1920 across without a width. False with why.
bool gameMode(const std::string &text, const modetest::MonitorRange &range, ModeLine *mode, std::string *why) {
    int width = 1920, lines = 0, used = 0;
    double refresh = 0;
    const char *p = text.c_str();
    if (std::sscanf(p, "%dx%d%n", &width, &lines, &used) == 2) {
        p += used;
    } else {
        width = 1920;
        if (std::sscanf(p, "%d%n", &lines, &used) != 1) {
            *why = "a game is <lines>@<Hz>, or <width>x<lines>@<Hz>";
            return false;
        }
        p += used;
    }
    // Its rate, with a point: the tool is in the C locale.
    char *end = nullptr;
    if (*p != '@' || (refresh = std::strtod(p + 1, &end), end == p + 1) || *end != '\0') {
        *why = "a game is <lines>@<Hz>, or <width>x<lines>@<Hz>";
        return false;
    }
    return modetest::modeFor(width, lines, refresh, range, mode, why);
}

std::string modeLineOf(const ModeLine &mode) {
    char text[256];
    std::snprintf(text, sizeof text, "\"%s\" %.6f %d %d %d %d %d %d %d %d %chsync %cvsync", mode.name.c_str(),
                  mode.clockMHz, mode.hdisplay, mode.hsyncStart, mode.hsyncEnd, mode.htotal, mode.vdisplay,
                  mode.vsyncStart, mode.vsyncEnd, mode.vtotal, mode.hsyncPositive ? '+' : '-',
                  mode.vsyncPositive ? '+' : '-');
    return text;
}

// osdos_keepmode.so beside this program, as the app's package has the two;
// "" where it isn't there.
std::string keepModeLibrary() {
    char self[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    if (length <= 0)
        return std::string();
    self[length] = '\0';
    std::string path(self);
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos)
        return std::string();
    path = path.substr(0, slash) + "/osdos_keepmode.so";
    return access(path.c_str(), R_OK) == 0 ? path : std::string();
}

std::string connectorName(const drmModeConnector *connector) {
    const char *type = "Unknown";
    switch (connector->connector_type) {
    case DRM_MODE_CONNECTOR_VGA:         type = "VGA"; break;
    case DRM_MODE_CONNECTOR_Composite:   type = "Composite"; break;
    case DRM_MODE_CONNECTOR_SVIDEO:      type = "SVIDEO"; break;
    case DRM_MODE_CONNECTOR_LVDS:        type = "LVDS"; break;
    case DRM_MODE_CONNECTOR_DisplayPort: type = "DP"; break;
    case DRM_MODE_CONNECTOR_HDMIA:       type = "HDMI-A"; break;
    case DRM_MODE_CONNECTOR_HDMIB:       type = "HDMI-B"; break;
    case DRM_MODE_CONNECTOR_TV:          type = "TV"; break;
    case DRM_MODE_CONNECTOR_eDP:         type = "eDP"; break;
    case DRM_MODE_CONNECTOR_VIRTUAL:     type = "Virtual"; break;
    case DRM_MODE_CONNECTOR_DSI:         type = "DSI"; break;
    case DRM_MODE_CONNECTOR_DPI:         type = "DPI"; break;
    default: break;
    }
    return std::string(type) + "-" + std::to_string(connector->connector_type_id);
}

// How well an output suits the test when none is named: a DPI output first
// (a Pi 5's, and a Pi's under full KMS), then DSI, which is what a Pi 4's
// firmware DPI is called under fake KMS ("main LCD"). No other is taken
// without being asked for: the modes here are an arcade monitor's.
int suits(const drmModeConnector *connector) {
    switch (connector->connector_type) {
    case DRM_MODE_CONNECTOR_DPI: return 2;
    case DRM_MODE_CONNECTOR_DSI: return 1;
    default:                     return 0;
    }
}

// The screen a test runs on: a device, an output on it and the CRTC that
// drives it, with what it showed as the test began.
struct Screen {
    int fd = -1;
    std::string device;
    std::string driver;
    std::string connector;
    uint32_t connectorId = 0;
    uint32_t crtcId = 0;
    drmModeCrtc *before = nullptr;
};

// A frame buffer of the test's own: a dumb buffer, XRGB8888, mapped.
struct Picture {
    uint32_t handle = 0;
    uint32_t fbId = 0;
    uint32_t pitch = 0;
    uint64_t size = 0;
    uint32_t *pixels = nullptr;
};

bool makePicture(int fd, int width, int height, Picture *picture) {
    drm_mode_create_dumb create = {};
    create.width = uint32_t(width);
    create.height = uint32_t(height);
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0)
        return false;
    picture->handle = create.handle;
    picture->pitch = create.pitch;
    picture->size = create.size;
    if (drmModeAddFB(fd, uint32_t(width), uint32_t(height), 24, 32, create.pitch, create.handle, &picture->fbId) != 0)
        return false;
    drm_mode_map_dumb map = {};
    map.handle = create.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0)
        return false;
    void *at = mmap(nullptr, size_t(create.size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, off_t(map.offset));
    if (at == MAP_FAILED)
        return false;
    picture->pixels = static_cast<uint32_t *>(at);
    return true;
}

void dropPicture(int fd, Picture *picture) {
    if (picture->pixels)
        munmap(picture->pixels, size_t(picture->size));
    if (picture->fbId)
        drmModeRmFB(fd, picture->fbId);
    if (picture->handle) {
        drm_mode_destroy_dumb destroy = {};
        destroy.handle = picture->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    *picture = Picture();
}

// Every device's outputs, printed for --list, and the one to test on: the
// one named, else the one that suits best (suits()) among those connected
// and showing a picture.
bool findScreen(const std::string &wanted, bool list, Screen *screen) {
    // Where the devices are: OSDOS_MODETEST_DRI stands in for tests.
    const char *dri = std::getenv("OSDOS_MODETEST_DRI");
    const std::string folder = dri && *dri ? dri : "/dev/dri";
    int best = 0;
    int outputs = 0;
    for (int card = 0; card < 16; card++) {
        const std::string device = folder + "/card" + std::to_string(card);
        const int fd = open(device.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        drmModeRes *resources = drmModeGetResources(fd);
        if (!resources) {
            close(fd);
            continue;
        }
        std::string driver = "?";
        if (drmVersion *version = drmGetVersion(fd)) {
            driver = version->name ? version->name : "?";
            drmFreeVersion(version);
        }
        bool taken = false;
        for (int i = 0; i < resources->count_connectors; i++) {
            drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
            if (!connector)
                continue;
            const std::string name = connectorName(connector);
            outputs++;
            uint32_t crtcId = 0;
            if (connector->encoder_id) {
                if (drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id)) {
                    crtcId = encoder->crtc_id;
                    drmModeFreeEncoder(encoder);
                }
            }
            drmModeCrtc *crtc = crtcId ? drmModeGetCrtc(fd, crtcId) : nullptr;
            const bool showing = crtc && crtc->mode_valid;
            if (list) {
                std::printf("%s (%s)  %s  %s", device.c_str(), driver.c_str(), name.c_str(),
                            connector->connection == DRM_MODE_CONNECTED ? "connected"
                            : connector->connection == DRM_MODE_DISCONNECTED ? "not connected" : "unknown");
                if (showing)
                    std::printf("  on %s", modetest::describe(fromDrm(crtc->mode)).c_str());
                else
                    std::printf("  showing nothing");
                std::printf("\n");
            }
            const int rank = !wanted.empty() ? (name == wanted ? 3 : 0)
                           : (connector->connection != DRM_MODE_DISCONNECTED ? suits(connector) : 0);
            if (showing && rank > best) {
                if (screen->before)
                    drmModeFreeCrtc(screen->before);
                if (screen->fd >= 0 && screen->fd != fd)
                    close(screen->fd);
                best = rank;
                screen->fd = fd;
                screen->device = device;
                screen->driver = driver;
                screen->connector = name;
                screen->connectorId = connector->connector_id;
                screen->crtcId = crtcId;
                screen->before = crtc;
                crtc = nullptr;
                taken = true;
            }
            if (crtc)
                drmModeFreeCrtc(crtc);
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);
        if (!taken)
            close(fd);
    }
    if (list && outputs == 0)
        std::printf("No screen's device could be opened under %s.\n", folder.c_str());
    return best > 0;
}

// The mode it found, back: with the frame buffer it had where that can still
// be named, else with a black one of the test's own, which the console
// replaces as the test's device is closed.
void putBack(Screen *screen, Picture *black) {
    drmModeCrtc *before = screen->before;
    if (before->buffer_id
        && drmModeSetCrtc(screen->fd, screen->crtcId, before->buffer_id, before->x, before->y,
                          &screen->connectorId, 1, &before->mode) == 0)
        return;
    if (makePicture(screen->fd, before->mode.hdisplay, before->mode.vdisplay, black)) {
        std::memset(black->pixels, 0, size_t(black->size));
        drmModeSetCrtc(screen->fd, screen->crtcId, black->fbId, 0, 0, &screen->connectorId, 1, &before->mode);
    }
}

// --run: one mode set on a black screen, the command run with the screen as
// OSD/OS runs an emulator on a game's own mode (the device's mastership let
// go for it, osdos_keepmode.so preloaded into it), which mode its picture
// came up on noted meanwhile, and the mode found put back once it ends. The
// report comes last, when the console has the screen again.
int runOn(Screen *screen, const ModeLine &mode, const ModeLine &now, const std::string &command) {
    std::string why;
    if (!modetest::safeFor(mode, now.lineRateHz(), &why)) {
        std::fprintf(stderr, "osdos-modetest: %s is left out: %s\n", modetest::describe(mode).c_str(), why.c_str());
        return 1;
    }
    Picture frame;
    if (!makePicture(screen->fd, mode.hdisplay, mode.vdisplay, &frame)) {
        std::fprintf(stderr, "osdos-modetest: no frame buffer for %s: %s\n", modetest::describe(mode).c_str(),
                     std::strerror(errno));
        dropPicture(screen->fd, &frame);
        return 1;
    }
    std::memset(frame.pixels, 0, size_t(frame.size));
    drmModeModeInfo drm = toDrm(mode);
    // The sync as the screen has it now, as the app sets a game's mode.
    if (screen->before->mode.flags & modetest::kSyncFlags)
        drm.flags = (drm.flags & ~modetest::kSyncFlags) | (screen->before->mode.flags & modetest::kSyncFlags);
    if (drmModeSetCrtc(screen->fd, screen->crtcId, frame.fbId, 0, 0, &screen->connectorId, 1, &drm) != 0) {
        std::fprintf(stderr, "osdos-modetest: %s was refused by the kernel: %s\n", modetest::describe(mode).c_str(),
                     std::strerror(errno));
        dropPicture(screen->fd, &frame);
        return 1;
    }
    // The command is the screen's master now.
    drmDropMaster(screen->fd);
    const std::string library = keepModeLibrary();
    // The card this screen is on, by its number, for an SDL program (MAME):
    // left to itself SDL opens the first card it can use, which on a Pi 5
    // may be HDMI's. The app names the card the same way for the emulators
    // it starts (ScriptLauncher), and a number given already is kept.
    const std::string::size_type digits = screen->device.find_last_not_of("0123456789");
    const std::string card = digits == std::string::npos ? std::string() : screen->device.substr(digits + 1);
    const pid_t child = fork();
    if (child == 0) {
        if (!card.empty())
            setenv("SDL_KMSDRM_DEVICE_INDEX", card.c_str(), 0);
        if (!library.empty()) {
            const char *preloaded = std::getenv("LD_PRELOAD");
            const std::string all = preloaded && *preloaded ? std::string(preloaded) + ":" + library : library;
            setenv("LD_PRELOAD", all.c_str(), 1);
            setenv("OSDOS_KEEPMODE_VERBOSE", "1", 1);
        }
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    // While it runs: the mode its first picture is on.
    bool seen = false, kept = false;
    std::string cameUpOn;
    int status = 0;
    int asked = 0;
    while (child > 0) {
        const pid_t ended = waitpid(child, &status, WNOHANG);
        if (ended == child || (ended < 0 && errno != EINTR))
            break;
        // Stopped from the keyboard: the command first, and no waiting on
        // one that won't go.
        if (g_stop && asked++ % 15 == 0)
            kill(child, asked > 15 ? SIGKILL : SIGTERM);
        if (!seen) {
            if (drmModeCrtc *crtc = drmModeGetCrtc(screen->fd, screen->crtcId)) {
                if (crtc->mode_valid && crtc->buffer_id && crtc->buffer_id != frame.fbId) {
                    seen = true;
                    kept = modetest::sameTimings(crtc->mode, drm);
                    cameUpOn = modetest::describe(fromDrm(crtc->mode));
                }
                drmModeFreeCrtc(crtc);
            }
        }
        usleep(200000);
    }
    // The screen back: the command's hold on the device goes as it ends.
    for (int tries = 0; tries < 20 && !drmIsMaster(screen->fd) && drmSetMaster(screen->fd) != 0; tries++)
        usleep(100000);
    Picture black;
    putBack(screen, &black);
    dropPicture(screen->fd, &frame);
    dropPicture(screen->fd, &black);
    drmDropMaster(screen->fd);

    std::printf(" 1  %s  set, and the command run on it\n", modetest::describe(mode).c_str());
    if (library.empty())
        std::printf("    osdos_keepmode.so isn't beside osdos-modetest: the command ran without it\n");
    if (child < 0)
        std::printf("    the command could not be started: %s\n", std::strerror(errno));
    else if (!seen)
        std::printf("    it showed no picture of its own\n");
    else if (kept)
        std::printf("    its picture came up on this mode: it kept the mode it found\n");
    else
        std::printf("    its picture came up on %s, a mode it set itself: it did not keep this one\n", cameUpOn.c_str());
    if (child > 0 && WIFEXITED(status))
        std::printf("    the command ended with status %d\n", WEXITSTATUS(status));
    else if (child > 0 && WIFSIGNALED(status))
        std::printf("    the command was ended by signal %d\n", WTERMSIG(status));
    std::printf("\nOn its mode, a game's picture has each of its lines on one of the screen's. Did it hold still,\n"
                "fill the screen from edge to edge, and move without a tremor every second or so?\n");
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    bool list = false;
    bool modelines = false;
    int seconds = 6;
    std::string wanted;
    std::string command;
    std::vector<ModeLine> modes;
    modetest::MonitorRange monitor;
    {
        std::string why;
        if (!modetest::parseMonitorRange(modetest::kArcade15, &monitor, &why))
            return 1;
    }
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--seconds" && more) {
            seconds = std::atoi(argv[++i]);
            if (seconds < 1 || seconds > 60) {
                std::fprintf(stderr, "osdos-modetest: --seconds takes 1 to 60\n");
                return 1;
            }
        } else if (arg == "--connector" && more) {
            wanted = argv[++i];
        } else if (arg == "--mode" && more) {
            ModeLine mode;
            std::string why;
            if (!modetest::parseModeLine(argv[++i], &mode, &why)) {
                std::fprintf(stderr, "osdos-modetest: not a modeline (%s): %s\n", why.c_str(), argv[i]);
                return 1;
            }
            mode.what = "given on the command line";
            modes.push_back(mode);
        } else if (arg == "--monitor" && more) {
            std::string why;
            if (!modetest::parseMonitorRange(argv[++i], &monitor, &why)) {
                std::fprintf(stderr, "osdos-modetest: not a monitor's range (%s): %s\n", why.c_str(), argv[i]);
                return 1;
            }
        } else if (arg == "--game" && more) {
            ModeLine mode;
            std::string why;
            if (!gameMode(argv[++i], monitor, &mode, &why)) {
                std::fprintf(stderr, "osdos-modetest: no mode for the game %s: %s\n", argv[i], why.c_str());
                return 1;
            }
            mode.what = std::string("a game's own mode, ") + argv[i];
            modes.push_back(mode);
        } else if (arg == "--modelines") {
            modelines = true;
        } else if (arg == "--run" && more) {
            command = argv[++i];
        } else {
            std::fprintf(stderr, "osdos-modetest: not understood: %s\n\n%s", arg.c_str(), kUsage);
            return 1;
        }
    }
    if (!command.empty() && modes.size() != 1) {
        std::fprintf(stderr, "osdos-modetest: --run takes one mode, a --game or a --mode: %zu given\n", modes.size());
        return 1;
    }
    if (modes.empty())
        modes = modetest::builtInModes();
    if (modelines) {
        for (const ModeLine &mode : modes)
            std::printf("%s    # %s\n", modeLineOf(mode).c_str(), modetest::describe(mode).c_str());
        return 0;
    }

    Screen screen;
    const bool found = findScreen(wanted, list, &screen);
    if (list)
        return 0;
    if (!found) {
        std::fprintf(stderr,
                     "osdos-modetest: no screen to test on: %s.\n"
                     "osdos-modetest --list shows the outputs there are; --connector <name> takes one of them.\n",
                     wanted.empty() ? "no DPI output is showing a picture" : (wanted + " isn't showing a picture").c_str());
        return 2;
    }

    const ModeLine now = fromDrm(screen.before->mode);
    std::printf("osdos-modetest: %s on %s (%s), now on %s\n", screen.connector.c_str(), screen.device.c_str(),
                screen.driver.c_str(), modetest::describe(now).c_str());

    // Setting a mode takes the device's mastership: the first to open a
    // device with no master has it, and a device another program draws on
    // (OSD/OS itself) gives it to no one else.
    if (!drmIsMaster(screen.fd) && drmSetMaster(screen.fd) != 0) {
        std::fprintf(stderr,
                     "osdos-modetest: another program has the screen (%s).\n"
                     "On the OSD/OS image: Settings -> Quit -> Exit to Terminal, and run it there.\n",
                     std::strerror(errno));
        return 3;
    }

    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);

    if (!command.empty()) {
        const int result = runOn(&screen, modes.front(), now, command);
        drmModeFreeCrtc(screen.before);
        close(screen.fd);
        return result;
    }

    std::vector<std::string> report;
    Picture shown;
    int index = 0;
    int held = 0;
    for (const ModeLine &mode : modes) {
        if (g_stop)
            break;
        index++;
        char line[320];
        std::string why;
        if (!modetest::safeFor(mode, now.lineRateHz(), &why)) {
            std::snprintf(line, sizeof line, "%2d  %-42s left out: %s", index, modetest::describe(mode).c_str(),
                          why.c_str());
            report.push_back(line);
            continue;
        }
        Picture next;
        if (!makePicture(screen.fd, mode.hdisplay, mode.vdisplay, &next)) {
            std::snprintf(line, sizeof line, "%2d  %-42s no frame buffer for it: %s", index,
                          modetest::describe(mode).c_str(), std::strerror(errno));
            report.push_back(line);
            dropPicture(screen.fd, &next);
            continue;
        }
        modetest::drawPattern(next.pixels, int(next.pitch / 4), mode.hdisplay, mode.vdisplay, index, mode);
        drmModeModeInfo drm = toDrm(mode);
        if (drmModeSetCrtc(screen.fd, screen.crtcId, next.fbId, 0, 0, &screen.connectorId, 1, &drm) != 0) {
            std::snprintf(line, sizeof line, "%2d  %-42s refused by the kernel: %s", index,
                          modetest::describe(mode).c_str(), std::strerror(errno));
            report.push_back(line);
            dropPicture(screen.fd, &next);
            continue;
        }
        dropPicture(screen.fd, &shown);
        shown = next;
        held++;
        // What the driver made of it: its clock can be the nearest it has.
        std::string made;
        if (drmModeCrtc *crtc = drmModeGetCrtc(screen.fd, screen.crtcId)) {
            if (crtc->mode_valid && crtc->mode.clock != drm.clock) {
                char clock[48];
                std::snprintf(clock, sizeof clock, ", the driver's clock %.3f MHz", crtc->mode.clock / 1000.0);
                made = clock;
            }
            drmModeFreeCrtc(crtc);
        }
        std::snprintf(line, sizeof line, "%2d  %-42s shown %d s%s  (%s)", index, modetest::describe(mode).c_str(),
                      seconds, made.c_str(), mode.what.c_str());
        report.push_back(line);
        for (int tenth = 0; tenth < seconds * 10 && !g_stop; tenth++)
            usleep(100000);
    }

    Picture black;
    putBack(&screen, &black);
    dropPicture(screen.fd, &shown);
    dropPicture(screen.fd, &black);
    drmDropMaster(screen.fd);
    drmModeFreeCrtc(screen.before);
    close(screen.fd);

    for (const std::string &line : report)
        std::printf("%s\n", line.c_str());
    if (held > 0) {
        std::printf("\nEach mode shown had as many squares at its top left as its number here, and its lines and\n"
                    "refresh rate at its bottom left. For each: did the picture hold still, with all four white\n"
                    "edges in sight? Did it roll or tear? Or did the monitor show nothing?\n");
    }
    return 0;
}
