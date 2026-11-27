#include <windows.h>
#include "bm_game_build.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bm_room_recovery.h"

#define UPDATE_CALL BM_GAME_RVA(0x56aa51, 0x56af81)
#define RESET_POSITIONS BM_GAME_RVA(0x52ba4a, 0x52bf5a)
#define PROCESS_JOINS BM_GAME_RVA(0x520610, 0x520b10)
#define ROOM_FRAME BM_GAME_RVA(0x41c3e0, 0x41c370)
#define ROOM_ENEMIES BM_GAME_RVA(0x5a3f0, 0x5a380)
#define GAME_OBJECT BM_GAME_RVA(0x8994f8, 0x89a5f8)
#define NET_OBJECT BM_GAME_RVA(0x89a094, 0x89b194)

#define ROOM_ITEM_CLEAR BM_GAME_RVA(0x52e2d4, 0x52e7e4)
#define ROOM_ITEM_COPY BM_GAME_RVA(0x52e300, 0x52e810)
#define NATIVE_COPY BM_GAME_RVA(0x710dff, 0x7114cf)
#define ROOM_ITEM_BUFFER BM_GAME_RVA(0x899520, 0x89a620)
#define PICKUP_ANIM_CALL BM_GAME_RVA(0x3b3c0a, 0x3b3c2b)
#define PLAY_EXTRA_ANIM BM_GAME_RVA(0x3b499f, 0x3b49c0)
#define ANM2_IS_PLAYING 0x00b790

enum {
    ROOM_ITEM_BUFFER_BYTES = 733 * sizeof(uint32_t),
    PLAYER_SPRITE_OFFSET = 0x41c
};

enum {
    UPDATE_GAME = 0x301950,
    GAME_PAUSED = 0x304190, ROOM_CLEAR = 0x27d10,
    GAME_MODE = 8, GAMEPLAY = 2, NET_TRANSITION = 0x4b400,
    NET_FRAME = 0x4b6b4, NET_MANAGER = 0x4b6a8, PENDING_JOINS = 0x1938,
    CURRENT_ROOM = 0x18300, CURRENT_ROOM_INDEX = 0x18304, GAME_FRAME = 0x26508,
    AMBUSH_ACTIVE = 0x265f8, ROOM_DESCRIPTOR = 4, ROOM_VISITS = 0x40,
    ROOM_SCRIPT_TIMER = 0x706c, CLEAR_FRAMES = 2
};

static void (__thiscall *update_native)(void *);
static void (__thiscall *process_joins)(void *, bool);
static bool (__thiscall *game_paused)(void *);
static bool (__thiscall *room_clear)(void *);
static int (__thiscall *room_frame)(void *);
static int (__thiscall *room_enemies)(void *);
static uint8_t *const *game_object, *const *net_object;
static bool (*recovery_host)(void);
static void (*report_checkpoint)(const char *);

static bool room_checkpoint_sending __attribute__((used));
static bool room_checkpoint_encoded __attribute__((used));
static void *(__cdecl *copy_room_items_native)(void *, const void *, size_t);
static void (__thiscall *play_extra_anim_native)(void *, const char *);
static bool (__thiscall *anm2_is_playing_native)(void *, const char *);

static void __thiscall play_pickup_anim_safe(uint8_t *player, const char *anim)
{
    play_extra_anim_native(player, anim);
    if (anim && strcmp(anim, "Pickup_Online") == 0 &&
        !anm2_is_playing_native(player + PLAYER_SPRITE_OFFSET, "Pickup_Online"))
        play_extra_anim_native(player, "Pickup");
}

