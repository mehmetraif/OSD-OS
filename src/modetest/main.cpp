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
#include "ModeLine.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

using modetest::ModeLine;

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
    "\n"
    "Only modes within 5% of the line rate the screen is on are shown. Run it with nothing else on\n"
    "the screen: on the OSD/OS image, Settings -> Quit -> Exit to Terminal.\n";

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

ModeLine fromDrm(const drmModeModeInfo &m) {
    ModeLine out;
    out.name = m.name;
    out.clockMHz = m.clock / 1000.0;
    out.hdisplay = m.hdisplay; out.hsyncStart = m.hsync_start; out.hsyncEnd = m.hsync_end; out.htotal = m.htotal;
    out.vdisplay = m.vdisplay; out.vsyncStart = m.vsync_start; out.vsyncEnd = m.vsync_end; out.vtotal = m.vtotal;
    out.hsyncPositive = m.flags & DRM_MODE_FLAG_PHSYNC;
    out.vsyncPositive = m.flags & DRM_MODE_FLAG_PVSYNC;
    out.interlace = m.flags & DRM_MODE_FLAG_INTERLACE;
    out.doublescan = m.flags & DRM_MODE_FLAG_DBLSCAN;
    out.csync = m.flags & DRM_MODE_FLAG_CSYNC;
    return out;
}

drmModeModeInfo toDrm(const ModeLine &mode) {
    drmModeModeInfo m = {};
    m.clock = uint32_t(mode.clockMHz * 1000.0 + 0.5);
    m.hdisplay = uint16_t(mode.hdisplay); m.hsync_start = uint16_t(mode.hsyncStart);
    m.hsync_end = uint16_t(mode.hsyncEnd); m.htotal = uint16_t(mode.htotal);
    m.vdisplay = uint16_t(mode.vdisplay); m.vsync_start = uint16_t(mode.vsyncStart);
    m.vsync_end = uint16_t(mode.vsyncEnd); m.vtotal = uint16_t(mode.vtotal);
    m.vrefresh = uint32_t(mode.refreshHz() + 0.5);
    m.flags = (mode.hsyncPositive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC)
            | (mode.vsyncPositive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);
    if (mode.csync)
        m.flags |= DRM_MODE_FLAG_CSYNC;
    m.type = DRM_MODE_TYPE_USERDEF;
    std::snprintf(m.name, sizeof m.name, "%s", mode.name.c_str());
    return m;
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

} // namespace

int main(int argc, char **argv) {
    bool list = false;
    int seconds = 6;
    std::string wanted;
    std::vector<ModeLine> modes;
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
        } else {
            std::fprintf(stderr, "osdos-modetest: not understood: %s\n\n%s", arg.c_str(), kUsage);
            return 1;
        }
    }
    if (modes.empty())
        modes = modetest::builtInModes();

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
