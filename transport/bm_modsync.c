#include "bm_friends.h"
#include "bm_modsync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    MAX_MANIFEST = BM_MODSYNC_MAX_PACKET - 8, MAX_ITEMS = 256,
    START1 = 0x4f4f97, START2 = 0x4f50b4, START_NATIVE = 0x4f8850,
    SEND_INPUT_CALL = 0x50fd28, SEND_INPUT_NATIVE = 0x510b10,
    SUBSCRIBED = 1, INSTALLED = 4, NEEDS_UPDATE = 8, DOWNLOADING = 16, PENDING = 32,
    SYNC_TIMEOUT = 600000
};

static WCHAR folder[1024];
static void *ugc;
static uint64_t (__cdecl *subscribe_item)(void *, uint64_t);
static uint32_t (__cdecl *item_state)(void *, uint64_t);
static bool (__cdecl *item_path)(void *, uint64_t, uint64_t *, char *, uint32_t, uint32_t *);
static bool (*ready_callback)(void);
static BmModSyncSend send_sync;
static void (__thiscall *native_start)(void *);
static void (__thiscall *native_send_input)(void *, uint32_t);

static uint64_t room_id, room_owner, local_user, next_poll, deadline, rejoin_room, rejoin_host, rejoin_delay;
static bool ready, failed, sent_scan, apply_requested, playing;
static uint64_t requested[MAX_ITEMS];
static size_t request_count;
static char host_data[MAX_MANIFEST + 1], local_data[MAX_MANIFEST + 1], installed_data[MAX_MANIFEST + 1];
static HWND status_window;
static WCHAR old_title[256];

static void status(const WCHAR *title)
{
    if (title && !status_window) {
        HWND candidate = GetActiveWindow();
        if (candidate) {
            GetWindowTextW(candidate, old_title, 256);
            status_window = candidate;
        }
    }
    if (status_window)
        SetWindowTextW(status_window, title ? title : old_title);
    if (!title)
        status_window = NULL;
}

static void __thiscall start_if_synced(void *menu)
{
    if (ready_callback())
        native_start(menu);
    else
        status(L"Better Multiplayer – 等待房间成员同步 Mod");
}

static void __thiscall send_gameplay_input(void *device, uint32_t frame)
{
    playing = true;
    native_send_input(device, frame);
}

static bool call_matches(const uint8_t *image, uint32_t site, uint32_t target)
{
    int32_t distance;
    memcpy(&distance, image + site + 1, sizeof(distance));
    return image[site] == 0xe8 && distance == (int32_t)(target - site - 5);
}

static void redirect_call(uint8_t *site, uintptr_t target)
{
    const uint32_t distance = (uint32_t)(target - ((uintptr_t)site + 5));
    memcpy(site + 1, &distance, sizeof(distance));
}

static bool install_start_gate(void)
{
    if (native_start)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < SEND_INPUT_NATIVE + 5 ||
        !call_matches(image, START1, START_NATIVE) ||
        !call_matches(image, START2, START_NATIVE) ||
        !call_matches(image, SEND_INPUT_CALL, SEND_INPUT_NATIVE))
        return false;
    DWORD start_access, input_access, discarded;
    uint8_t *start = image + START1;
    uint8_t *input = image + SEND_INPUT_CALL;
    const size_t length = START2 - START1 + 5;
    if (!VirtualProtect(start, length, PAGE_EXECUTE_READWRITE, &start_access))
        return false;
    if (!VirtualProtect(input, 5, PAGE_EXECUTE_READWRITE, &input_access)) {
        VirtualProtect(start, length, start_access, &discarded);
        return false;
    }
    native_start = (void (__thiscall *)(void *))(image + START_NATIVE);
    native_send_input = (void (__thiscall *)(void *, uint32_t))(image + SEND_INPUT_NATIVE);
    redirect_call(start, (uintptr_t)start_if_synced);
    redirect_call(image + START2, (uintptr_t)start_if_synced);
    redirect_call(input, (uintptr_t)send_gameplay_input);
    FlushInstructionCache(GetCurrentProcess(), start, length);
    FlushInstructionCache(GetCurrentProcess(), input, 5);
    VirtualProtect(input, 5, input_access, &discarded);
    VirtualProtect(start, length, start_access, &discarded);
    return true;
}

static FILE *sync_file(const WCHAR *name, const WCHAR *mode)
{
    WCHAR path[1080];
    if (swprintf(path, 1080, L"%ls\\%ls", folder, name) < 0)
        return NULL;
    return _wfopen(path, mode);
}

