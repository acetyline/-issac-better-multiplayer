#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#include "bm_characters.h"
#include "bm_friends.h"
#include "bm_game_build.h"
#include "bm_goodtrip.h"
#include "bm_modsync.h"
#include "bm_recovery.h"
#include "bm_room_recovery.h"
#include "bm_settings.h"
#include "bm_speed.h"

#if !defined(__i386__) && !defined(_M_IX86)
#error This library implements the x86 Steamworks ABI.
#endif

bool bm_game_j514;
static bool settings_ready;

enum {
    BM_DATA_CHANNEL = 54808,
    BM_OPEN_CHANNEL = 54809,
    BM_MODSYNC_CHANNEL = 54810,
    BM_MAX_PEERS = 3,
    BM_MAX_UNRELIABLE = 1200,
    BM_MAX_MESSAGE = 512 * 1024,
    BM_PEEK_TTL_MS = 2000,
    BM_RECEIVE_BATCH = 16,
    BM_ROOM_CACHE_MS = 1000,
    BM_SESSION_CACHE_MS = 1000,
    BM_OPEN_RETRY_MS = 1000,
    BM_OPEN_POLL_MS = 16,
    STEAM_FRIEND = 3,
    STEAM_OK = 1,
    STEAM_NO_CONNECTION = 3,
    STEAM_STATE_CONNECTED = 3,
    STEAM_SEND_NO_NAGLE = 1,
    STEAM_SEND_NO_DELAY = 4,
    STEAM_SEND_RELIABLE = 8,
    STEAM_REOPEN = 32,
    STEAM_GAME_LOBBY_JOIN_REQUESTED = 333,
    STEAM_PERSONA_STATE_CHANGE = 304,
    STEAM_LOBBY_ENTER = 504,
    STEAM_LOBBY_DATA_UPDATE = 505,
    STEAM_LOBBY_CHAT_UPDATE = 506,
    STEAM_LOBBY_CREATED = 513,
    STEAM_MEMBER_DEPARTED = 2 | 4 | 8 | 16,
    STEAM_RELATIONSHIP_CHANGED = 0x0200
};

typedef struct { unsigned char bytes[136]; } SteamIdentity;

typedef struct {
    void *data;
    int size;
    uint32_t connection;
    SteamIdentity peer;
} SteamMessage;

typedef struct {
    uint8_t active, connecting, error, relay;
    int queued_bytes, queued_packets;
    uint32_t remote_ip;
    uint16_t remote_port;
} LegacySessionState;

typedef struct { unsigned char bytes[20]; } LegacyAddress;

typedef struct {
    uint64_t id;
    SteamIdentity identity;
    ULONGLONG next_open, session_expires;
    int session;
} Peer;

typedef struct {
    uint64_t owner, members[BM_MAX_PEERS + 1];
    ULONGLONG expires;
    unsigned count, allowed;
    bool has_local, owner_allowed, public_room;
} Room;

typedef struct {
    SteamMessage *message;
    uint64_t peer;
} Incoming;

typedef struct {
    uint64_t lobby;
    uint32_t permissions;
    bool locked;
    uint32_t result;
} LobbyEnter;

typedef struct {
    uint64_t lobby, member, actor;
    uint32_t change;
} LobbyChatUpdate;

typedef struct {
    uint64_t lobby, member;
    uint8_t success;
} LobbyDataUpdate;

typedef struct {
    uint64_t user;
    uint32_t changes;
} PersonaStateChange;

typedef struct {
    uint32_t result;
    uint64_t lobby;
} LobbyCreated;

typedef struct {
    uint64_t lobby, friend_id;
} GameLobbyJoinRequested;

typedef struct Callback Callback;
typedef struct {
    void (__thiscall *run_result)(Callback *, void *, bool, uint64_t);
    void (__thiscall *run)(Callback *, void *);
    int (__thiscall *size)(Callback *);
} CallbackVTable;

struct Callback {
    const CallbackVTable *methods;
    uint8_t flags;
    int callback_id;
};

_Static_assert(offsetof(SteamMessage, peer) == 12, "Steam message peer layout");
_Static_assert(sizeof(SteamIdentity) == 136, "Steam identity layout");
_Static_assert(sizeof(LobbyEnter) == 24 && sizeof(LobbyChatUpdate) == 32, "Steam lobby callback layout");
_Static_assert(sizeof(LobbyDataUpdate) == 24 && sizeof(PersonaStateChange) == 16, "Steam room cache callback layout");
_Static_assert(sizeof(LobbyCreated) == 16, "Steam lobby creation callback layout");
_Static_assert(sizeof(Callback) == 12, "Steam CCallbackBase layout");
_Static_assert(sizeof(GameLobbyJoinRequested) == 16, "Steam game lobby join callback layout");

typedef struct LegacyInterface LegacyInterface;
typedef struct LegacyVTable {
    bool (__thiscall *send)(LegacyInterface *, uint64_t, const void *, uint32_t, int, int);
    bool (__thiscall *available)(LegacyInterface *, uint32_t *, int);
    bool (__thiscall *read)(LegacyInterface *, void *, uint32_t, uint32_t *, uint64_t *, int);
    bool (__thiscall *accept)(LegacyInterface *, uint64_t);
    bool (__thiscall *close_peer)(LegacyInterface *, uint64_t);
    bool (__thiscall *close_channel)(LegacyInterface *, uint64_t, int);
    bool (__thiscall *state)(LegacyInterface *, uint64_t, LegacySessionState *);
    bool (__thiscall *relay)(LegacyInterface *, bool);
    uint32_t (__thiscall *listen_socket)(LegacyInterface *, int, LegacyAddress, uint16_t, bool);
    uint32_t (__thiscall *peer_socket)(LegacyInterface *, uint64_t, int, int, bool);
    uint32_t (__thiscall *connect_socket)(LegacyInterface *, LegacyAddress, uint16_t, int);
    bool (__thiscall *destroy_socket)(LegacyInterface *, uint32_t, bool);
    bool (__thiscall *destroy_listener)(LegacyInterface *, uint32_t, bool);
    bool (__thiscall *send_socket)(LegacyInterface *, uint32_t, void *, uint32_t, bool);
    bool (__thiscall *available_socket)(LegacyInterface *, uint32_t, uint32_t *);
    bool (__thiscall *read_socket)(LegacyInterface *, uint32_t, void *, uint32_t, uint32_t *);
    bool (__thiscall *available_listener)(LegacyInterface *, uint32_t, uint32_t *, uint32_t *);
    bool (__thiscall *read_listener)(LegacyInterface *, uint32_t, void *, uint32_t, uint32_t *, uint32_t *);
    bool (__thiscall *socket_info)(LegacyInterface *, uint32_t, uint64_t *, int *, LegacyAddress *, uint16_t *);
    bool (__thiscall *listener_info)(LegacyInterface *, uint32_t, LegacyAddress *, uint16_t *);
    int (__thiscall *connection_type)(LegacyInterface *, uint32_t);
    int (__thiscall *maximum_size)(LegacyInterface *, uint32_t);
} LegacyVTable;