bool bm_pickup_anim_install(void)
{
    if (play_extra_anim_native)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < PLAY_EXTRA_ANIM + 16)
        return false;

    uint8_t *site = image + PICKUP_ANIM_CALL;
    if (memcmp(site, "\xe8\x90\x0d\x00\x00", 5))
        return false;

    DWORD previous, protection;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous))
        return false;
    play_extra_anim_native = (__typeof__(play_extra_anim_native))(image + PLAY_EXTRA_ANIM);
    anm2_is_playing_native = (__typeof__(anm2_is_playing_native))(image + ANM2_IS_PLAYING);
    const uint32_t offset = (uint32_t)((uintptr_t)play_pickup_anim_safe - ((uintptr_t)site + 5));
    memcpy(site + 1, &offset, sizeof(offset));
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site, 5, previous, &protection);
    return true;
}

static void *__cdecl copy_room_items(void *destination, const void *source, size_t size)
{
    // The following game loop reads 733 counters. Keep extended item pools inside that buffer.
    if (size > ROOM_ITEM_BUFFER_BYTES)
        size = ROOM_ITEM_BUFFER_BYTES;
    return copy_room_items_native(destination, source, size);
}

bool bm_room_item_copy_install(void)
{
    if (copy_room_items_native)
        return true;

    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < ROOM_ITEM_BUFFER + ROOM_ITEM_BUFFER_BYTES)
        return false;

    uint8_t *site = image + ROOM_ITEM_COPY;
    const uint8_t *clear = image + ROOM_ITEM_CLEAR;
    const char *expected_call = bm_game_j514 ? "\xe8\xba\x2c\x1e\x00" : "\xe8\xfa\x2a\x1e\x00";
    uintptr_t destination;
    memcpy(&destination, site - sizeof(destination), sizeof(destination));
    if (memcmp(clear, "\x68\x74\x0b\x00\x00", 5) || site[-5] != 0x68 ||
        destination != (uintptr_t)(image + ROOM_ITEM_BUFFER) ||
        memcmp(site, expected_call, 5))
        return false;

    DWORD previous, protection;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous))
        return false;
    copy_room_items_native = (__typeof__(copy_room_items_native))(image + NATIVE_COPY);
    const uint32_t offset = (uint32_t)((uintptr_t)copy_room_items - ((uintptr_t)site + 5));
    memcpy(site + 1, &offset, sizeof(offset));
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site, 5, previous, &protection);
    return true;
}

static struct {
    const uint8_t *descriptor;
    int index;
    uint32_t first_frame, attempted_frame;
    bool attempted;
} cleared_room;

// The native checkpoint captures the room and the players' current positions.
__attribute__((naked))
static void preserve_checkpoint_positions(void)
{
    __asm__ volatile("jz 1f\n\t"
                     "cmpb $0, _room_checkpoint_sending\n\t"
                     "je 2f\n\t"
                     "movb $1, _room_checkpoint_encoded\n\t"
                     "1: addl $0x82, (%esp)\n\t"
                     "2: ret");
}

static void __thiscall update_with_room_recovery(uint8_t *game)
{
    update_native(game);
    uint8_t *net = *net_object;
    uint8_t *manager = net ? net + NET_MANAGER : NULL;
    if (!net || room_checkpoint_sending || game != *game_object || *(const uint32_t *)(net + GAME_MODE) != GAMEPLAY ||
        net[NET_TRANSITION] || !*(const uint32_t *)(manager + PENDING_JOINS)) {
        cleared_room.descriptor = NULL;
        return;
    }

    uint8_t *room = *(uint8_t *const *)(game + CURRENT_ROOM);
    const int index = *(const int *)(game + CURRENT_ROOM_INDEX);
    const uint8_t *descriptor = room ? *(const uint8_t *const *)(room + ROOM_DESCRIPTOR) : NULL;
    if (!descriptor || index < 0 || game[AMBUSH_ACTIVE] || *(const int *)(room + ROOM_SCRIPT_TIMER) > 0 ||
        !*(const uint32_t *)(descriptor + ROOM_VISITS) ||
        room_frame(room) < CLEAR_FRAMES || !room_clear(room) || room_enemies(room) > 0 || game_paused(game)) {
        cleared_room.descriptor = NULL;
        return;
    }

    const uint32_t frame = *(const uint32_t *)(game + GAME_FRAME);
    if (descriptor != cleared_room.descriptor || index != cleared_room.index || frame < cleared_room.first_frame) {
        cleared_room.descriptor = descriptor;
        cleared_room.index = index;
        cleared_room.first_frame = frame;
        cleared_room.attempted = false;
        return;
    }
    const uint32_t input_frame = *(const uint32_t *)(net + NET_FRAME);
    if (frame - cleared_room.first_frame < CLEAR_FRAMES ||
        (cleared_room.attempted && cleared_room.attempted_frame == frame) ||
        !recovery_host())
        return;

    cleared_room.attempted = true;
    cleared_room.attempted_frame = frame;
    room_checkpoint_encoded = false;
    room_checkpoint_sending = true;
    process_joins(manager, false);
    room_checkpoint_sending = false;
    if (room_checkpoint_encoded) {
        char line[96];
        snprintf(line, sizeof(line), "Host room checkpoint encoded: room=%d frame=%u.\r\n", index, input_frame);
        report_checkpoint(line);
    }
}

