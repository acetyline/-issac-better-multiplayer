#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm_characters.h"
#include "bm_friends.h"

enum {
    LOBBY_PRIVATE = 0,
    LOBBY_FRIENDS = 1,
    ROSTER_PARTS = 12, ROSTER_PART_SIZE = 2048
};

static const char lobby_key[] = "bettermultiplayer_j511_owner";
static const char member_key[] = "bettermultiplayer_j511_transport";
static const char sync_member[] = "bm_j511_mod_ready";
static const char protocol[] = "friends-v1";
static const char sync_protocol[] = "friends-v6";
static const char roster_prefix[] = "bm_j511_roster_";
static char roster_parts[ROSTER_PARTS][ROSTER_PART_SIZE];
static char ready_value[8];
static unsigned roster_count;

typedef struct { void **methods; } Matchmaking;
static Matchmaking *stock;
static uint64_t pending_create, pending_join, verified_lobby, verified_owner;
static ULONGLONG create_expires;
static uint64_t waiting_join, cancelled_join;
static ULONGLONG join_expires, next_probe;
static void (*report)(const char *);
static void (__cdecl *stock_join_dialog)(const uint32_t *, void *);

#define METHOD(result, index, declaration) ((result (__thiscall *) declaration)stock->methods[index])
#define PASS(result, name, index, declaration, invocation) \
    static result __thiscall name declaration { \
        (void)self; return METHOD(result, index, declaration) invocation; \
    }
#define PASS_VOID(name, index, declaration, invocation) \
    static void __thiscall name declaration { \
        (void)self; METHOD(void, index, declaration) invocation; \
    }

PASS(int, favorite_count, 0, (Matchmaking *self), (stock))
PASS(bool, get_favorite, 1, (Matchmaking *self, int i, uint32_t *app, uint32_t *ip, uint16_t *conn, uint16_t *query, uint32_t *flags, uint32_t *last), (stock, i, app, ip, conn, query, flags, last))
PASS(int, add_favorite, 2, (Matchmaking *self, uint32_t app, uint32_t ip, uint16_t conn, uint16_t query, uint32_t flags, uint32_t last), (stock, app, ip, conn, query, flags, last))
PASS(bool, remove_favorite, 3, (Matchmaking *self, uint32_t app, uint32_t ip, uint16_t conn, uint16_t query, uint32_t flags), (stock, app, ip, conn, query, flags))
PASS_VOID(filter_string, 5, (Matchmaking *self, const char *key, const char *value, int comparison), (stock, key, value, comparison))
PASS_VOID(filter_number, 6, (Matchmaking *self, const char *key, int value, int comparison), (stock, key, value, comparison))
PASS_VOID(filter_near, 7, (Matchmaking *self, const char *key, int value), (stock, key, value))
PASS_VOID(filter_slots, 8, (Matchmaking *self, int value), (stock, value))
PASS_VOID(filter_distance, 9, (Matchmaking *self, int value), (stock, value))
PASS_VOID(filter_results, 10, (Matchmaking *self, int value), (stock, value))
PASS_VOID(filter_compatibility, 11, (Matchmaking *self, uint64_t lobby), (stock, lobby))
PASS(uint64_t *, get_lobby, 12, (Matchmaking *self, uint64_t *out, int index), (stock, out, index))
PASS(bool, invite, 16, (Matchmaking *self, uint64_t lobby, uint64_t user), (stock, lobby, user))
PASS(int, member_count, 17, (Matchmaking *self, uint64_t lobby), (stock, lobby))
PASS(uint64_t *, get_member, 18, (Matchmaking *self, uint64_t *out, uint64_t lobby, int index), (stock, out, lobby, index))
PASS(const char *, get_lobby_data, 19, (Matchmaking *self, uint64_t lobby, const char *key), (stock, lobby, key))
PASS(int, lobby_data_count, 21, (Matchmaking *self, uint64_t lobby), (stock, lobby))
PASS(bool, lobby_data_at, 22, (Matchmaking *self, uint64_t lobby, int index, char *key, int key_cap, char *value, int value_cap), (stock, lobby, index, key, key_cap, value, value_cap))
PASS(const char *, get_member_data, 24, (Matchmaking *self, uint64_t lobby, uint64_t user, const char *key), (stock, lobby, user, key))
PASS(bool, send_chat, 26, (Matchmaking *self, uint64_t lobby, const void *text, int size), (stock, lobby, text, size))
PASS(int, read_chat, 27, (Matchmaking *self, uint64_t lobby, int index, uint64_t *sender, void *data, int cap, int *type), (stock, lobby, index, sender, data, cap, type))
PASS(bool, refresh, 28, (Matchmaking *self, uint64_t lobby), (stock, lobby))
PASS_VOID(set_server, 29, (Matchmaking *self, uint64_t lobby, uint32_t ip, uint16_t port, uint64_t server), (stock, lobby, ip, port, server))
PASS(bool, get_server, 30, (Matchmaking *self, uint64_t lobby, uint32_t *ip, uint16_t *port, uint64_t *server), (stock, lobby, ip, port, server))
PASS(bool, set_limit, 31, (Matchmaking *self, uint64_t lobby, int limit), (stock, lobby, limit))
PASS(int, get_limit, 32, (Matchmaking *self, uint64_t lobby), (stock, lobby))
PASS(bool, set_joinable, 34, (Matchmaking *self, uint64_t lobby, bool joinable), (stock, lobby, joinable))
PASS(uint64_t *, get_owner, 35, (Matchmaking *self, uint64_t *out, uint64_t lobby), (stock, out, lobby))
PASS(bool, set_linked, 37, (Matchmaking *self, uint64_t lobby, uint64_t dependency), (stock, lobby, dependency))