struct LegacyInterface { const LegacyVTable *methods; };

static void *network, *utils, *friends, *matchmaking;
static void (__cdecl *set_identity)(SteamIdentity *, uint64_t);
static uint64_t (__cdecl *get_identity)(const SteamIdentity *);
static int (__cdecl *get_friend_relationship)(void *, uint64_t);
static int (__cdecl *get_member_count)(void *, uint64_t);
static uint64_t (__cdecl *get_member)(void *, uint64_t, int);
static int (__cdecl *send_message)(void *, const SteamIdentity *, const void *, uint32_t, int, int);
static int (__cdecl *receive_messages)(void *, int, SteamMessage **, int);
static bool (__cdecl *accept_session)(void *, const SteamIdentity *);
static bool (__cdecl *close_message_channel)(void *, const SteamIdentity *, int);
static int (__cdecl *get_session_info)(void *, const SteamIdentity *, void *, void *);
static void (__cdecl *release_message)(SteamMessage *);
static bool (__cdecl *on_request)(void *, void (__cdecl *)(const SteamIdentity *));
static void (__cdecl *init_relay)(void *);

static Peer peers[BM_MAX_PEERS];
static Room room;
static bool friends_room_only;
static uint64_t local_user, active_lobby;
static Incoming incoming[BM_RECEIVE_BATCH];
static unsigned incoming_head, incoming_count;
static ULONGLONG incoming_at, next_open_poll;
static INIT_ONCE adapter_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION queue_lock;
static volatile LONG adapter_ready, startup_reported;
static bool reported_send, reported_receive;
static LegacyInterface *legacy_stock;
static Callback *game_lobby_join;
static bool recovery_enabled;

