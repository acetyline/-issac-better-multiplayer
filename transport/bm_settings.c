#include "bm_settings.h"
#include "bm_game_build.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef int (__thiscall *ReadNumber)(void *, const char *, int);
typedef bool (__thiscall *ReadBool)(void *, const char *, bool);
typedef int (__cdecl *FormatNumber)(char *, size_t, const char *, int);

static ReadNumber read_crossplay_native;
static ReadBool read_mods_native;
static FormatNumber format_native;
static int crossplay_mode, saved_crossplay;
static bool crossplay_loaded;
static int mods_override = -1, current_mods = -1;
static bool mods_loaded, saved_mods;

static int __thiscall read_crossplay(void *section, const char *key, int fallback)
{
    saved_crossplay = read_crossplay_native(section, key, fallback);
    crossplay_loaded = true;
    return crossplay_mode;
}

static bool __thiscall read_mods(void *section, const char *key, bool fallback)
{
    saved_mods = read_mods_native(section, key, fallback);
    mods_loaded = true;
    current_mods = mods_override < 0 ? (int)saved_mods : mods_override;
    return current_mods != 0;
}

static int __cdecl save_crossplay(char *buffer, size_t capacity, const char *format, int value)
{
    return format_native(buffer, capacity, format, crossplay_loaded ? saved_crossplay : value);
}

static int __cdecl save_mods(char *buffer, size_t capacity, const char *format, int value)
{
    current_mods = value != 0;
    return format_native(buffer, capacity, format, mods_override >= 0 && mods_loaded ? saved_mods : value);
}

int bm_settings_mods_enabled(void)
{
    return current_mods;
}

bool bm_settings_install(bool friends_only)
{
    const unsigned read_number = BM_GAME_RVA(0x28b240, 0x28afd0);
    const unsigned read_bool = BM_GAME_RVA(0x28b2e0, 0x28b070);
    const unsigned format = 0x1f910;
    const struct {
        unsigned site, native;
        uintptr_t replacement;
    } patches[] = {
        { BM_GAME_RVA(0x5396a2, 0x539bc2), read_bool, (uintptr_t)read_mods },
        { BM_GAME_RVA(0x539a27, 0x539f47), read_number, (uintptr_t)read_crossplay },
        { BM_GAME_RVA(0x539f7c, 0x53a49c), format, (uintptr_t)save_mods },
        { BM_GAME_RVA(0x53a8bd, 0x53addd), format, (uintptr_t)save_crossplay }
    };
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    const unsigned count = sizeof(patches) / sizeof(patches[0]);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < patches[count - 1].site + 5)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *site = image + patches[i].site;
        uint32_t displacement;
        memcpy(&displacement, site + 1, sizeof(displacement));
        if (site[0] != 0xe8 || displacement != patches[i].native - patches[i].site - 5)
            return false;
    }

    // These calls are in the game's options loader and serializer, before gameplay starts.
    DWORD protections[sizeof(patches) / sizeof(patches[0])], unused;
    unsigned writable = 0;
    while (writable < count && VirtualProtect(image + patches[writable].site, 5,
                                                PAGE_EXECUTE_READWRITE, &protections[writable]))
        ++writable;
    const bool ready = writable == count;
    if (ready) {
        char value[2];
        if (friends_only && GetEnvironmentVariableA("BETTERMULTIPLAYER_MODS_ENABLED", value, sizeof(value)) == 1 &&
            (value[0] == '0' || value[0] == '1'))
            mods_override = value[0] - '0';
        crossplay_mode = friends_only ? 1 : 0;
        read_crossplay_native = (ReadNumber)(image + read_number);
        read_mods_native = (ReadBool)(image + read_bool);
        format_native = (FormatNumber)(image + format);
        for (unsigned i = 0; i < count; ++i) {
            uint8_t *site = image + patches[i].site;
            const uint32_t distance = (uint32_t)(patches[i].replacement - ((uintptr_t)site + 5));
            memcpy(site + 1, &distance, sizeof(distance));
            FlushInstructionCache(GetCurrentProcess(), site, 5);
        }
    }
    while (writable) {
        --writable;
        VirtualProtect(image + patches[writable].site, 5, protections[writable], &unused);
    }
    return ready;
}
