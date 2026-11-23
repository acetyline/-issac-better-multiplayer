#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm_characters.h"

enum {
    STOCK_SLOTS = 18, LAST_SLOT = 254, FIRST_CUSTOM_TYPE = 41,
    STOCK_CHECK = 0x55f030, CHECK_ACHIEVEMENT = 0x55c0c0, STOCK_TAINTED = 0x4f7c00,
    LOBBY_CHECK = 0x4f2717, TOGGLE_CHECK = 0x4f7ac2, CYCLE = 0x4f7b2a, CYCLE_DONE = 0x4f7b7d,
    CUSTOM_PAIRS = 0x89dfdc, APPEND_PAIR = 0x5638f0, GAME = 0x89a094,
    CONFIG_BEGIN = 0x2a948, CONFIG_END = 0x2a94c, CONFIG_SIZE = 0x148,
    CONFIG_NAME = 4, CONFIG_PORTRAIT = 0x4c, CONFIG_PARENT = 0x11c,
    CONFIG_ALT = 0x124, CONFIG_HIDDEN = 0x128,
    NAME_LENGTH = 16, NAME_CAPACITY = 20
};

typedef struct { int normal, tainted; } CharacterPair;
typedef struct { uint8_t prefix[8], slot, tainted; } LobbySelection;

_Static_assert(offsetof(LobbySelection, slot) == 8 && sizeof(LobbySelection) == 10, "J511 lobby selection layout");

static uint8_t *image_base;
static bool (__fastcall *stock_check)(unsigned, int);
static bool (__cdecl *stock_tainted)(void);
static bool (__thiscall *achievement_unlocked)(const int *, int);
static void (__stdcall *append_pair)(CharacterPair *, const CharacterPair *);

static const CharacterPair *characters(unsigned *count)
{
    const CharacterPair *const *range = (const CharacterPair *const *)(image_base + CUSTOM_PAIRS);
    const uintptr_t first = (uintptr_t)range[0], last = (uintptr_t)range[1];
    const unsigned limit = LAST_SLOT - STOCK_SLOTS + 1;
    *count = 0;
    if (first && last >= first && (last - first) % sizeof(CharacterPair) == 0) {
        size_t available = (last - first) / sizeof(CharacterPair);
        *count = available > limit ? limit : (unsigned)available;
    }
    return range[0];
}

static const uint8_t *player_config(int type)
{
    const uint8_t *game = *(const uint8_t *const *)(image_base + GAME);
    if (!game || type < 0)
        return NULL;
    const uintptr_t first = *(const uintptr_t *)(game + CONFIG_BEGIN);
    const uintptr_t last = *(const uintptr_t *)(game + CONFIG_END);
    return first && last > first && (unsigned)type < (last - first) / CONFIG_SIZE
        ? (const uint8_t *)first + (unsigned)type * CONFIG_SIZE : NULL;
}

bool bm_characters_prepare(void)
{
    if (!image_base)
        return false;
    CharacterPair **range = (CharacterPair **)(image_base + CUSTOM_PAIRS);
    if (range[0] != range[1])
        return true;
    const uint8_t *game = *(const uint8_t *const *)(image_base + GAME);
    if (!game)
        return false;
    const uint8_t *first = *(const uint8_t *const *)(game + CONFIG_BEGIN);
    const uint8_t *last = *(const uint8_t *const *)(game + CONFIG_END);
    if (!first || !last || (uintptr_t)last < (uintptr_t)first)
        return false;
    const size_t count = ((uintptr_t)last - (uintptr_t)first) / CONFIG_SIZE;
    /* Use the character menu's pair order and the existing portrait configuration. */
    for (size_t i = FIRST_CUSTOM_TYPE; i < count; ++i) {
        const uint8_t *entry = first + i * CONFIG_SIZE;
        if (!entry[CONFIG_HIDDEN] && !*(const uint32_t *)(entry + CONFIG_PARENT)) {
            const CharacterPair pair = { *(const int *)entry, *(const int *)(entry + CONFIG_ALT) };
            if (range[1] == range[2])
                append_pair(range[1], &pair);
            else {
                *range[1] = pair;
                ++range[1];
            }
        }
    }
    return true;
}

