/*
 * A libretro core that runs, for tests/retroarch_tls_test.sh: a black
 * picture sixty times a second, no sound, no input, and eight bytes of
 * memory for RetroAchievements to read, the first of which turns to 1 after
 * two seconds. Only the part of libretro.h's interface RetroArch asks a core
 * for is declared here, as that header has it (RETRO_API_VERSION 1).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};
struct retro_game_geometry {
    unsigned base_width, base_height, max_width, max_height;
    float aspect_ratio;
};
struct retro_system_timing {
    double fps, sample_rate;
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
typedef bool (*retro_environment_t)(unsigned, void *);
typedef void (*retro_video_refresh_t)(const void *, unsigned, unsigned, size_t);
typedef void (*retro_audio_sample_t)(int16_t, int16_t);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *, size_t);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned, unsigned, unsigned, unsigned);

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll_input;
static uint16_t picture[320 * 240];
static int16_t silence[735 * 2];
static uint8_t memory[8];

void retro_set_environment(retro_environment_t cb) { environment = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_input = cb; }
void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
void retro_init(void) {}
void retro_deinit(void) {}
unsigned retro_api_version(void) { return 1; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "OSD/OS TLS Test Core";
    info->library_version = "1";
    info->valid_extensions = "bin";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = info->geometry.max_width = 320;
    info->geometry.base_height = info->geometry.max_height = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_reset(void) {}

void retro_run(void)
{
    static unsigned frames;
    if (++frames > 120)
        memory[0] = 1;
    /* The test's null video driver waits for nothing. */
    usleep(16000);
    poll_input();
    video(picture, 320, 240, 320 * sizeof(uint16_t));
    audio(silence, 735);
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

bool retro_load_game(const struct retro_game_info *game)
{
    unsigned rgb565 = 2; /* RETRO_PIXEL_FORMAT_RGB565 */
    (void)game;
    environment(10 /* RETRO_ENVIRONMENT_SET_PIXEL_FORMAT */, &rgb565);
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count)
{
    (void)type; (void)info; (void)count;
    return false;
}

void retro_unload_game(void) {}
unsigned retro_get_region(void) { return 0; }
void *retro_get_memory_data(unsigned id) { return id == 2 /* RETRO_MEMORY_SYSTEM_RAM */ ? memory : NULL; }
size_t retro_get_memory_size(unsigned id) { return id == 2 ? sizeof(memory) : 0; }