static bool private_type(int type)
{
    return type == LOBBY_PRIVATE || type == LOBBY_FRIENDS;
}

static bool valid_lobby(uint64_t lobby)
{
    return ((lobby >> 52) & 15) == 8 && ((lobby >> 32) & 0x40000) != 0;
}

static bool creating_now(ULONGLONG now)
{
    return pending_create && now < create_expires;
}

bool bm_friends_creating(uint64_t request)
{
    return request && request == pending_create;
}

void bm_friends_create_failed(uint64_t request)
{
    if (bm_friends_creating(request))
        pending_create = 0;
}

static const char *room_protocol(void)
{
    return GetEnvironmentVariableW(L"BETTERMULTIPLAYER_MODSYNC", NULL, 0) > 0 ? sync_protocol : protocol;
}

static uint64_t expected_owner(uint64_t lobby)
{
    const char *value = get_lobby_data(NULL, lobby, lobby_key);
    if (!value || strncmp(value, room_protocol(), 10) != 0 || value[10] != ':')
        return 0;
    char *end;
    uint64_t owner = _strtoui64(value + 11, &end, 10);
    return *end == 0 && ((owner >> 52) & 15) == 1 ? owner : 0;
}

static uint64_t __thiscall request_lobby_list(Matchmaking *self)
{
    (void)self;
    return 0;
}

static void cancel_join(void)
{
    if (pending_join)
        cancelled_join = pending_join;
    if ((waiting_join || pending_join) && report)
        report("Friend lobby join cancelled.\r\n");
    waiting_join = pending_join = 0;
}

static void __cdecl join_dialog(const uint32_t *choice, void *operation)
{
    if (choice && *choice == 0xdeadbeef)
        cancel_join();
    stock_join_dialog(choice, operation);
}

bool bm_friends_install_cancel(void)
{
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    enum { JOIN_DIALOG = 0x5172d0, DIALOG_HANDLER_PUSH = 0x51c006 };
    uint8_t *site = image + DIALOG_HANDLER_PUSH;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < DIALOG_HANDLER_PUSH + 5 || site[0] != 0x68)
        return false;
    uintptr_t original;
    memcpy(&original, site + 1, sizeof(original));
    if (original != (uintptr_t)(image + JOIN_DIALOG))
        return false;
    DWORD access, discarded;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &access))
        return false;
    stock_join_dialog = (void *)original;
    const uintptr_t replacement = (uintptr_t)join_dialog;
    memcpy(site + 1, &replacement, sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    VirtualProtect(site, 5, access, &discarded);
    return true;
}

static uint64_t __thiscall create_lobby(Matchmaking *self, int type, int limit)
{
    (void)self;
    if (!private_type(type) || limit < 2 || limit > 4) {
        if (report)
            report("Friend lobby create requested an unsupported room type or size.\r\n");
        return 0;
    }
    if (creating_now(GetTickCount64())) {
        if (report)
            report("Friend lobby create already pending.\r\n");
        return 0;
    }
    cancel_join();
    pending_create = METHOD(uint64_t, 13, (Matchmaking *, int, int))(stock, type, limit);
    create_expires = GetTickCount64() + 30000;
    if (report)
        report(pending_create ? "Steam friend lobby create requested.\r\n" :
                                "Steam returned an empty friend lobby create handle.\r\n");
    return pending_create;
}

static uint64_t __thiscall join_lobby(Matchmaking *self, uint64_t lobby)
{
    (void)self;
    if (!valid_lobby(lobby))
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (creating_now(now) || !expected_owner(lobby)) {
        if (waiting_join != lobby) {
            waiting_join = lobby;
            next_probe = now + 1000;
            join_expires = now + 35000;
            refresh(NULL, lobby);
            if (report)
                report("Friend lobby join deferred while Steam resolves the room.\r\n");
        }
        return 0;
    }
    if (pending_join && pending_join != lobby)
        cancel_join();
    waiting_join = 0;
    uint64_t call = METHOD(uint64_t, 14, (Matchmaking *, uint64_t))(stock, lobby);
    pending_join = call ? lobby : 0;
    if (pending_join && cancelled_join == lobby)
        cancelled_join = 0;
    if (report)
        report(call ? "Steam friend lobby join requested.\r\n" :
                      "Steam returned an empty friend lobby join handle.\r\n");
    return call;
}

