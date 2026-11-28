#include <windows.h>
#include "bm_game_build.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm_characters.h"
#include "bm_locate.h"

#define STOCK_CHECK BM_GAME_RVA(0x55f030, 0x55f540)
#define STOCK_TYPE_TO_SLOT BM_GAME_RVA(0x55f0b0, 0x55f5c0)
#define CHECK_ACHIEVEMENT BM_GAME_RVA(0x55c0c0, 0x55c5d0)
#define STOCK_TAINTED BM_GAME_RVA(0x4f7c00, 0x4f8080)
#define LOBBY_CHECK BM_GAME_RVA(0x4f2717, 0x4f2ba7)
#define TOGGLE_CHECK BM_GAME_RVA(0x4f7ac2, 0x4f7f42)
#define CYCLE BM_GAME_RVA(0x4f7b2a, 0x4f7faa)
#define CYCLE_DONE BM_GAME_RVA(0x4f7b7d, 0x4f7ffd)
#define CUSTOM_PAIRS BM_GAME_RVA(0x89dfdc, 0x89f0dc)
#define APPEND_PAIR BM_GAME_RVA(0x5638f0, 0x563e10)
#define GAME BM_GAME_RVA(0x89a094, 0x89b194)

enum {
    STOCK_SLOTS = 18, LAST_SLOT = 254, FIRST_CUSTOM_TYPE = 41,
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
static void *stock_type_to_slot __attribute__((used));
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

static bool __cdecl custom_type_to_slot(int player_type, int *out_slot, uint8_t *out_tainted) __attribute__((used, noinline));
static bool __cdecl custom_type_to_slot(int player_type, int *out_slot, uint8_t *out_tainted)
{
    if (player_type < FIRST_CUSTOM_TYPE || !out_slot || !out_tainted)
        return false;
    bm_characters_prepare();
    unsigned count;
    const CharacterPair *pairs = characters(&count);
    for (unsigned i = 0; i < count; ++i) {
        if (pairs[i].normal == player_type) {
            *out_slot = (int)(STOCK_SLOTS + i);
            *out_tainted = 0;
            return true;
        }
        if (pairs[i].tainted == player_type) {
            *out_slot = (int)(STOCK_SLOTS + i);
            *out_tainted = 1;
            return true;
        }
    }
    return false;
}

__attribute__((naked))
static void type_to_slot_hook(void)
{
    __asm__ volatile(
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl 12(%esp)\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "call _custom_type_to_slot\n\t"
        "addl $12, %esp\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "testb %al, %al\n\t"
        "jnz 1f\n\t"
        "jmp *_stock_type_to_slot\n\t"
        "1: ret"
    );
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
    unsigned tainted_calls[] = { BM_GAME_RVA(0x4f56b0, 0x4f5b30), BM_GAME_RVA(0x4f6ca5, 0x4f7125), BM_GAME_RVA(0x4f6d56, 0x4f71d6), BM_GAME_RVA(0x4f7869, 0x4f7ce9) };
    static const uint8_t toggle[] = { 0x8d, 0x04, 0x52, 0x8d, 0x0c, 0xc5 };
    static const uint8_t cycle[] = { 0x8a, 0x7f, 0x08, 0x33, 0xf6, 0x80, 0x7f, 0x09, 0x00, 0xb9, 0x12, 0, 0, 0 };
    static const uint8_t resume[] = { 0x81, 0x7d, 0xf8, 0xe9, 0x03, 0x00, 0x00 };
    unsigned lobby_check = LOBBY_CHECK, toggle_check = TOGGLE_CHECK,
             cycle_site = CYCLE, cycle_done = CYCLE_DONE,
             stock_check_rva = STOCK_CHECK, check_achievement_rva = CHECK_ACHIEVEMENT,
             stock_tainted_rva = STOCK_TAINTED;
    /* Prefer pattern resolution so locally patched images beyond the two
     * known builds keep working; the per-build offsets remain the fallback.
     * The toggle pattern begins nine bytes before the site start and the
     * tainted pattern six bytes before its E8; targets are derived from
     * the calls themselves. */
    if (bm_locate_all()) {
        const uint32_t located_lobby = bm_locate_site(BM_SITE_CHARACTERS_LOBBY);
        const uint32_t located_toggle = bm_locate_site(BM_SITE_CHARACTERS_TOGGLE);
        const uint32_t located_tainted = bm_locate_site(BM_SITE_CHARACTERS_TAINTED_BASE);
        if (located_lobby && located_toggle && located_tainted) {
            int32_t distance;
            memcpy(&distance, image + located_lobby + 1, sizeof(distance));
            stock_check_rva = (uint32_t)((int64_t)located_lobby + 5 + distance);
            const uint32_t toggle_e8 = located_toggle + 19;
            memcpy(&distance, image + toggle_e8 + 1, sizeof(distance));
            check_achievement_rva = (uint32_t)((int64_t)toggle_e8 + 5 + distance);
            const uint32_t tainted_e8 = located_tainted + 6;
            memcpy(&distance, image + tainted_e8 + 1, sizeof(distance));
            stock_tainted_rva = (uint32_t)((int64_t)tainted_e8 + 5 + distance);
            /* The three remaining calls keep their relative distance
             * from the first on every known build. */
            tainted_calls[0] = tainted_e8;
            tainted_calls[1] = tainted_e8 + 0x15f5;
            tainted_calls[2] = tainted_e8 + 0x16a6;
            tainted_calls[3] = tainted_e8 + 0x21b9;
            lobby_check = located_lobby;
            toggle_check = located_toggle + 9;
            cycle_site = toggle_check + (CYCLE - TOGGLE_CHECK);
            cycle_done = cycle_site + (CYCLE_DONE - CYCLE);
        }
    }
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < CUSTOM_PAIRS + 8 ||
        !matches_call(image, lobby_check, stock_check_rva) ||
        !matches_call(image, toggle_check + 10, check_achievement_rva) ||
        memcmp(image + toggle_check, toggle, sizeof(toggle)) ||
        memcmp(image + cycle_site, cycle, sizeof(cycle)) ||
        memcmp(image + cycle_done, resume, sizeof(resume)))
        return false;
    for (unsigned i = 0; i < sizeof(tainted_calls) / sizeof(tainted_calls[0]); ++i)
        if (!matches_call(image, tainted_calls[i], stock_tainted_rva))
            return false;