static bool custom_unlocked(const CharacterPair *pair, unsigned tainted)
{
    const int type = tainted ? pair->tainted : pair->normal;
    const int selection[] = { 0, type };
    return type >= FIRST_CUSTOM_TYPE && (!tainted || pair->tainted != pair->normal) &&
           player_config(type) && achievement_unlocked(selection, 0);
}

static bool __fastcall lobby_unlocked(unsigned tainted, int slot)
{
    tainted &= 1;
    if (slot < 0)
        return false;
    if (slot < STOCK_SLOTS)
        return stock_check(tainted, slot);
    unsigned count;
    const CharacterPair *pairs = characters(&count);
    const unsigned position = (unsigned)slot - STOCK_SLOTS;
    return position < count && custom_unlocked(&pairs[position], tainted);
}

static bool __thiscall toggle_unlocked(const LobbySelection *selection, int ignored)
{
    (void)ignored;
    return lobby_unlocked(selection->tainted, selection->slot);
}

static bool __cdecl tainted_unlocked(void)
{
    if (stock_tainted())
        return true;
    unsigned count;
    const CharacterPair *pairs = characters(&count);
    for (unsigned i = 0; i < count; ++i)
        if (custom_unlocked(&pairs[i], 1))
            return true;
    return false;
}

static unsigned __fastcall cycle_character(LobbySelection *selection, int direction)
{
    unsigned custom_count;
    characters(&custom_count);
    const int total = STOCK_SLOTS + (int)custom_count;
    int slot = selection->slot;
    if (direction) {
        const int step = direction < 0 ? -1 : 1;
        if (slot >= total)
            slot = step < 0 ? 0 : total - 1;
        for (int i = 0; i < total; ++i) {
            slot = (slot + step + total) % total;
            if (lobby_unlocked(selection->tainted, slot)) {
                selection->slot = (uint8_t)slot;
                break;
            }
        }
    }
    return selection->slot;
}

static bool roster_string(char *text, size_t capacity, size_t *used, const uint8_t *value)
{
    const size_t length = value ? *(const uint32_t *)(value + NAME_LENGTH) : 0;
    if (length && *(const uint32_t *)(value + NAME_CAPACITY) >= 16)
        value = *(const uint8_t *const *)value;
    int prefix = snprintf(text + *used, capacity - *used, "%zu:", length);
    if (prefix < 0 || (size_t)prefix >= capacity - *used || length >= capacity - *used - (size_t)prefix)
        return false;
    *used += (size_t)prefix;
    if (length) {
        if (!value || memchr(value, 0, length))
            return false;
        memcpy(text + *used, value, length);
        *used += length;
    }
    return true;
}

bool bm_characters_roster(char *text, size_t capacity)
{
    if (!capacity || !bm_characters_prepare())
        return false;
    unsigned count;
    const CharacterPair *pairs = characters(&count);
    size_t used = 0;
    for (unsigned i = 0; i < count; ++i) {
        const int types[] = { pairs[i].normal, pairs[i].tainted };
        for (unsigned side = 0; side < 2; ++side) {
            const uint8_t *entry = player_config(types[side]);
            int prefix = snprintf(text + used, capacity - used, "%d,", types[side]);
            if (prefix < 0 || (size_t)prefix >= capacity - used)
                return false;
            used += (size_t)prefix;
            if (!roster_string(text, capacity, &used, entry ? entry + CONFIG_NAME : NULL) ||
                !roster_string(text, capacity, &used, entry ? entry + CONFIG_PORTRAIT : NULL))
                return false;
        }
    }
    text[used] = 0;
    return true;
}

