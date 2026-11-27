/* osdos_keepmode.so (src/modetest/KeepMode.c) against a stand-in for libdrm
 * (keepmode_fake_drm.c): preloaded, each connector lists the one mode its
 * CRTC is on, the very bytes libdrm gives for that CRTC, as SDL2's KMSDRM
 * driver compares them; a connector whose CRTC shows nothing, or that drives
 * none, is as libdrm has it. And without it preloaded ("as-is"), every
 * connector is as libdrm has it: what the test would miss if the library
 * never came between.
 *
 *   keepmode_test kept     with LD_PRELOAD=osdos_keepmode.so
 *   keepmode_test as-is    without
 */
#include <stdio.h>
#include <string.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static int failures;

static void check(int ok, const char *what, uint32_t connector) {
    if (!ok) {
        fprintf(stderr, "FAIL: connector %u: %s\n", connector, what);
        failures++;
    }
}

/* The connector's list is its two modes, as the stand-in has them. */
static void asListed(drmModeConnectorPtr c, uint32_t id) {
    check(c && c->count_modes == 2, "lists its two modes", id);
    if (c && c->count_modes == 2) {
        check(strcmp(c->modes[0].name, "720x240") == 0 && (c->modes[0].type & DRM_MODE_TYPE_PREFERRED),
              "its preferred mode first", id);
        check(strcmp(c->modes[1].name, "640x480") == 0, "its second mode second", id);
    }
}

/* The connector's list is the one mode its CRTC is on. */
static void onlyTheCrtcs(int fd, drmModeConnectorPtr c, uint32_t id, uint32_t crtcId) {
    drmModeCrtcPtr crtc = drmModeGetCrtc(fd, crtcId);
    check(c && c->count_modes == 1 && c->modes, "lists one mode", id);
    if (c && c->count_modes == 1 && c->modes && crtc)
        check(memcmp(&c->modes[0], &crtc->mode, sizeof crtc->mode) == 0, "the mode its CRTC is on, byte for byte", id);
    drmModeFreeCrtc(crtc);
}

int main(int argc, char **argv) {
    const int kept = argc > 1 && strcmp(argv[1], "kept") == 0;
    const int fd = 7;
    int current;

    for (current = 0; current < 2; current++) {
        drmModeConnectorPtr (*get)(int, uint32_t) = current ? drmModeGetConnectorCurrent : drmModeGetConnector;
        drmModeConnectorPtr c;

        /* A panel whose CRTC is on a game's mode. */
        c = get(fd, 31);
        check(c != NULL, "is there", 31);
        if (kept)
            onlyTheCrtcs(fd, c, 31, 33);
        else
            asListed(c, 31);
        check(c && c->encoder_id == 32 && c->connection == DRM_MODE_CONNECTED, "is otherwise as it was", 31);
        drmModeFreeConnector(c);

        /* Asked again, the same: nothing is kept from one call to the next. */
        c = get(fd, 31);
        if (kept)
            onlyTheCrtcs(fd, c, 31, 33);
        else
            asListed(c, 31);
        drmModeFreeConnector(c);

        /* A CRTC showing nothing, and no CRTC at all: as listed, either way. */
        c = get(fd, 41);
        asListed(c, 41);
        drmModeFreeConnector(c);
        c = get(fd, 51);
        asListed(c, 51);
        drmModeFreeConnector(c);

        /* No list of its own, its CRTC on a mode: a list of that one. */
        c = get(fd, 61);
        if (kept)
            onlyTheCrtcs(fd, c, 61, 63);
        else
            check(c && c->count_modes == 0, "lists no mode", 61);
        drmModeFreeConnector(c);

        /* No such connector, no such device: nothing, as from libdrm. */
        check(get(fd, 99) == NULL, "is none", 99);
        check(get(3, 31) == NULL, "is none on another device", 31);
    }

    if (failures)
        return 1;
    printf("%s: every connector as expected\n", kept ? "kept" : "as-is");
    return 0;
}