static bool write_file(const WCHAR *name, const char *data)
{
    WCHAR path[1080], temporary[1080];
    if (swprintf(path, 1080, L"%ls\\%ls", folder, name) < 0 ||
        swprintf(temporary, 1080, L"%ls\\update.tmp", folder) < 0)
        return false;
    FILE *file = _wfopen(temporary, L"wb");
    if (!file)
        return false;
    bool written = fputs(data, file) >= 0;
    if (fclose(file) == 0 && written && MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING))
        return true;
    DeleteFileW(temporary);
    return false;
}

static bool read_file(const WCHAR *name, char *data, size_t cap)
{
    FILE *file = sync_file(name, L"rb");
    if (!file)
        return false;
    size_t count = fread(data, 1, cap - 1, file);
    bool complete = fgetc(file) == EOF && !ferror(file);
    fclose(file);
    data[count] = 0;
    return complete;
}

void bm_modsync_init(HMODULE steam, bool (*start_ready)(void), BmModSyncSend send, bool hooks_ready)
{
    DWORD length = GetEnvironmentVariableW(L"BETTERMULTIPLAYER_MODSYNC", folder, 1024);
    if (!length || length >= 1024) {
        folder[0] = 0;
        return;
    }
    ready_callback = start_ready;
    send_sync = send;
    void *(__cdecl *get_ugc)(void) =
        (__typeof__(get_ugc))(void *)GetProcAddress(steam, "SteamAPI_SteamUGC_v020");
    subscribe_item = (__typeof__(subscribe_item))(void *)GetProcAddress(steam, "SteamAPI_ISteamUGC_SubscribeItem");
    item_state = (__typeof__(item_state))(void *)GetProcAddress(steam, "SteamAPI_ISteamUGC_GetItemState");
    item_path = (__typeof__(item_path))(void *)GetProcAddress(steam, "SteamAPI_ISteamUGC_GetItemInstallInfo");
    ugc = get_ugc ? get_ugc() : NULL;
    if (!ugc || !subscribe_item || !item_state || !item_path || !hooks_ready || !install_start_gate())
        write_file(L"error.txt", "setup");
    char reconnect[64] = { 0 };
    GetEnvironmentVariableA("BETTERMULTIPLAYER_MODSYNC_REJOIN", reconnect, sizeof(reconnect));
    unsigned long long lobby, host;
    if (sscanf(reconnect, "%llu %llu", &lobby, &host) == 2) {
        rejoin_room = lobby;
        rejoin_host = host;
        rejoin_delay = GetTickCount64() + 4000;
    }
}

static void mark_ready(uint64_t room)
{
    if (!bm_friends_mark_synced(room)) {
        failed = true;
        status(L"Better Multiplayer – 请检查在线角色列表");
        return;
    }
    ready = true;
    status(NULL);
}

static void publish(uint64_t room)
{
    char id[24], scanned[24];
    snprintf(id, sizeof(id), "%llu", (unsigned long long)room);
    if (!sent_scan)
        sent_scan = write_file(L"host-scan.txt", id);
    if (!read_file(L"host-ready.txt", scanned, sizeof(scanned)) || strcmp(scanned, id) != 0 ||
        !read_file(L"local.txt", local_data, sizeof(local_data)))
        return;

    mark_ready(room);
}

void bm_modsync_receive(uint64_t peer, const void *data, uint32_t size)
{
    uint64_t room;
    if (!bm_modsync_wants_messages() || size < sizeof(room) || size > BM_MODSYNC_MAX_PACKET || !data)
        return;
    memcpy(&room, data, sizeof(room));
    if (room != room_id)
        return;
    const char *message = (const char *)data + sizeof(room);
    const uint32_t length = size - sizeof(room);
    if (local_user == room_owner && ready && length == 0) {
        uint8_t response[BM_MODSYNC_MAX_PACKET];
        size_t manifest_size = strlen(local_data);
        memcpy(response, &room, sizeof(room));
        memcpy(response + sizeof(room), local_data, manifest_size);
        send_sync(peer, response, (uint32_t)(sizeof(room) + manifest_size));
    } else if (local_user != room_owner && peer == room_owner && !host_data[0] &&
               length > 6 && memcmp(message, "BM1\n", 4) == 0 && !memchr(message, 0, length)) {
        memcpy(host_data, message, length);
        host_data[length] = 0;
    }
}

static size_t read_ids(const char *text, uint64_t *ids)
{
    size_t count = 0;
    while (*text && *text != '\n' && count < MAX_ITEMS) {
        if (*text < '1' || *text > '9')
            return MAX_ITEMS + 1;
        char *end;
        uint64_t id = _strtoui64(text, &end, 10);
        if (end - text > 20 || (*end && *end != ',' && *end != '\n'))
            return MAX_ITEMS + 1;
        ids[count++] = id;
        text = *end == ',' ? end + 1 : end;
    }
    return (*text && *text != '\n') ? MAX_ITEMS + 1 : count;
}

