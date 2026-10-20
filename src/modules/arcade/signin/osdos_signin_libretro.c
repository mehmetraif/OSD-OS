/*
 * osdos_signin_libretro: a libretro core that plays nothing, for RetroArch to
 * sign in to RetroAchievements with (libretro::SignIn). RetroArch signs in
 * only as a core that says it supports achievements starts a file, so this
 * one says so and opens any file: a blank picture, no sound, no memory for
 * achievements to read. RetroAchievements knows no game of the file, so no
 * session of one is started. OSD/OS stops RetroArch as soon as it has said
 * how the sign-in went; the core ends RetroArch itself after a while, should
 * nothing stop it.
 *
 * Only the few parts of libretro.h's interface it uses are declared here, as
 * that header has them (RETRO_API_VERSION 1).
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RETRO_API_VERSION 1
#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000
#define RETRO_ENVIRONMENT_SHUTDOWN 7
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS (42 | RETRO_ENVIRONMENT_EXPERIMENTAL)

enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565 = 2,
    RETRO_PIXEL_FORMAT_UNKNOWN = INT_MAX
};

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_game_info {
    const char *path;
    const void *data;
    size_t size;
    const char *meta;
};

typedef bool (*retro_environment_t)(unsigned cmd, void *data);
typedef void (*retro_video_refresh_t)(const void *data, unsigned width, unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#define EXPORT __attribute__((visibility("default")))

/* The picture: a few black pixels. */
enum { kSide = 16 };
/* How long RetroArch may take to sign in before the core ends it. */
enum { kLimitSeconds = 30 };

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_input_poll_t inputPoll;
static uint32_t frame[kSide * kSide];
static struct timespec started;
static bool ending;

static void supportAchievements(void) {
    bool yes = true;
    if (environment)
        environment(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &yes);
}

EXPORT void retro_set_environment(retro_environment_t callback) {
    environment = callback;
    supportAchievements();
}
EXPORT void retro_set_video_refresh(retro_video_refresh_t callback) { video = callback; }
EXPORT void retro_set_audio_sample(retro_audio_sample_t callback) { (void)callback; }
EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) { (void)callback; }
EXPORT void retro_set_input_poll(retro_input_poll_t callback) { inputPoll = callback; }
EXPORT void retro_set_input_state(retro_input_state_t callback) { (void)callback; }

EXPORT void retro_init(void) {}
EXPORT void retro_deinit(void) {}
EXPORT unsigned retro_api_version(void) { return RETRO_API_VERSION; }

EXPORT void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof *info);
    info->library_name = "OSD/OS Sign-In";
    info->library_version = "1";
    info->valid_extensions = "osdos";
    /* The file isn't read: rcheevos hashes it from its path. */
    info->need_fullpath = true;
}

EXPORT void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof *info);
    info->geometry.base_width = info->geometry.max_width = kSide;
    info->geometry.base_height = info->geometry.max_height = kSide;
    info->geometry.aspect_ratio = 1.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

EXPORT void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
EXPORT void retro_reset(void) {}

EXPORT void retro_run(void) {
    if (inputPoll)
        inputPoll();
    if (video)
        video(frame, kSide, kSide, kSide * sizeof(uint32_t));
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!ending && now.tv_sec - started.tv_sec >= kLimitSeconds) {
        ending = true;
        environment(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
    /* Without a screen to wait for, RetroArch runs frames as fast as it can:
     * a frame's time here instead, so the machine isn't kept busy. */
    usleep(16000);
}

EXPORT size_t retro_serialize_size(void) { return 0; }
EXPORT bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
EXPORT bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
EXPORT void retro_cheat_reset(void) {}
EXPORT void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

EXPORT bool retro_load_game(const struct retro_game_info *game) {
    (void)game;
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
    supportAchievements();
    clock_gettime(CLOCK_MONOTONIC, &started);
    ending = false;
    return true;
}

EXPORT bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count) {
    (void)type; (void)info; (void)count;
    return false;
}
EXPORT void retro_unload_game(void) {}
EXPORT unsigned retro_get_region(void) { return 0; }
EXPORT void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
EXPORT size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