static HMODULE valve_module(void)
{
    HMODULE loaded = GetModuleHandleW(L"steam_api.dll");
    return loaded ? loaded : LoadLibraryExW(L"steam_api.dll", NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

static FARPROC valve_proc(const char *symbol)
{
    HMODULE steam = valve_module();
    return steam ? GetProcAddress(steam, symbol) : NULL;
}

static void report(const char *line)
{
    WCHAR filename[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"BETTERMULTIPLAYER_TRANSPORT_LOG", filename, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    HANDLE file = CreateFileW(filename, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char timestamp[48];
    int prefix = snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u [transport] ",
                          (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
                          (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond, (unsigned)now.wMilliseconds);
    DWORD written;
    char entry[768];
    if (prefix > 0 && (size_t)prefix < sizeof(timestamp)) {
        size_t line_length = strlen(line);
        const size_t limit = sizeof(entry) - (size_t)prefix - 2;
        if (line_length > limit)
            line_length = limit;
        memcpy(entry, timestamp, (size_t)prefix);
        memcpy(entry + prefix, line, line_length);
        size_t total = (size_t)prefix + line_length;
        if (total && entry[total - 1] != '\n') {
            entry[total++] = '\r';
            entry[total++] = '\n';
        }
        WriteFile(file, entry, (DWORD)total, &written, NULL);
    }
    CloseHandle(file);
}

static bool is_ready(void)
{
    return InterlockedCompareExchange(&adapter_ready, 0, 0) != 0;
}

static void refresh_room(ULONGLONG now)
{
    if (!active_lobby || now < room.expires)
        return;

    room = (Room){ .expires = now + BM_ROOM_CACHE_MS };
    if (friends_room_only) {
        room.owner = bm_friends_lobby_owner(active_lobby);
        room.public_room = bm_friends_public_lobby(active_lobby, room.owner);
        room.owner_allowed = room.owner && room.owner != local_user &&
                             (room.public_room || get_friend_relationship(friends, room.owner) == STEAM_FRIEND);
    }

    const int count = get_member_count(matchmaking, active_lobby);
    if (count < 1 || count > BM_MAX_PEERS + 1)
        return;
    room.count = (unsigned)count;
    const bool marked = !friends_room_only || bm_friends_room_marked(active_lobby, room.owner);
    for (unsigned i = 0; i < room.count; ++i) {
        const uint64_t member = get_member(matchmaking, active_lobby, i);
        room.members[i] = member;
        if (member == local_user) {
            room.has_local = true;
        } else if (member && marked &&
                   (room.public_room || (member == room.owner ? room.owner_allowed :
                                         get_friend_relationship(friends, member) == STEAM_FRIEND)) &&
                   (!friends_room_only || bm_friends_member_marked(active_lobby, member))) {
            room.allowed |= 1u << i;
        }
    }
}

static uint64_t goodtrip_room_owner(void)
{
    if (!is_ready())
        return 0;
    EnterCriticalSection(&queue_lock);
    refresh_room(GetTickCount64());
    const uint64_t owner = bm_friends_lobby_enrolled(active_lobby, room.owner) ? room.owner : 0;
    LeaveCriticalSection(&queue_lock);
    return owner;
}

static int goodtrip_room_role(void)
{
    const uint64_t owner = goodtrip_room_owner();
    return owner ? (owner == local_user ? 1 : 2) : 0;
}

static bool modsync_enabled(void)
{
    return bm_friends_only() && GetEnvironmentVariableW(L"BETTERMULTIPLAYER_MODSYNC", NULL, 0) > 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reserved;
    // Install before the game loads its base Lua scripts, even with an empty mod list.
    if (reason == DLL_PROCESS_ATTACH) {
        const uint8_t *image = (const uint8_t *)GetModuleHandleW(NULL);
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
        const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
        bm_game_j514 = pe->FileHeader.TimeDateStamp == 0x6b0775bf;
        settings_ready = bm_settings_install(bm_friends_only());
        report(settings_ready ? "Session options hooks ready.\r\n" : "Session options hooks unavailable.\r\n");
        if (modsync_enabled())
            bm_goodtrip_install(goodtrip_room_role, goodtrip_room_owner, report);
    }
    return TRUE;
}

static bool permitted_peer(uint64_t id);
static void change_lobby(uint64_t lobby);
static void fill_identity(SteamIdentity *identity, uint64_t peer);

static void send_modsync(uint64_t peer, const void *data, uint32_t size)
{
    if (permitted_peer(peer)) {
        SteamIdentity identity;
        fill_identity(&identity, peer);
        send_message(network, &identity, data, size, STEAM_SEND_RELIABLE | STEAM_REOPEN, BM_MODSYNC_CHANNEL);
    }
}

static void receive_modsync(void)
{
    SteamMessage *messages[BM_RECEIVE_BATCH];
    const int count = receive_messages(network, BM_MODSYNC_CHANNEL, messages, BM_RECEIVE_BATCH);
    for (int i = 0; i < count; ++i) {
        SteamMessage *message = messages[i];
        uint64_t peer = get_identity(&message->peer);
        if (message->size > 0 && permitted_peer(peer))
            bm_modsync_receive(peer, message->data, (uint32_t)message->size);
        release_message(message);
    }
}

static void capture_recovery(bool rebuilding)
{
    if (is_ready()) {
        EnterCriticalSection(&queue_lock);
        const ULONGLONG now = GetTickCount64();
        refresh_room(now);
        if (room.owner)
            bm_recovery_begin(active_lobby, room.owner, room.count, now, rebuilding, friends);
        LeaveCriticalSection(&queue_lock);
    }
}

static bool room_recovery_host(void)
{
    EnterCriticalSection(&queue_lock);
    const ULONGLONG now = GetTickCount64();
    refresh_room(now);
    const bool allowed = room.has_local && bm_recovery_room_host(active_lobby, room.owner, local_user, now);
    LeaveCriticalSection(&queue_lock);
    return allowed;
}

static bool lobby_mods_ready(void)
{
    if (!is_ready() || !bm_modsync_local_ready())
        return false;
    EnterCriticalSection(&queue_lock);
    refresh_room(GetTickCount64());
    bool ready = room.has_local && bm_friends_lobby_enrolled(active_lobby, room.owner);
    for (unsigned i = 0; ready && i < room.count; ++i) {
        const uint64_t member = room.members[i];
        if (member != local_user)
            ready = (room.allowed & (1u << i)) && bm_friends_synced(active_lobby, member);
    }
    LeaveCriticalSection(&queue_lock);
    return ready;
}

static void recover_lobby(void)
{
    Callback *callback = game_lobby_join;
    if (!is_ready() || !callback)
        return;

    EnterCriticalSection(&queue_lock);
    GameLobbyJoinRequested request = { 0 };
    const uint64_t now = GetTickCount64();
    if (callback->methods->size(callback) == sizeof(GameLobbyJoinRequested)) {
        refresh_room(now);
        const uint64_t owner = bm_friends_lobby_enrolled(active_lobby, room.owner) ? room.owner : 0;
        if (active_lobby && friends_room_only && !owner)
            change_lobby(0);
        if (owner && (owner == local_user || room.owner_allowed)) {
            bm_modsync_tick(active_lobby, local_user, owner);
            if (bm_modsync_wants_messages())
                receive_modsync();
        } else {
            bm_modsync_tick(0, local_user, 0);
        }
        request.lobby = bm_friends_retry_join(&request.friend_id);
        if (!request.lobby)
            request.lobby = bm_modsync_rejoin(active_lobby, &request.friend_id);
        if (!request.lobby && recovery_enabled && bm_recovery_due(now)) {
            int members = owner == local_user ? (int)room.count : 0;
            request.lobby = bm_recovery_poll(local_user, active_lobby, owner, members, now, friends, &request.friend_id);
        }
    }
    LeaveCriticalSection(&queue_lock);

    if (request.lobby) {
        if (get_friend_relationship(friends, request.friend_id) == STEAM_FRIEND ||
            bm_friends_public_lobby(request.lobby, request.friend_id)) {
            report("Dispatching the verified lobby join.\r\n");
            callback->methods->run(callback, &request);
        }
    }
}

static bool __thiscall adapter_close(LegacyInterface *, uint64_t);

static bool permitted_peer(uint64_t id)
{
    if (!active_lobby || !id || id == local_user)
        return false;
    refresh_room(GetTickCount64());
    if (friends_room_only && !bm_friends_lobby_enrolled(active_lobby, room.owner))
        return false;
    for (unsigned i = 0; room.has_local && i < room.count; ++i)
        if (room.members[i] == id)
            return (room.allowed & (1u << i)) != 0;
    return false;
}

static Peer *remember_peer(uint64_t id)
{
    for (unsigned i = 0; i < BM_MAX_PEERS; ++i)
        if (peers[i].id == id)
            return &peers[i];
    for (unsigned i = 0; i < BM_MAX_PEERS; ++i) {
        if (peers[i].id == 0 || !permitted_peer(peers[i].id)) {
            peers[i] = (Peer){ .id = id };
            fill_identity(&peers[i].identity, id);
            return &peers[i];
        }
    }
    return NULL;
}

static void __cdecl session_request(const SteamIdentity *who)
{
    if (!is_ready() || !who)
        return;
    EnterCriticalSection(&queue_lock);
    const uint64_t id = get_identity(who);
    Peer *peer = permitted_peer(id) ? remember_peer(id) : NULL;
    if (peer && accept_session(network, who))
        peer->session_expires = 0;
    LeaveCriticalSection(&queue_lock);
}

static void __thiscall lobby_enter(Callback *self, void *parameter)
{
    (void)self;
    const LobbyEnter *record = parameter;
    if (record && record->result == STEAM_OK) {
        EnterCriticalSection(&queue_lock);
        if (!bm_friends_discard_cancelled_join(record->lobby)) {
            if (bm_accept_friends_lobby(record->lobby, local_user)) {
                change_lobby(record->lobby);
                room.expires = 0;
            } else {
                report("Steam lobby Better Multiplayer enrollment rejected.\r\n");
                if (active_lobby == record->lobby)
                    change_lobby(0);
            }
        }
        LeaveCriticalSection(&queue_lock);
    } else if (record && bm_friends_only()) {
        EnterCriticalSection(&queue_lock);
        bm_friends_join_failed(record->lobby);
        char detail[64];
        snprintf(detail, sizeof(detail), "Steam lobby enter response: %u.\r\n", record->result);
        report(detail);
        LeaveCriticalSection(&queue_lock);
    }
}

static uint64_t observed_create;

static void __thiscall lobby_created(Callback *self, void *parameter, bool failed, uint64_t request)
{
    (void)self;
    if (request != observed_create)
        return;
    observed_create = 0;
    const LobbyCreated *record = parameter;
    if (failed || !record) {
        bm_friends_create_failed(request);
        report("Steam friend lobby create callback reported an I/O failure.\r\n");
        return;
    }
    if (record->result != STEAM_OK)
        bm_friends_create_failed(request);
    char detail[80];
    snprintf(detail, sizeof(detail), "Steam friend lobby create result: %u.\r\n", record->result);
    report(detail);
}

static void __thiscall unused_create_callback(Callback *self, void *parameter)
{
    (void)self;
    (void)parameter;
}

static int __thiscall create_result_size(Callback *self)
{
    (void)self;
    return sizeof(LobbyCreated);
}

static const CallbackVTable create_methods = { lobby_created, unused_create_callback, create_result_size };
static Callback create_callback = { &create_methods, 0, STEAM_LOBBY_CREATED };

static void __thiscall lobby_chat_update(Callback *self, void *parameter)
{
    (void)self;
    const LobbyChatUpdate *record = parameter;
    if (!record)
        return;
    EnterCriticalSection(&queue_lock);
    if (record->lobby == active_lobby) {
        room.expires = 0;
        if (record->change & STEAM_MEMBER_DEPARTED) {
            if (record->member == local_user) {
                bm_forget_friends_lobby(record->lobby);
                change_lobby(0);
            } else {
                adapter_close(NULL, record->member);
            }
        }
    }
    LeaveCriticalSection(&queue_lock);
}

static void __thiscall lobby_data_update(Callback *self, void *parameter)
{
    (void)self;
    const LobbyDataUpdate *record = parameter;
    if (!record || !record->success)
        return;
    EnterCriticalSection(&queue_lock);
    if (record->lobby == active_lobby)
        room.expires = 0;
    LeaveCriticalSection(&queue_lock);
}

static void __thiscall persona_state_change(Callback *self, void *parameter)
{
    (void)self;
    const PersonaStateChange *record = parameter;
    if (!record || !(record->changes & STEAM_RELATIONSHIP_CHANGED))
        return;
    EnterCriticalSection(&queue_lock);
    for (unsigned i = 0; i < room.count; ++i) {
        if (room.members[i] == record->user) {
            room.expires = 0;
            break;
        }
    }
    LeaveCriticalSection(&queue_lock);
}

static void __thiscall callback_result(Callback *self, void *parameter, bool failed, uint64_t call)
{
    (void)call;
    if (!failed)
        self->methods->run(self, parameter);
}

static int __thiscall enter_size(Callback *callback) { (void)callback; return (int)sizeof(LobbyEnter); }
static int __thiscall chat_size(Callback *callback) { (void)callback; return (int)sizeof(LobbyChatUpdate); }
static int __thiscall data_size(Callback *callback) { (void)callback; return (int)sizeof(LobbyDataUpdate); }
static int __thiscall persona_size(Callback *callback) { (void)callback; return (int)sizeof(PersonaStateChange); }
static const CallbackVTable enter_methods = { callback_result, lobby_enter, enter_size };
static const CallbackVTable chat_methods = { callback_result, lobby_chat_update, chat_size };
static const CallbackVTable data_methods = { callback_result, lobby_data_update, data_size };
static const CallbackVTable persona_methods = { callback_result, persona_state_change, persona_size };
static Callback enter_callback = { &enter_methods, 0, STEAM_LOBBY_ENTER };
static Callback chat_callback = { &chat_methods, 0, STEAM_LOBBY_CHAT_UPDATE };
static Callback data_callback = { &data_methods, 0, STEAM_LOBBY_DATA_UPDATE };
static Callback persona_callback = { &persona_methods, 0, STEAM_PERSONA_STATE_CHANGE };

static BOOL CALLBACK initialize_adapter(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter; (void)context;
    HMODULE steam = valve_module();
    if (!steam)
        return FALSE;
    void *(__cdecl *get_network)(void) = (void *(__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    void *(__cdecl *get_utils)(void) = (void *(__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    void *(__cdecl *get_friends)(void) = (void *(__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_SteamFriends_v017");
    void *(__cdecl *get_matchmaking)(void) = (void *(__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_SteamMatchmaking_v009");
    void *(__cdecl *get_user)(void) = (void *(__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_SteamUser_v023");
    int (__cdecl *get_handle)(void) = (int (__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_GetHSteamUser");
    void *(__cdecl *lookup)(int, const char *) = (void *(__cdecl *)(int, const char *))(void *)GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface");
    uint64_t (__cdecl *get_user_id)(void *) = (uint64_t (__cdecl *)(void *))(void *)GetProcAddress(steam, "SteamAPI_ISteamUser_GetSteamID");
    void (__cdecl *register_callback)(Callback *, int) = (void (__cdecl *)(Callback *, int))(void *)GetProcAddress(steam, "SteamAPI_RegisterCallback");
    if (!get_network || !get_utils || !get_friends || !get_matchmaking || !get_user || !get_handle || !lookup || !get_user_id || !register_callback)
        return FALSE;
    #define BIND(target, symbol) do { \
        target = (__typeof__(target))(void *)GetProcAddress(steam, symbol); \
        if (!target) return FALSE; \
    } while (0)
    BIND(set_identity, "SteamAPI_SteamNetworkingIdentity_SetSteamID64");
    BIND(get_identity, "SteamAPI_SteamNetworkingIdentity_GetSteamID64");
    BIND(get_friend_relationship, "SteamAPI_ISteamFriends_GetFriendRelationship");
    BIND(get_member_count, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    BIND(get_member, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    BIND(send_message, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    BIND(receive_messages, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    BIND(accept_session, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    BIND(close_message_channel, "SteamAPI_ISteamNetworkingMessages_CloseChannelWithUser");
    BIND(get_session_info, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
    BIND(release_message, "SteamAPI_SteamNetworkingMessage_t_Release");
    BIND(on_request, "SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionRequest");
    BIND(init_relay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    #undef BIND
    network = get_network();
    utils = get_utils();
    friends = get_friends();
    matchmaking = get_matchmaking();
    void *user = get_user();
    if (!legacy_stock)
        legacy_stock = lookup(get_handle(), "SteamNetworking006");
    if (!network || !utils || !friends || !matchmaking || !user || !legacy_stock)
        return FALSE;
    local_user = get_user_id(user);
    if (!local_user || !on_request(utils, session_request))
        return FALSE;
    friends_room_only = bm_friends_only();
    InitializeCriticalSection(&queue_lock);
    init_relay(utils);
    InterlockedExchange(&adapter_ready, 1);
    register_callback(&enter_callback, STEAM_LOBBY_ENTER);
    register_callback(&chat_callback, STEAM_LOBBY_CHAT_UPDATE);
    register_callback(&data_callback, STEAM_LOBBY_DATA_UPDATE);
    register_callback(&persona_callback, STEAM_PERSONA_STATE_CHANGE);
    report("Steam P2P adapter ready.\r\n");
    report(bm_room_item_copy_install() ? "Room item buffer guard ready.\r\n" :
                                         "Room item buffer guard setup failed.\r\n");
    report(bm_pickup_anim_install() ? "Modded pickup animation fallback ready.\r\n" :
                                      "Modded pickup animation fallback setup failed.\r\n");
    if (friends_room_only) {
        bm_friends_set_reporter(report);
        if (!bm_friends_install_cancel())
            report("Friend lobby cancel hook unavailable.\r\n");
        const bool sync_enabled = modsync_enabled();
        const bool goodtrip_ready = !sync_enabled || bm_goodtrip_install(goodtrip_room_role, goodtrip_room_owner, report);
        const bool characters_ready = !sync_enabled || bm_characters_install();
        const bool speed_ready = !sync_enabled || bm_speed_install(report);
        bm_recovery_initialize(steam);
        recovery_enabled = bm_recovery_install(capture_recovery);
        const bool room_ready = !sync_enabled || (recovery_enabled && bm_room_recovery_install(room_recovery_host, report));
        if (sync_enabled) {
            report(characters_ready ? "Online character picker ready.\r\n" : "Online character picker setup failed.\r\n");
            report(speed_ready ? "Trinket speed synchronization ready.\r\n" : "Trinket speed synchronization setup failed.\r\n");
            report(room_ready ? "Host room checkpoint recovery ready.\r\n" :
                                "Host room checkpoint recovery setup failed.\r\n");
        }
        bm_modsync_init(steam, lobby_mods_ready, send_modsync,
                        settings_ready && goodtrip_ready && characters_ready && speed_ready && room_ready);
        report(recovery_enabled ? "Host checkpoint recovery ready.\r\n" :
                                 "Host checkpoint recovery setup failed.\r\n");
    }
    return TRUE;
}

static void try_initialize(HMODULE steam)
{
    if (is_ready())
        return;
    int (__cdecl *get_user)(void) = (int (__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_GetHSteamUser");
    if (!get_user || !get_user())
        return;
    if (!InitOnceExecuteOnce(&adapter_once, initialize_adapter, NULL, NULL) &&
        InterlockedExchange(&startup_reported, 1) == 0)
        report("Waiting for Steam Networking Messages and native lobby events.\r\n");
}

static void fill_identity(SteamIdentity *identity, uint64_t peer)
{
    memset(identity, 0, sizeof(*identity));
    set_identity(identity, peer);
}

static int session_state(Peer *peer, ULONGLONG now)
{
    if (now >= peer->session_expires) {
        peer->session = get_session_info(network, &peer->identity, NULL, NULL);
        peer->session_expires = now + BM_SESSION_CACHE_MS;
    }
    return peer->session;
}

static void open_session(Peer *peer, ULONGLONG now)
{
    if (now >= peer->next_open && session_state(peer, now) != STEAM_STATE_CONNECTED) {
        const uint8_t open = 1;
        send_message(network, &peer->identity, &open, sizeof(open),
                     STEAM_SEND_RELIABLE | STEAM_REOPEN, BM_OPEN_CHANNEL);
        peer->next_open = now + BM_OPEN_RETRY_MS;
    }
}

static bool __thiscall adapter_send(LegacyInterface *self, uint64_t remote, const void *data, uint32_t size, int mode, int channel)
{
    (void)self;
    if (!is_ready() || channel != 0 || mode < 0 || mode > 3 || !data || size == 0 ||
        size > (mode <= 1 ? BM_MAX_UNRELIABLE : BM_MAX_MESSAGE))
        return false;
    EnterCriticalSection(&queue_lock);
    Peer *peer = permitted_peer(remote) ? remember_peer(remote) : NULL;
    if (!peer) {
        LeaveCriticalSection(&queue_lock);
        return false;
    }
    static const int flags[] = {
        0, STEAM_SEND_NO_NAGLE | STEAM_SEND_NO_DELAY,
        STEAM_SEND_RELIABLE | STEAM_SEND_NO_NAGLE, STEAM_SEND_RELIABLE
    };
    const ULONGLONG now = GetTickCount64();
    open_session(peer, now);
    const int result = send_message(network, &peer->identity, data, size, flags[mode] | STEAM_REOPEN, BM_DATA_CHANNEL);
    if (result == STEAM_NO_CONNECTION && peer->session == STEAM_STATE_CONNECTED) {
        peer->session = 0;
        peer->session_expires = now + BM_SESSION_CACHE_MS;
        open_session(peer, now);
    }
    if (result == STEAM_OK && !reported_send) {
        report("First native game packet queued through Steam Networking Messages.\r\n");
        reported_send = true;
    }
    LeaveCriticalSection(&queue_lock);
    return result == STEAM_OK;
}

static void forget_incoming(uint64_t peer)
{
    unsigned retained = 0;
    for (unsigned i = incoming_head; i < incoming_count; ++i) {
        if (!peer || incoming[i].peer == peer)
            release_message(incoming[i].message);
        else
            incoming[retained++] = incoming[i];
    }
    incoming_head = 0;
    incoming_count = retained;
}

static void poll_open_channel(ULONGLONG now)
{
    if (now < next_open_poll)
        return;
    next_open_poll = now + BM_OPEN_POLL_MS;
    SteamMessage *opening[BM_RECEIVE_BATCH];
    const int count = receive_messages(network, BM_OPEN_CHANNEL, opening, BM_RECEIVE_BATCH);
    for (int i = 0; i < count; ++i)
        release_message(opening[i]);
}

static const Incoming *peek_packet(void)
{
    const ULONGLONG now = GetTickCount64();
    if (incoming_head < incoming_count && now - incoming_at >= BM_PEEK_TTL_MS)
        forget_incoming(0);
    while (incoming_head < incoming_count) {
        const Incoming *entry = &incoming[incoming_head];
        if (permitted_peer(entry->peer))
            return entry;
        release_message(entry->message);
        ++incoming_head;
    }
    incoming_head = incoming_count = 0;
    poll_open_channel(now);
    for (unsigned batch = 0; batch < 2; ++batch) {
        SteamMessage *messages[BM_RECEIVE_BATCH];
        const int count = receive_messages(network, BM_DATA_CHANNEL, messages, BM_RECEIVE_BATCH);
        for (int i = 0; i < count; ++i) {
            SteamMessage *message = messages[i];
            const uint64_t peer = get_identity(&message->peer);
            if (message->data && message->size > 0 && message->size <= BM_MAX_MESSAGE &&
                permitted_peer(peer) && remember_peer(peer))
                incoming[incoming_count++] = (Incoming){ .message = message, .peer = peer };
            else
                release_message(message);
        }
        if (incoming_count > 0) {
            incoming_at = now;
            return &incoming[0];
        }
        if (count < BM_RECEIVE_BATCH)
            break;
    }
    return NULL;
}

static bool __thiscall adapter_available(LegacyInterface *self, uint32_t *size, int channel)
{
    (void)self;
    if (!is_ready() || channel != 0)
        return false;
    EnterCriticalSection(&queue_lock);
    const Incoming *entry = peek_packet();
    if (size)
        *size = entry ? (uint32_t)entry->message->size : 0;
    LeaveCriticalSection(&queue_lock);
    return entry != NULL;
}

static bool __thiscall adapter_read(LegacyInterface *self, void *data, uint32_t capacity, uint32_t *size, uint64_t *remote, int channel)
{
    (void)self;
    if (!is_ready() || channel != 0 || !data)
        return false;
    EnterCriticalSection(&queue_lock);
    const Incoming *entry = peek_packet();
    bool fits = false;
    if (entry) {
        const SteamMessage *message = entry->message;
        fits = capacity >= (uint32_t)message->size;
        if (fits) {
            memcpy(data, message->data, (size_t)message->size);
            if (size) *size = (uint32_t)message->size;
            if (remote) *remote = entry->peer;
            if (!reported_receive) {
                report("First native game packet delivered from Steam Networking Messages.\r\n");
                reported_receive = true;
            }
        }
        release_message(entry->message);
        ++incoming_head;
    }
    if (!fits && size)
        *size = 0;
    LeaveCriticalSection(&queue_lock);
    return fits;
}

static bool __thiscall adapter_accept(LegacyInterface *self, uint64_t remote)
{
    (void)self;
    if (!is_ready())
        return false;
    EnterCriticalSection(&queue_lock);
    Peer *peer = permitted_peer(remote) ? remember_peer(remote) : NULL;
    const bool accepted = peer && accept_session(network, &peer->identity);
    if (accepted)
        peer->session_expires = 0;
    LeaveCriticalSection(&queue_lock);
    return accepted;
}

static bool __thiscall adapter_close(LegacyInterface *self, uint64_t remote)
{
    (void)self;
    if (!is_ready() || !remote)
        return false;
    SteamIdentity identity;
    fill_identity(&identity, remote);
    EnterCriticalSection(&queue_lock);
    forget_incoming(remote);
    for (unsigned i = 0; i < BM_MAX_PEERS; ++i)
        if (peers[i].id == remote)
            peers[i] = (Peer){ 0 };
    const bool data_closed = close_message_channel(network, &identity, BM_DATA_CHANNEL);
    const bool open_closed = close_message_channel(network, &identity, BM_OPEN_CHANNEL);
    close_message_channel(network, &identity, BM_MODSYNC_CHANNEL);
    LeaveCriticalSection(&queue_lock);
    return data_closed || open_closed;
}

static void change_lobby(uint64_t lobby)
{
    EnterCriticalSection(&queue_lock);
    if (lobby != active_lobby) {
        forget_incoming(0);
        for (unsigned i = 0; i < BM_MAX_PEERS; ++i)
            if (peers[i].id)
                adapter_close(NULL, peers[i].id);
        active_lobby = lobby;
        if (lobby && friends_room_only && modsync_enabled())
            bm_characters_prepare();
        room = (Room){ 0 };
        next_open_poll = 0;
        bm_goodtrip_reset();
        reported_send = false;
        reported_receive = false;
        report(lobby ? "Native Steam lobby entered.\r\n" : "Native Steam lobby left.\r\n");
    }
    LeaveCriticalSection(&queue_lock);
}

static bool __thiscall adapter_close_channel(LegacyInterface *self, uint64_t remote, int channel)
{
    return channel == 0 && adapter_close(self, remote);
}

static bool __thiscall adapter_state(LegacyInterface *self, uint64_t remote, LegacySessionState *state)
{
    (void)self;
    if (!is_ready())
        return false;
    EnterCriticalSection(&queue_lock);
    Peer *peer = state && permitted_peer(remote) ? remember_peer(remote) : NULL;
    if (!peer) {
        LeaveCriticalSection(&queue_lock);
        return false;
    }
    memset(state, 0, sizeof(*state));
    const int status = session_state(peer, GetTickCount64());
    state->connecting = status == 1 || status == 2;
    state->active = status == STEAM_STATE_CONNECTED;
    LeaveCriticalSection(&queue_lock);
    return state->connecting || state->active;
}

static bool __thiscall adapter_relay(LegacyInterface *self, bool enabled)
{
    (void)self;
    return enabled;
}

static uint32_t __thiscall listen_socket(LegacyInterface *self, int port, LegacyAddress ip, uint16_t remote_port, bool relay)
{ (void)self; return legacy_stock ? legacy_stock->methods->listen_socket(legacy_stock, port, ip, remote_port, relay) : 0; }
static uint32_t __thiscall peer_socket(LegacyInterface *self, uint64_t remote, int port, int timeout, bool relay)
{ (void)self; return legacy_stock ? legacy_stock->methods->peer_socket(legacy_stock, remote, port, timeout, relay) : 0; }
static uint32_t __thiscall connect_socket(LegacyInterface *self, LegacyAddress ip, uint16_t port, int timeout)
{ (void)self; return legacy_stock ? legacy_stock->methods->connect_socket(legacy_stock, ip, port, timeout) : 0; }
static bool __thiscall destroy_socket(LegacyInterface *self, uint32_t socket, bool notify)
{ (void)self; return legacy_stock && legacy_stock->methods->destroy_socket(legacy_stock, socket, notify); }
static bool __thiscall destroy_listener(LegacyInterface *self, uint32_t socket, bool notify)
{ (void)self; return legacy_stock && legacy_stock->methods->destroy_listener(legacy_stock, socket, notify); }
static bool __thiscall send_socket(LegacyInterface *self, uint32_t socket, void *data, uint32_t size, bool reliable)
{ (void)self; return legacy_stock && legacy_stock->methods->send_socket(legacy_stock, socket, data, size, reliable); }
static bool __thiscall available_socket(LegacyInterface *self, uint32_t socket, uint32_t *size)
{ (void)self; return legacy_stock && legacy_stock->methods->available_socket(legacy_stock, socket, size); }
static bool __thiscall read_socket(LegacyInterface *self, uint32_t socket, void *data, uint32_t cap, uint32_t *size)
{ (void)self; return legacy_stock && legacy_stock->methods->read_socket(legacy_stock, socket, data, cap, size); }
static bool __thiscall available_listener(LegacyInterface *self, uint32_t socket, uint32_t *size, uint32_t *accepted)
{ (void)self; return legacy_stock && legacy_stock->methods->available_listener(legacy_stock, socket, size, accepted); }
static bool __thiscall read_listener(LegacyInterface *self, uint32_t socket, void *data, uint32_t cap, uint32_t *size, uint32_t *accepted)
{ (void)self; return legacy_stock && legacy_stock->methods->read_listener(legacy_stock, socket, data, cap, size, accepted); }
static bool __thiscall socket_info(LegacyInterface *self, uint32_t socket, uint64_t *remote, int *status, LegacyAddress *ip, uint16_t *port)
{ (void)self; return legacy_stock && legacy_stock->methods->socket_info(legacy_stock, socket, remote, status, ip, port); }
static bool __thiscall listener_info(LegacyInterface *self, uint32_t socket, LegacyAddress *ip, uint16_t *port)
{ (void)self; return legacy_stock && legacy_stock->methods->listener_info(legacy_stock, socket, ip, port); }
static int __thiscall connection_type(LegacyInterface *self, uint32_t socket)
{ (void)self; return legacy_stock ? legacy_stock->methods->connection_type(legacy_stock, socket) : 0; }
static int __thiscall maximum_size(LegacyInterface *self, uint32_t socket)
{ (void)self; return legacy_stock ? legacy_stock->methods->maximum_size(legacy_stock, socket) : 0; }

static const LegacyVTable methods = {
    adapter_send, adapter_available, adapter_read, adapter_accept,
    adapter_close, adapter_close_channel, adapter_state, adapter_relay,
    listen_socket, peer_socket, connect_socket, destroy_socket, destroy_listener, send_socket,
    available_socket, read_socket, available_listener, read_listener, socket_info, listener_info,
    connection_type, maximum_size
};
static LegacyInterface proxy_instance = { &methods };
static HWND game_caption_window;

static BOOL CALLBACK append_game_caption(HWND window, LPARAM parameter)
{
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != (DWORD)parameter || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;

    static const WCHAR suffix[] = L" (better multiplayer)";
    WCHAR title[256];
    const int length = GetWindowTextW(window, title, 256);
    if (length <= 0 || !wcsstr(title, L"Binding of Isaac"))
        return TRUE;
    game_caption_window = window;
    if (!wcsstr(title, suffix) && (size_t)length + sizeof(suffix) / sizeof(*suffix) <= sizeof(title) / sizeof(*title)) {
        memcpy(title + length, suffix, sizeof(suffix));
        SetWindowTextW(window, title);
    }
    return FALSE;
}

void __cdecl SteamAPI_RunCallbacks(void)
{
    HMODULE steam = valve_module();
    if (!steam)
        return;
    void (__cdecl *pump)(void) = (void (__cdecl *)(void))(void *)GetProcAddress(steam, "SteamAPI_RunCallbacks");
    try_initialize(steam);
    if (pump)
        pump();
    static ULONGLONG next_caption;
    const ULONGLONG now = GetTickCount64();
    if (friends_room_only && now >= next_caption) {
        next_caption = now + 1000;
        const LPARAM process = (LPARAM)GetCurrentProcessId();
        if (game_caption_window && IsWindow(game_caption_window))
            append_game_caption(game_caption_window, process);
        else
            EnumWindows(append_game_caption, process);
    }
    recover_lobby();
}

void *__cdecl SteamInternal_FindOrCreateUserInterface(int user, const char *version)
{
    HMODULE steam = valve_module();
    if (!steam)
        return NULL;
    void *(__cdecl *lookup)(int, const char *) =
        (void *(__cdecl *)(int, const char *))(void *)GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface");
    if (!lookup)
        return NULL;
    void *stock = lookup(user, version);
    if (version && strcmp(version, "SteamMatchMaking009") == 0)
        return bm_verify_matchmaking(stock);
    if (!version || strcmp(version, "SteamNetworking006") != 0)
        return stock;
    if (stock)
        legacy_stock = stock;
    try_initialize(steam);
    return &proxy_instance;
}

int __cdecl SteamAPI_GetHSteamUser(void)
{
    int (__cdecl *call)(void) = (int (__cdecl *)(void))(void *)valve_proc("SteamAPI_GetHSteamUser");
    return call ? call() : 0;
}

void __cdecl SteamAPI_RegisterCallback(void *callback, int id)
{
    void (__cdecl *call)(void *, int) = (void (__cdecl *)(void *, int))(void *)valve_proc("SteamAPI_RegisterCallback");
    if (call) {
        call(callback, id);
        if (id == STEAM_GAME_LOBBY_JOIN_REQUESTED)
            game_lobby_join = callback;
    }
}

void __cdecl SteamAPI_UnregisterCallback(void *callback)
{
    void (__cdecl *call)(void *) = (void (__cdecl *)(void *))(void *)valve_proc("SteamAPI_UnregisterCallback");
    if (callback == game_lobby_join)
        game_lobby_join = NULL;
    if (call) call(callback);
}

void __cdecl SteamAPI_RegisterCallResult(void *callback, uint64_t api_call)
{
    void (__cdecl *call)(void *, uint64_t) = (void (__cdecl *)(void *, uint64_t))(void *)valve_proc("SteamAPI_RegisterCallResult");
    if (!call)
        return;
    call(callback, api_call);
    if (bm_friends_creating(api_call) && observed_create != api_call) {
        if (observed_create) {
            void (__cdecl *unregister)(void *, uint64_t) =
                (void (__cdecl *)(void *, uint64_t))(void *)valve_proc("SteamAPI_UnregisterCallResult");
            if (unregister)
                unregister(&create_callback, observed_create);
        }
        observed_create = api_call;
        call(&create_callback, api_call);
    }
}

void __cdecl SteamAPI_UnregisterCallResult(void *callback, uint64_t api_call)
{
    void (__cdecl *call)(void *, uint64_t) = (void (__cdecl *)(void *, uint64_t))(void *)valve_proc("SteamAPI_UnregisterCallResult");
    if (call) call(callback, api_call);
}

void __cdecl SteamAPI_Shutdown(void)
{
    void (__cdecl *call)(void) = (void (__cdecl *)(void))(void *)valve_proc("SteamAPI_Shutdown");
    if (is_ready()) {
        if (observed_create) {
            SteamAPI_UnregisterCallResult(&create_callback, observed_create);
            observed_create = 0;
        }
        bm_recovery_reset(friends);
        recovery_enabled = false;
        change_lobby(0);
        on_request(utils, NULL);
        SteamAPI_UnregisterCallback(&enter_callback);
        SteamAPI_UnregisterCallback(&chat_callback);
        SteamAPI_UnregisterCallback(&data_callback);
        SteamAPI_UnregisterCallback(&persona_callback);
        InterlockedExchange(&adapter_ready, 0);
        DeleteCriticalSection(&queue_lock);
        legacy_stock = NULL;
        InitOnceInitialize(&adapter_once);
    }
    if (call) call();
}

void *__cdecl SteamInternal_ContextInit(void *data)
{
    void *(__cdecl *call)(void *) = (void *(__cdecl *)(void *))(void *)valve_proc("SteamInternal_ContextInit");
    return call ? call(data) : NULL;
}

int __cdecl SteamInternal_SteamAPI_Init(const char *versions, char *error)
{
    int (__cdecl *call)(const char *, char *) = (int (__cdecl *)(const char *, char *))(void *)valve_proc("SteamInternal_SteamAPI_Init");
    if (call) return call(versions, error);
    if (error) strcpy(error, "Better Multiplayer: unable to load the installed Steamworks library.");
    return 1;
}