static bool matches_call(const uint8_t *image, unsigned site, unsigned target)
{
    int32_t distance;
    memcpy(&distance, image + site + 1, sizeof(distance));
    return image[site] == 0xe8 && distance == (int32_t)(target - site - 5);
}

static void write_branch(uint8_t *site, uint8_t opcode, uintptr_t target)
{
    const uint32_t distance = (uint32_t)(target - ((uintptr_t)site + 5));
    site[0] = opcode;
    memcpy(site + 1, &distance, sizeof(distance));
}

bool bm_characters_install(void)
{
    if (image_base)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    const unsigned tainted_calls[] = { 0x4f56b0, 0x4f6ca5, 0x4f6d56, 0x4f7869 };
    static const uint8_t toggle[] = { 0x8d, 0x04, 0x52, 0x8d, 0x0c, 0xc5 };
    static const uint8_t cycle[] = { 0x8a, 0x7f, 0x08, 0x33, 0xf6, 0x80, 0x7f, 0x09, 0x00, 0xb9, 0x12, 0, 0, 0 };
    static const uint8_t resume[] = { 0x81, 0x7d, 0xf8, 0xe9, 0x03, 0x00, 0x00 };
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < CUSTOM_PAIRS + 8 ||
        !matches_call(image, LOBBY_CHECK, STOCK_CHECK) ||
        !matches_call(image, TOGGLE_CHECK + 10, CHECK_ACHIEVEMENT) ||
        memcmp(image + TOGGLE_CHECK, toggle, sizeof(toggle)) ||
        memcmp(image + CYCLE, cycle, sizeof(cycle)) ||
        memcmp(image + CYCLE_DONE, resume, sizeof(resume)))
        return false;
    for (unsigned i = 0; i < sizeof(tainted_calls) / sizeof(tainted_calls[0]); ++i)
        if (!matches_call(image, tainted_calls[i], STOCK_TAINTED))
            return false;

    const size_t length = CYCLE_DONE - LOBBY_CHECK;
    DWORD access, discarded;
    if (!VirtualProtect(image + LOBBY_CHECK, length, PAGE_EXECUTE_READWRITE, &access))
        return false;
    stock_check = (void *)(image + STOCK_CHECK);
    stock_tainted = (void *)(image + STOCK_TAINTED);
    achievement_unlocked = (void *)(image + CHECK_ACHIEVEMENT);
    append_pair = (void *)(image + APPEND_PAIR);
    image_base = image;
    write_branch(image + LOBBY_CHECK, 0xe8, (uintptr_t)lobby_unlocked);
    for (unsigned i = 0; i < sizeof(tainted_calls) / sizeof(tainted_calls[0]); ++i)
        write_branch(image + tainted_calls[i], 0xe8, (uintptr_t)tainted_unlocked);

    const uint8_t set_this[] = { 0x8b, 0xcf };
    uint8_t *site = image + TOGGLE_CHECK;
    memcpy(site, set_this, sizeof(set_this));
    write_branch(site + 2, 0xe8, (uintptr_t)toggle_unlocked);
    memset(site + 7, 0x90, 8);

    /* Preserve the original and selected slots in BH and BL for the lobby response. */
    const uint8_t cycle_args[] = { 0x8a, 0x7f, 0x08, 0x0f, 0xbe, 0xd0, 0x8b, 0xcf };
    site = image + CYCLE;
    memcpy(site, cycle_args, sizeof(cycle_args));
    write_branch(site + 8, 0xe8, (uintptr_t)cycle_character);
    site[13] = 0x8a;
    site[14] = 0xd8;
    write_branch(site + 15, 0xe9, (uintptr_t)(image + CYCLE_DONE));
    FlushInstructionCache(GetCurrentProcess(), image + LOBBY_CHECK, length);
    VirtualProtect(image + LOBBY_CHECK, length, access, &discarded);
    return true;
}
