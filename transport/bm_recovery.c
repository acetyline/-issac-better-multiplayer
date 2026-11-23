#include "bm_recovery.h"
#include "bm_friends.h"

#include <stdio.h>
#include <string.h>

enum {
    RECOVERY_WINDOW_MS = 120000,
    ROOM_SETTLE_MS = 1000,
    POLL_INTERVAL_MS = 1000,
    STEAM_FRIEND = 3,
    DESYNC_COMPLETE_CALL = 0x50961f,
    DESYNC_REBUILD_CALL = 0x5098da,
    DESYNC_TIMEOUT_REBUILD_CALL = 0x509c7a,
    NATIVE_SEND_COMPLETE = 0x52a080,
    NATIVE_REBUILD_LOBBY = 0x5165b0
};

static const char presence_key[] = "bettermultiplayer_j511_recovery";
static bool (__cdecl *set_presence)(void *, const char *, const char *);
static const char *(__cdecl *get_presence)(void *, uint64_t, const char *);
static void (__cdecl *request_presence)(void *, uint64_t);
static int (__cdecl *get_relationship)(void *, uint64_t);
static void (__cdecl *send_complete)(void);
static void (__thiscall *rebuild_lobby)(void *);
static void (*begin_recovery)(bool rebuilding);

static struct {
    uint64_t origin, host, started, next_poll, published, replacement, requested;
    bool rebuilding;
} recovery;

static void __cdecl on_desync_complete(void)
{
    begin_recovery(false);
    send_complete();
}

static void __thiscall on_desync_rebuild(void *manager)
{
    begin_recovery(true);
    rebuild_lobby(manager);
}

static bool calls_target(const uint8_t *image, uint32_t site, uint32_t target)
{
    int32_t displacement;
    memcpy(&displacement, image + site + 1, sizeof(displacement));
    return image[site] == 0xe8 && displacement == (int32_t)(target - site - 5);
}

static void redirect_call(uint8_t *site, uintptr_t target)
{
    const uint32_t displacement = (uint32_t)(target - ((uintptr_t)site + 5));
    memcpy(site + 1, &displacement, sizeof(displacement));
}

bool bm_recovery_install(void (*begin)(bool rebuilding))
{
    if (begin_recovery)
        return true;
    if (!set_presence || !get_presence || !request_presence || !get_relationship)
        return false;

    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < NATIVE_SEND_COMPLETE + 5 ||
        !calls_target(image, DESYNC_COMPLETE_CALL, NATIVE_SEND_COMPLETE) ||
        !calls_target(image, DESYNC_REBUILD_CALL, NATIVE_REBUILD_LOBBY) ||
        !calls_target(image, DESYNC_TIMEOUT_REBUILD_CALL, NATIVE_REBUILD_LOBBY))
        return false;

    uint8_t *start = image + DESYNC_COMPLETE_CALL;
    const size_t length = DESYNC_TIMEOUT_REBUILD_CALL + 5 - DESYNC_COMPLETE_CALL;
    DWORD previous;
    if (!VirtualProtect(start, length, PAGE_EXECUTE_READWRITE, &previous))
        return false;

    begin_recovery = begin;
    send_complete = (void (__cdecl *)(void))(image + NATIVE_SEND_COMPLETE);
    rebuild_lobby = (void (__thiscall *)(void *))(image + NATIVE_REBUILD_LOBBY);
    redirect_call(start, (uintptr_t)on_desync_complete);
    redirect_call(image + DESYNC_REBUILD_CALL, (uintptr_t)on_desync_rebuild);
    redirect_call(image + DESYNC_TIMEOUT_REBUILD_CALL, (uintptr_t)on_desync_rebuild);
    FlushInstructionCache(GetCurrentProcess(), start, length);
    DWORD current;
    VirtualProtect(start, length, previous, &current);
    return true;
}

