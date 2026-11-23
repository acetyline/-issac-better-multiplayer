#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm_speed.h"

enum {
    MOVE_CALL = 0x385166, LINKED_MOVE_CALL = 0x385fb4, MOVE_NATIVE = 0x381eb0,
    EVALUATE_ITEMS = 0x36b7e0, HAS_TRINKET = 0x3798e0, TRINKET_MULTIPLIER = 0x3d42e0,
    TOTAL_CHARGE = 0x399d60, MAX_CHARGE = 0x399f90, SEND_INPUT_CHECK = 0x510b5d,
    NET_STATE = 0x89a094, NET_FRAME = 0x4b6b4, NET_ACTIVE = 0x4b6d0, NET_SUSPENDED = 0x4b400,
    ENTITY_SEED = 0x3ec, MOVE_SPEED = 0x1568, CACHE_FLAGS = 0x1574, REGULAR_CHARGE = 0x1588,
    CACHE_SPEED = 0x10, TRINKET_GOAT_HOOF = 37, TRINKET_VIBRANT_BULB = 100, TRINKET_DIM_BULB = 101
};

typedef struct {
    int hoof, vibrant, dim;
} TrinketSpeed;

typedef struct {
    const uint8_t *player;
    uint32_t seed, frame;
    TrinketSpeed speed;
} PlayerSpeed;

static const uint8_t *const *net_state;
static unsigned (__thiscall *move_native)(void *);
static void (__thiscall *evaluate_items)(void *);
static bool (__thiscall *has_trinket)(void *, int, bool);
static int (__thiscall *trinket_multiplier)(void *, int);
static int (__thiscall *total_charge)(void *, int);
static int (__thiscall *max_charge)(void *, int);
static void (*trace)(const char *);
static PlayerSpeed players[32];
static uint32_t last_frame;
static bool online;
static const uint32_t cache_twin_sync = UINT32_C(0x80000000);

static PlayerSpeed *player_speed(const uint8_t *player)
{
    PlayerSpeed *oldest = &players[0];
    for (size_t i = 0; i < sizeof(players) / sizeof(players[0]); ++i) {
        PlayerSpeed *entry = &players[i];
        if (entry->player == player || !entry->player)
            return entry;
        if (entry->frame < oldest->frame)
            oldest = entry;
    }
    return oldest;
}

static int effective_trinket(void *player, int id)
{
    return has_trinket(player, id, false) ? trinket_multiplier(player, id) : 0;
}

static void refresh_speed(uint8_t *player, uint32_t frame)
{
    const uint32_t seed = *(const uint32_t *)(player + ENTITY_SEED);
    PlayerSpeed *entry = player_speed(player);
    const bool known = entry->player == player && entry->seed == seed;
    if (known && entry->frame == frame)
        return;

    const int hoof = effective_trinket(player, TRINKET_GOAT_HOOF);
    const int vibrant = effective_trinket(player, TRINKET_VIBRANT_BULB);
    const int dim = effective_trinket(player, TRINKET_DIM_BULB);
    TrinketSpeed speed = { .hoof = hoof };
    if (vibrant || dim) {
        const int capacity = max_charge(player, 0);
        if (capacity > 0) {
            if (vibrant && total_charge(player, 0) >= capacity)
                speed.vibrant = vibrant;
            if (dim && *(const int *)(player + REGULAR_CHARGE) == 0)
                speed.dim = dim;
        }
    }

    const bool changed = known && (entry->speed.hoof != speed.hoof ||
                                   entry->speed.vibrant != speed.vibrant || entry->speed.dim != speed.dim);
    *entry = (PlayerSpeed){ .player = player, .seed = seed, .frame = frame, .speed = speed };
    uint32_t *flags = (uint32_t *)(player + CACHE_FLAGS);
    if (!changed && (!(hoof || vibrant || dim) || !(*flags & CACHE_SPEED)))
        return;

    // Recalculate speed before movement; keep the other cache flags for their native callers.
    const uint32_t pending = *flags & ~((uint32_t)CACHE_SPEED);
    *flags = CACHE_SPEED | (pending & cache_twin_sync);
    evaluate_items(player);
    *flags |= pending;
    if (trace) {
        char line[160];
        snprintf(line, sizeof(line), "Trinket speed frame=%u entity=%u hoof=%d vibrant=%d dim=%d speed=%.9g\r\n",
                 frame, seed, speed.hoof, speed.vibrant, speed.dim, (double)*(const float *)(player + MOVE_SPEED));
        trace(line);
    }
}

static unsigned __thiscall move_with_synced_speed(uint8_t *player)
{
    const uint8_t *net = *net_state;
    if (net && net[NET_ACTIVE] && !net[NET_SUSPENDED]) {
        const uint32_t frame = *(const uint32_t *)(net + NET_FRAME);
        if (!online || frame < last_frame)
            memset(players, 0, sizeof(players));
        online = true;
        last_frame = frame;
        refresh_speed(player, frame);
    } else {
        online = false;
    }
    return move_native(player);
}

bool bm_speed_install(void (*report)(const char *))
{
    if (move_native)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < NET_STATE + sizeof(void *) ||
        memcmp(image + MOVE_CALL, "\xe8\x45\xcd\xff\xff", 5) ||
        memcmp(image + LINKED_MOVE_CALL, "\xe8\xf7\xbe\xff\xff", 5) ||
        memcmp(image + SEND_INPUT_CHECK + 6, "\x80\xba\xd0\xb6\x04\x00\x00", 7) ||
        memcmp(image + EVALUATE_ITEMS, "\x55\x8b\xec\x6a\xff\x68", 6))
        return false;

    uint8_t *first = image + MOVE_CALL;
    const size_t length = LINKED_MOVE_CALL - MOVE_CALL + 5;
    DWORD protection, previous;
    if (!VirtualProtect(first, length, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    net_state = (const uint8_t *const *)(image + NET_STATE);
    move_native = (__typeof__(move_native))(image + MOVE_NATIVE);
    evaluate_items = (__typeof__(evaluate_items))(image + EVALUATE_ITEMS);
    has_trinket = (__typeof__(has_trinket))(image + HAS_TRINKET);
    trinket_multiplier = (__typeof__(trinket_multiplier))(image + TRINKET_MULTIPLIER);
    total_charge = (__typeof__(total_charge))(image + TOTAL_CHARGE);
    max_charge = (__typeof__(max_charge))(image + MAX_CHARGE);
    trace = GetEnvironmentVariableW(L"BETTERMULTIPLAYER_SPEED_TRACE", NULL, 0) ? report : NULL;
    const unsigned sites[] = { MOVE_CALL, LINKED_MOVE_CALL };
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        uint8_t *site = image + sites[i];
        const uint32_t offset = (uint32_t)((uintptr_t)move_with_synced_speed - ((uintptr_t)site + 5));
        memcpy(site + 1, &offset, sizeof(offset));
    }
    FlushInstructionCache(GetCurrentProcess(), first, length);
    VirtualProtect(first, length, protection, &previous);
    return true;
}