bool bm_room_recovery_install(bool (*host_recovering)(void), void (*report)(const char *))
{
    if (update_native)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < NET_OBJECT + sizeof(void *) ||
        memcmp(image + PROCESS_JOINS, "\x55\x8b\xec\x6a\xff\x68", 6) ||
        memcmp(image + ROOM_CLEAR, "\x8b\x41\x04\x8a\x40\x44\x24\x01", 8))
        return false;

    const struct {
        unsigned rva;
        const char *bytes;
        size_t size;
        uintptr_t target;
    } patches[] = {
        { UPDATE_CALL, bm_game_j514 ? "\xe8\xca\x69\xd9\xff" : "\xe8\xfa\x6e\xd9\xff",
          5, (uintptr_t)update_with_room_recovery },
        { RESET_POSITIONS, "\x0f\x84\x81\x00\x00\x00", 6, (uintptr_t)preserve_checkpoint_positions }
    };
    const size_t count = sizeof(patches) / sizeof(patches[0]);
    DWORD previous[sizeof(patches) / sizeof(patches[0])];
    for (size_t i = 0; i < count; ++i) {
        if (memcmp(image + patches[i].rva, patches[i].bytes, patches[i].size))
            return false;
    }
    size_t writable = 0;
    for (; writable < count; ++writable) {
        if (!VirtualProtect(image + patches[writable].rva, patches[writable].size,
                            PAGE_EXECUTE_READWRITE, &previous[writable]))
            break;
    }
    const bool ready = writable == count;
    if (ready) {
        update_native = (__typeof__(update_native))(image + UPDATE_GAME);
        process_joins = (__typeof__(process_joins))(image + PROCESS_JOINS);
        game_paused = (__typeof__(game_paused))(image + GAME_PAUSED);
        room_clear = (__typeof__(room_clear))(image + ROOM_CLEAR);
        room_frame = (__typeof__(room_frame))(image + ROOM_FRAME);
        room_enemies = (__typeof__(room_enemies))(image + ROOM_ENEMIES);
        game_object = (uint8_t *const *)(image + GAME_OBJECT);
        net_object = (uint8_t *const *)(image + NET_OBJECT);
        recovery_host = host_recovering;
        report_checkpoint = report;
        for (size_t i = 0; i < count; ++i) {
            uint8_t *site = image + patches[i].rva;
            const uint32_t offset = (uint32_t)(patches[i].target - ((uintptr_t)site + 5));
            site[0] = 0xe8;
            memcpy(site + 1, &offset, sizeof(offset));
            memset(site + 5, 0x90, patches[i].size - 5);
            FlushInstructionCache(GetCurrentProcess(), site, patches[i].size);
        }
    }
    while (writable) {
        --writable;
        DWORD protection;
        VirtualProtect(image + patches[writable].rva, patches[writable].size, previous[writable], &protection);
    }
    return ready;
}
