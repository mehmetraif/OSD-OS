/*
 * A libretro core that only describes itself, for tests/arcade_test.cpp: what
 * libretro::loadCoreInfo() asks of FinalBurn Neo's, whose version the lobby's
 * rooms are matched against. Only the part of libretro.h's interface it uses
 * is declared here, as that header has it (RETRO_API_VERSION 1).
 */
#include <stdbool.h>

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name = "OSD/OS Test Core";
    info->library_version = "v1.0.0.03 6e7c2a1";
    info->valid_extensions = "zip|7z";
    info->need_fullpath = true;
    info->block_extract = true;
}