uint64_t bm_friends_retry_join(uint64_t *owner)
{
    if (!waiting_join)
        return 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= join_expires) {
        waiting_join = 0;
        if (report)
            report("Friend lobby join expired while waiting for room details.\r\n");
        return 0;
    }
    if (now < next_probe || creating_now(now))
        return 0;
    next_probe = now + 1000;
    uint64_t host = expected_owner(waiting_join);
    if (!host) {
        const char *marker = get_lobby_data(NULL, waiting_join, lobby_key);
        if (marker && *marker) {
            waiting_join = 0;
            if (report)
                report("Friend lobby join found a different Better Multiplayer version.\r\n");
        } else {
            refresh(NULL, waiting_join);
        }
        return 0;
    }
    const uint64_t target = waiting_join;
    waiting_join = 0;
    *owner = host;
    return target;
}

void bm_friends_join_failed(uint64_t lobby)
{
    if (pending_join == lobby)
        pending_join = 0;
    if (waiting_join == lobby)
        waiting_join = 0;
    if (cancelled_join == lobby)
        cancelled_join = 0;
}

bool bm_friends_discard_cancelled_join(uint64_t lobby)
{
    if (!lobby || cancelled_join != lobby)
        return false;
    cancelled_join = 0;
    METHOD(void, 15, (Matchmaking *, uint64_t))(stock, lobby);
    if (report)
        report("Steam friend lobby join completed after cancellation; left that lobby.\r\n");
    return true;
}

void bm_friends_set_reporter(void (*reporter)(const char *))
{
    report = reporter;
}

static void __thiscall leave_lobby(Matchmaking *self, uint64_t lobby)
{
    (void)self;
    bm_forget_friends_lobby(lobby);
    METHOD(void, 15, (Matchmaking *, uint64_t))(stock, lobby);
}

static bool __thiscall update_type(Matchmaking *self, uint64_t lobby, int type)
{
    (void)self;
    return private_type(type) && METHOD(bool, 33, (Matchmaking *, uint64_t, int))(stock, lobby, type);
}

static bool __thiscall write_lobby_data(Matchmaking *self, uint64_t lobby, const char *key, const char *value)
{
    (void)self;
    return key && strcmp(key, lobby_key) != 0 && METHOD(bool, 20, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, key, value);
}

static bool __thiscall delete_lobby_data(Matchmaking *self, uint64_t lobby, const char *key)
{
    (void)self;
    return key && strcmp(key, lobby_key) != 0 && METHOD(bool, 23, (Matchmaking *, uint64_t, const char *))(stock, lobby, key);
}

static void __thiscall write_member_data(Matchmaking *self, uint64_t lobby, const char *key, const char *value)
{
    (void)self;
    if (key && strcmp(key, member_key) != 0 && strcmp(key, sync_member) != 0 &&
        strncmp(key, roster_prefix, sizeof(roster_prefix) - 1) != 0)
        METHOD(void, 25, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, key, value);
}

static bool __thiscall change_owner(Matchmaking *self, uint64_t lobby, uint64_t owner)
{
    (void)self;
    return lobby != verified_lobby && METHOD(bool, 36, (Matchmaking *, uint64_t, uint64_t))(stock, lobby, owner);
}

static void *methods[] = {
    favorite_count, get_favorite, add_favorite, remove_favorite,
    request_lobby_list, filter_string, filter_number, filter_near, filter_slots, filter_distance, filter_results,
    filter_compatibility, get_lobby, create_lobby, join_lobby, leave_lobby, invite, member_count, get_member,
    get_lobby_data, write_lobby_data, lobby_data_count, lobby_data_at, delete_lobby_data, get_member_data,
    write_member_data, send_chat, read_chat, refresh, set_server, get_server, set_limit, get_limit,
    update_type, set_joinable, get_owner, change_owner, set_linked
};
_Static_assert(sizeof(methods) / sizeof(methods[0]) == 38, "SteamMatchMaking009 layout");
static Matchmaking wrapper = { methods };

bool bm_friends_only(void)
{
    char setting[2];
    return GetEnvironmentVariableA("BETTERMULTIPLAYER_FRIENDS_ONLY", setting, sizeof(setting)) == 1 && setting[0] == '1';
}