    const size_t length = cycle_done - lobby_check;
    DWORD access, discarded;
    if (!VirtualProtect(image + lobby_check, length, PAGE_EXECUTE_READWRITE, &access))
        return false;
    stock_check = (void *)(image + stock_check_rva);
    stock_tainted = (void *)(image + stock_tainted_rva);
    achievement_unlocked = (void *)(image + check_achievement_rva);
    append_pair = (void *)(image + APPEND_PAIR);
    image_base = image;
    write_branch(image + lobby_check, 0xe8, (uintptr_t)lobby_unlocked);
    for (unsigned i = 0; i < sizeof(tainted_calls) / sizeof(tainted_calls[0]); ++i)
        write_branch(image + tainted_calls[i], 0xe8, (uintptr_t)tainted_unlocked);

    const uint8_t set_this[] = { 0x8b, 0xcf };
    uint8_t *site = image + toggle_check;
    memcpy(site, set_this, sizeof(set_this));
    write_branch(site + 2, 0xe8, (uintptr_t)toggle_unlocked);
    memset(site + 7, 0x90, 8);

    /* Preserve the original and selected slots in BH and BL for the lobby response. */
    const uint8_t cycle_args[] = { 0x8a, 0x7f, 0x08, 0x0f, 0xbe, 0xd0, 0x8b, 0xcf };
    site = image + cycle_site;
    memcpy(site, cycle_args, sizeof(cycle_args));
    write_branch(site + 8, 0xe8, (uintptr_t)cycle_character);
    site[13] = 0x8a;
    site[14] = 0xd8;
    write_branch(site + 15, 0xe9, (uintptr_t)(image + cycle_done));
    FlushInstructionCache(GetCurrentProcess(), image + lobby_check, length);
    VirtualProtect(image + lobby_check, length, access, &discarded);

    stock_type_to_slot = (void *)(image + STOCK_TYPE_TO_SLOT);
    const unsigned type_to_slot_calls[] = {
        0x269db,
        BM_GAME_RVA(0x4f25ef, 0x4f2a7f),
        BM_GAME_RVA(0x4f4100, 0x4f4580),
        BM_GAME_RVA(0x521f0a, 0x52241a)
    };
    for (unsigned i = 0; i < sizeof(type_to_slot_calls) / sizeof(type_to_slot_calls[0]); ++i) {
        const unsigned rva = type_to_slot_calls[i];
        if (!matches_call(image, rva, STOCK_TYPE_TO_SLOT))
            continue;
        DWORD old_prot, tmp_prot;
        if (VirtualProtect(image + rva, 5, PAGE_EXECUTE_READWRITE, &old_prot)) {
            write_branch(image + rva, 0xe8, (uintptr_t)type_to_slot_hook);
            FlushInstructionCache(GetCurrentProcess(), image + rva, 5);
            VirtualProtect(image + rva, 5, old_prot, &tmp_prot);
        }
    }
    return true;
}
