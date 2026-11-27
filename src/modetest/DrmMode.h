#pragma once
#include "ModeLine.h"

#include <xf86drmMode.h>

#include <cstdio>

// A ModeLine as libdrm has a mode (drmModeModeInfo) and back, for what sets
// one through KMS: osdos-modetest, and the app's hand-off of the screen
// (util/DisplayHandoff). Linux only, as libdrm is.
namespace modetest {

inline ModeLine fromDrm(const drmModeModeInfo &m) {
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

// The pixel clock in kHz, as the kernel counts it: the nearest to the
// mode's own.
inline drmModeModeInfo toDrm(const ModeLine &mode) {
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

// The sync a mode is sent with: the polarities of its two syncs, and a
// composite one. What a board is wired for, whatever the picture.
inline constexpr uint32_t kSyncFlags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC
                                       | DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_CSYNC | DRM_MODE_FLAG_PCSYNC
                                       | DRM_MODE_FLAG_NCSYNC;

// Whether two modes are one picture on the screen: the same timings and the
// same kind of scan, whatever they are called, and the same clock to within
// a hundredth (a driver may report the nearest it makes).
inline bool sameTimings(const drmModeModeInfo &a, const drmModeModeInfo &b) {
    constexpr uint32_t scan = DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN;
    const uint32_t apart = a.clock > b.clock ? a.clock - b.clock : b.clock - a.clock;
    return apart * 100 <= a.clock && a.hdisplay == b.hdisplay && a.hsync_start == b.hsync_start
           && a.hsync_end == b.hsync_end && a.htotal == b.htotal && a.vdisplay == b.vdisplay
           && a.vsync_start == b.vsync_start && a.vsync_end == b.vsync_end && a.vtotal == b.vtotal
           && (a.flags & scan) == (b.flags & scan);
}

} // namespace modetest
