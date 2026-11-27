/* A stand-in for libdrm (built as libdrm.so.2 in a folder of its own), for
 * keepmode_test: a device with four connectors, none of them real.
 *
 *   31  a DPI panel listing 720x240 (preferred) and 640x480, its CRTC (33)
 *       on a mode neither of them: a game's, 1920x224
 *   41  a connector whose CRTC (43) shows nothing
 *   51  a connector that drives no CRTC
 *   61  a connector that lists no mode, its CRTC (63) on 320x224
 */
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static drmModeModeInfo mode(const char *name, uint32_t clock, uint16_t width, uint16_t height, uint32_t type) {
    drmModeModeInfo m;
    memset(&m, 0, sizeof m);
    m.clock = clock;
    m.hdisplay = width;
    m.hsync_start = (uint16_t)(width + 16);
    m.hsync_end = (uint16_t)(width + 78);
    m.htotal = (uint16_t)(width + 138);
    m.vdisplay = height;
    m.vsync_start = (uint16_t)(height + 4);
    m.vsync_end = (uint16_t)(height + 7);
    m.vtotal = (uint16_t)(height + 22);
    m.vrefresh = 60;
    m.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
    m.type = type;
    strncpy(m.name, name, sizeof m.name - 1);
    return m;
}

void *drmMalloc(int size) {
    return calloc(1, (size_t)size);
}

void drmFree(void *pointer) {
    free(pointer);
}

static drmModeConnectorPtr connector(int fd, uint32_t id) {
    drmModeConnectorPtr c;
    if (fd != 7 || (id != 31 && id != 41 && id != 51 && id != 61))
        return NULL;
    c = drmMalloc(sizeof *c);
    c->connector_id = id;
    c->connector_type = DRM_MODE_CONNECTOR_DPI;
    c->connector_type_id = 1;
    c->connection = DRM_MODE_CONNECTED;
    c->encoder_id = id == 51 ? 0 : id + 1;
    if (id != 61) {
        c->count_modes = 2;
        c->modes = drmMalloc(2 * (int)sizeof *c->modes);
        c->modes[0] = mode("720x240", 13500, 720, 240, DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER);
        c->modes[1] = mode("640x480", 25175, 640, 480, DRM_MODE_TYPE_DRIVER);
    }
    return c;
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t id) {
    return connector(fd, id);
}

drmModeConnectorPtr drmModeGetConnectorCurrent(int fd, uint32_t id) {
    return connector(fd, id);
}

void drmModeFreeConnector(drmModeConnectorPtr c) {
    if (!c)
        return;
    drmFree(c->modes);
    drmFree(c);
}

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t id) {
    drmModeEncoderPtr e;
    if (fd != 7 || (id != 32 && id != 42 && id != 62))
        return NULL;
    e = drmMalloc(sizeof *e);
    e->encoder_id = id;
    e->crtc_id = id + 1;
    return e;
}

void drmModeFreeEncoder(drmModeEncoderPtr e) {
    drmFree(e);
}

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t id) {
    drmModeCrtcPtr c;
    if (fd != 7 || (id != 33 && id != 43 && id != 63))
        return NULL;
    c = drmMalloc(sizeof *c);
    c->crtc_id = id;
    if (id == 33) {
        c->buffer_id = 90;
        c->mode_valid = 1;
        c->mode = mode("1920x224_59.19", 39132, 1920, 224, DRM_MODE_TYPE_USERDEF);
    } else if (id == 63) {
        c->buffer_id = 91;
        c->mode_valid = 1;
        c->mode = mode("320x224_59.19", 6525, 320, 224, DRM_MODE_TYPE_USERDEF);
    }
    return c;
}

void drmModeFreeCrtc(drmModeCrtcPtr c) {
    drmFree(c);
}
