/* osdos_keepmode.so: makes a program keep the video mode it finds on the
 * screen.
 *
 * OSD/OS sets a game's own mode (its lines and its refresh rate) before it
 * starts the emulator: a mode made for that game, which the output's
 * connector doesn't list (a Raspberry Pi's DPI panel lists the one mode of
 * its display preset). Left to themselves the emulators then take another:
 *
 *   - SDL2's KMSDRM driver (MAME's screen) looks the CRTC's mode up in the
 *     connector's list and, not finding it, takes the connector's preferred
 *     mode for the display's own and sets that at its first frame
 *     (KMSDRM_AddDisplay() and KMSDRM_GLES_SwapWindow() in SDL 2.32);
 *   - RetroArch's KMS context asks for a mode of the size the CRTC is on,
 *     among the connector's alone, and gives up when there is none
 *     (gfx_ctx_drm_set_video_mode() in RetroArch 1.20).
 *
 * Preloaded into them (LD_PRELOAD), this library shows them each connector
 * as listing one mode: the one its CRTC is on. Whatever they look up or
 * choose is then that mode, and they set it again as it is. Both must reach
 * libdrm through the dynamic linker for it to come between, as Debian's
 * SDL2 does (libSDL2 names libdrm.so.2 among the libraries it needs) and
 * RetroArch does; an SDL2 that opens libdrm itself at run time would pass
 * it by, and take the connector's own mode as before.
 *
 * Nothing here sets a mode or keeps one from being set, and a connector
 * whose CRTC shows nothing is passed on as it is. OSD/OS loads it only for a
 * game whose mode it has set (ArcadeBackend), and osdos-modetest for the
 * command of its --run. OSDOS_KEEPMODE_VERBOSE=1 has it say on stderr, once
 * for each connector, which mode it kept.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#define EXPORT __attribute__((visibility("default")))

/* libdrm's own, found after this library in the order the linker looks
 * things up; NULL where there is no libdrm to find them in. */
static void *next(const char *name) {
    return dlsym(RTLD_NEXT, name);
}

static void said(const drmModeConnector *connector, const drmModeModeInfo *mode) {
    static uint32_t told[16];
    static int count;
    const char *verbose = getenv("OSDOS_KEEPMODE_VERBOSE");
    int i;
    if (!verbose || !*verbose || strcmp(verbose, "0") == 0)
        return;
    for (i = 0; i < count; i++) {
        if (told[i] == connector->connector_id)
            return;
    }
    if (count < 16)
        told[count++] = connector->connector_id;
    fprintf(stderr, "osdos_keepmode: connector %u shown with the one mode it is on, %ux%u \"%s\" (%u kHz clock)\n",
            connector->connector_id, mode->hdisplay, mode->vdisplay, mode->name, mode->clock);
}

/* The connector as libdrm read it, its list of modes made the one mode its
 * CRTC is on. As it was where it drives no CRTC, or one showing nothing. */
static drmModeConnectorPtr keep(int fd, drmModeConnectorPtr connector) {
    drmModeEncoderPtr (*getEncoder)(int, uint32_t) = (drmModeEncoderPtr (*)(int, uint32_t))next("drmModeGetEncoder");
    void (*freeEncoder)(drmModeEncoderPtr) = (void (*)(drmModeEncoderPtr))next("drmModeFreeEncoder");
    drmModeCrtcPtr (*getCrtc)(int, uint32_t) = (drmModeCrtcPtr (*)(int, uint32_t))next("drmModeGetCrtc");
    void (*freeCrtc)(drmModeCrtcPtr) = (void (*)(drmModeCrtcPtr))next("drmModeFreeCrtc");
    drmModeEncoderPtr encoder;
    drmModeCrtcPtr crtc;
    uint32_t crtcId;

    if (!connector || !connector->encoder_id || !getEncoder || !freeEncoder || !getCrtc || !freeCrtc)
        return connector;
    encoder = getEncoder(fd, connector->encoder_id);
    if (!encoder)
        return connector;
    crtcId = encoder->crtc_id;
    freeEncoder(encoder);
    crtc = crtcId ? getCrtc(fd, crtcId) : NULL;
    if (!crtc)
        return connector;
    if (crtc->mode_valid && crtc->mode.hdisplay && crtc->mode.vdisplay) {
        if (connector->count_modes < 1 || !connector->modes) {
            /* A list of one, as libdrm makes its lists (drmModeFreeConnector()
             * gives it back with drmFree()). */
            void *(*allocate)(int) = (void *(*)(int))next("drmMalloc");
            drmModeModeInfoPtr one = allocate ? allocate(sizeof *one) : NULL;
            if (one) {
                connector->modes = one;
                connector->count_modes = 1;
            }
        }
        if (connector->count_modes >= 1 && connector->modes) {
            connector->modes[0] = crtc->mode;
            connector->count_modes = 1;
            said(connector, &crtc->mode);
        }
    }
    freeCrtc(crtc);
    return connector;
}

EXPORT drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id) {
    drmModeConnectorPtr (*real)(int, uint32_t) = (drmModeConnectorPtr (*)(int, uint32_t))next("drmModeGetConnector");
    return real ? keep(fd, real(fd, connector_id)) : NULL;
}

EXPORT drmModeConnectorPtr drmModeGetConnectorCurrent(int fd, uint32_t connector_id) {
    drmModeConnectorPtr (*real)(int, uint32_t) =
        (drmModeConnectorPtr (*)(int, uint32_t))next("drmModeGetConnectorCurrent");
    return real ? keep(fd, real(fd, connector_id)) : NULL;
}