void bm_recovery_initialize(HMODULE steam)
{
    set_presence = (__typeof__(set_presence))(void *)GetProcAddress(steam, "SteamAPI_ISteamFriends_SetRichPresence");
    get_presence = (__typeof__(get_presence))(void *)GetProcAddress(steam, "SteamAPI_ISteamFriends_GetFriendRichPresence");
    request_presence = (__typeof__(request_presence))(void *)GetProcAddress(steam, "SteamAPI_ISteamFriends_RequestFriendRichPresence");
    get_relationship = (__typeof__(get_relationship))(void *)GetProcAddress(steam, "SteamAPI_ISteamFriends_GetFriendRelationship");
}

void bm_recovery_begin(uint64_t origin, uint64_t host, int members, uint64_t now,
                       bool rebuilding, void *friends)
{
    if (!origin || !host)
        return;
    if (recovery.origin &&
        (now - recovery.started >= RECOVERY_WINDOW_MS || (members >= 2 && recovery.origin != origin)))
        bm_recovery_reset(friends);
    if (!recovery.origin) {
        recovery.origin = origin;
        recovery.host = host;
        recovery.started = now;
        recovery.next_poll = now + ROOM_SETTLE_MS;
    }
    if (origin == recovery.origin)
        recovery.rebuilding |= rebuilding;
}

void bm_recovery_reset(void *friends)
{
    if (recovery.published)
        set_presence(friends, presence_key, NULL);
    memset(&recovery, 0, sizeof(recovery));
}

static void publish_room(void *friends, uint64_t lobby)
{
    if (lobby == recovery.published)
        return;

    char marker[34];
    snprintf(marker, sizeof(marker), "%016llx:%016llx",
             (unsigned long long)recovery.origin, (unsigned long long)lobby);
    if (set_presence(friends, presence_key, lobby ? marker : NULL))
        recovery.published = lobby;
}

bool bm_recovery_due(uint64_t now)
{
    return recovery.origin && now >= recovery.next_poll;
}

bool bm_recovery_room_host(uint64_t lobby, uint64_t owner, uint64_t local, uint64_t now)
{
    return lobby && local && owner == local && recovery.host == local &&
           (lobby == recovery.origin || lobby == recovery.replacement) &&
           now - recovery.started < RECOVERY_WINDOW_MS;
}

uint64_t bm_recovery_poll(uint64_t local, uint64_t lobby, uint64_t owner, int members,
                          uint64_t now, void *friends, uint64_t *host)
{
    if (!bm_recovery_due(now))
        return 0;
    if (now - recovery.started >= RECOVERY_WINDOW_MS) {
        bm_recovery_reset(friends);
        return 0;
    }
    recovery.next_poll = now + POLL_INTERVAL_MS;

    if (local == recovery.host) {
        if (recovery.rebuilding && owner == local && lobby != recovery.origin && !recovery.replacement)
            recovery.replacement = lobby;
        bool recovery_room = lobby == recovery.origin || lobby == recovery.replacement;
        publish_room(friends, owner == local && recovery_room ? lobby : 0);
        return 0;
    }
    if (recovery.requested && lobby == recovery.requested && owner == recovery.host) {
        bm_recovery_reset(friends);
        return 0;
    }
    if (recovery.requested || (owner && (!recovery.rebuilding || owner != local || members != 1)) ||
        get_relationship(friends, recovery.host) != STEAM_FRIEND)
        return 0;

    const char *marker = get_presence(friends, recovery.host, presence_key);
    unsigned long long origin, destination;
    char tail;
    if (!marker || strlen(marker) != 33 || marker[16] != ':' ||
        sscanf(marker, "%16llx:%16llx%c", &origin, &destination, &tail) != 2 ||
        origin != recovery.origin) {
        request_presence(friends, recovery.host);
        return 0;
    }
    if (!bm_friends_recovery_room(destination, recovery.host))
        return 0;

    recovery.requested = destination;
    *host = recovery.host;
    return destination;
}