void *bm_verify_matchmaking(void *original)
{
    if (!original || !bm_friends_only())
        return original;
    stock = original;
    return &wrapper;
}

bool bm_accept_friends_lobby(uint64_t lobby, uint64_t local)
{
    if (!bm_friends_only())
        return true;
    if (!stock || !valid_lobby(lobby))
        return false;
    uint64_t owner = 0;
    get_owner(NULL, &owner, lobby);
    if (!owner)
        return false;
    if (verified_lobby == lobby && verified_owner == owner && expected_owner(lobby) == owner)
        return true;
    bool hosting = pending_create && GetTickCount64() < create_expires && owner == local;
    bool joining = pending_join == lobby && expected_owner(lobby) == owner && owner != local;
    if (!hosting && !joining)
        return false;
    if (hosting) {
        pending_create = 0;
        char value[50];
        snprintf(value, sizeof(value), "%s:%llu", room_protocol(), (unsigned long long)local);
        if (!METHOD(bool, 20, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, lobby_key, value))
            return false;
    }
    if (joining)
        pending_join = 0;
    verified_lobby = lobby;
    verified_owner = owner;
    METHOD(void, 25, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, member_key, protocol);
    return true;
}

bool bm_friends_room_marked(uint64_t lobby, uint64_t owner)
{
    return bm_friends_lobby_enrolled(lobby, owner) && expected_owner(lobby) == owner;
}

bool bm_friends_member_marked(uint64_t lobby, uint64_t peer)
{
    const char *value = get_member_data(NULL, lobby, peer, member_key);
    return value && strcmp(value, protocol) == 0;
}

bool bm_friends_lobby_enrolled(uint64_t lobby, uint64_t owner)
{
    return lobby && lobby == verified_lobby && owner && owner == verified_owner;
}

void bm_forget_friends_lobby(uint64_t lobby)
{
    if (verified_lobby == lobby)
        verified_lobby = verified_owner = 0;
    if (pending_join == lobby)
        pending_join = 0;
}

uint64_t bm_friends_lobby_owner(uint64_t lobby)
{
    if (!verified_owner || lobby != verified_lobby)
        return 0;
    uint64_t owner = 0;
    get_owner(NULL, &owner, lobby);
    return owner == verified_owner ? owner : 0;
}

bool bm_friends_recovery_room(uint64_t lobby, uint64_t owner)
{
    if (!stock || !valid_lobby(lobby) || creating_now(GetTickCount64()))
        return false;
    if (expected_owner(lobby) == owner)
        return true;
    refresh(NULL, lobby);
    return false;
}

static void roster_key(char *key, size_t capacity, unsigned part)
{
    snprintf(key, capacity, "%s%u", roster_prefix, part);
}

bool bm_friends_mark_synced(uint64_t lobby)
{
    char roster[ROSTER_PARTS * ROSTER_PART_SIZE];
    if (!bm_characters_roster(roster, sizeof(roster))) {
        if (report)
            report("Could not prepare the online character roster.\r\n");
        return false;
    }
    roster_count = 0;
    const char *part = roster;
    size_t left = strlen(roster);
    while (left && roster_count < ROSTER_PARTS) {
        size_t length = left < ROSTER_PART_SIZE ? left : ROSTER_PART_SIZE - 1;
        while (((unsigned char)part[length] & 0xc0) == 0x80)
            --length;
        if (!length)
            return false;
        memcpy(roster_parts[roster_count], part, length);
        roster_parts[roster_count++][length] = 0;
        part += length;
        left -= length;
    }
    if (left) {
        if (report)
            report("Online character roster exceeds the Steam lobby entry capacity.\r\n");
        return false;
    }
    for (unsigned i = 0; i < roster_count; ++i) {
        char key[32];
        roster_key(key, sizeof(key), i);
        METHOD(void, 25, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, key, roster_parts[i]);
    }
    snprintf(ready_value, sizeof(ready_value), "%u", roster_count + 1);
    METHOD(void, 25, (Matchmaking *, uint64_t, const char *, const char *))(stock, lobby, sync_member, ready_value);
    return true;
}

bool bm_friends_synced(uint64_t lobby, uint64_t member)
{
    const char *status = get_member_data(NULL, lobby, member, sync_member);
    if (!ready_value[0] || !status || !*status)
        return false;
    if (strcmp(status, ready_value) != 0) {
        if (report)
            report("Online character roster differs from a room member.\r\n");
        return false;
    }
    for (unsigned i = 0; i < roster_count; ++i) {
        char key[32];
        roster_key(key, sizeof(key), i);
        const char *remote = get_member_data(NULL, lobby, member, key);
        if (!remote || !*remote)
            return false;
        if (strcmp(remote, roster_parts[i]) != 0) {
            if (report)
                report("Online character roster differs from a room member.\r\n");
            return false;
        }
    }
    return true;
}