static bool includes(const uint64_t *list, size_t count, uint64_t id)
{
    for (size_t i = 0; i < count; ++i)
        if (list[i] == id)
            return true;
    return false;
}

static bool installed_locally(uint64_t id)
{
    char token[24];
    snprintf(token, sizeof(token), "%llu", (unsigned long long)id);
    const size_t size = strlen(token);
    const char *position = installed_data;
    while ((position = strstr(position, token)) != NULL) {
        if ((position == installed_data || position[-1] == ',') && (position[size] == 0 || position[size] == ','))
            return true;
        position += size;
    }
    return false;
}

static void prepare_guest(uint64_t room, uint64_t owner)
{
    if (!host_data[0]) {
        send_sync(owner, &room, sizeof(room));
        return;
    }
    if (read_file(L"local.txt", local_data, sizeof(local_data)) && strcmp(local_data, host_data) == 0) {
        mark_ready(room);
        return;
    }
    uint64_t required[MAX_ITEMS];
    size_t count = read_ids(host_data + 4, required);
    if (!read_file(L"installed.txt", installed_data, sizeof(installed_data)))
        installed_data[0] = 0;
    if (count > MAX_ITEMS) {
        failed = true;
        write_file(L"error.txt", "manifest");
        return;
    }

    bool waiting = false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = required[i];
        if (installed_locally(id))
            continue;
        uint32_t state = item_state(ugc, id);
        if (!(state & SUBSCRIBED) && !includes(requested, request_count, id) &&
            request_count < MAX_ITEMS && subscribe_item(ugc, id))
            requested[request_count++] = id;
        if (!(state & SUBSCRIBED) || !(state & INSTALLED) || (state & (NEEDS_UPDATE | DOWNLOADING | PENDING)))
            waiting = true;
    }
    if (waiting) {
        status(L"Better Multiplayer – Steam 正在准备房主的 Mod");
        return;
    }
    FILE *items = sync_file(L"items.txt", L"wb");
    if (!items)
        return;
    bool complete = true;
    for (size_t i = 0; i < count; ++i) {
        uint64_t length;
        uint32_t stamp;
        char path[4096];
        if (item_path(ugc, required[i], &length, path, sizeof(path), &stamp))
            complete &= fprintf(items, "%llu\t%s\n", (unsigned long long)required[i], path) > 0;
        else if (!installed_locally(required[i]))
            complete = false;
    }
    if (fclose(items) != 0 || !complete || !write_file(L"host.txt", host_data))
        return;
    char route[48];
    snprintf(route, sizeof(route), "%llu %llu", (unsigned long long)room, (unsigned long long)owner);
    status(L"Better Multiplayer – 正在应用房主的 Mod");
    apply_requested = write_file(L"apply.txt", route);
}

void bm_modsync_tick(uint64_t room, uint64_t local, uint64_t owner)
{
    if (!folder[0] || !ugc)
        return;
    const uint64_t now = GetTickCount64();
    if (room_id != room || room_owner != owner) {
        room_id = room;
        room_owner = owner;
        local_user = local;
        host_data[0] = 0;
        ready = failed = sent_scan = apply_requested = playing = false;
        next_poll = 0;
        deadline = now + SYNC_TIMEOUT;
        request_count = 0;
        status(NULL);
    }
    if (!room || !owner || now < next_poll || failed || apply_requested || playing || ready)
        return;
    next_poll = now + 1000;
    if (now >= deadline) {
        failed = true;
        write_file(L"error.txt", "timeout");
        return;
    }
    if (local == owner)
        publish(room);
    else
        prepare_guest(room, owner);
}

bool bm_modsync_wants_messages(void)
{
    return room_id && !playing && !failed && !apply_requested &&
           (local_user == room_owner || (!ready && !host_data[0]));
}

bool bm_modsync_local_ready(void)
{
    return !folder[0] || ready;
}

uint64_t bm_modsync_rejoin(uint64_t room, uint64_t *host)
{
    if (!rejoin_room || room || GetTickCount64() < rejoin_delay)
        return 0;
    rejoin_delay = GetTickCount64() + 1000;
    if (!bm_friends_recovery_room(rejoin_room, rejoin_host))
        return 0;
    uint64_t lobby = rejoin_room;
    rejoin_room = 0;
    *host = rejoin_host;
    return lobby;
}
