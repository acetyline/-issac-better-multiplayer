#include <windows.h>
#include "bm_game_build.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm_goodtrip.h"

#define PACK_ACTION BM_GAME_RVA(0x50ebf2, 0x50f0e2)
#define UNPACK_ACTION BM_GAME_RVA(0x523563, 0x523a73)
#define NATIVE_ACTION BM_GAME_RVA(0x50ee10, 0x50f300)
#define SCRIPT_CALL BM_GAME_RVA(0x47851b, 0x47867b)
#define LUA_PCALL_IMPORT BM_GAME_RVA(0x73a43c, 0x73b43c)
#define LUA_UNREF_IMPORT BM_GAME_RVA(0x73a380, 0x73b380)
#define SCRIPT_PATH BM_GAME_RVA(0x85d350, 0x85e350)
#define NET_STATE BM_GAME_RVA(0x89a094, 0x89b194)
#define PROFILE_STEAM_ID BM_GAME_RVA(0x63d410, 0x63daa0)

enum {
    NET_FRAME = 0x4b6b4, NET_ACTIVE = 0x4b6d0,
    DEVICE_PROFILE = 0x370, DEVICE_PRESSED = 0x30,
    MAP_ACTION = 13, SPARE_FLAG = 1 << 15, NO_WIRE_BIT = 255, ROLE_HOST = 1,
    NATIVE_CONTROL_BITS = 3, WIRE_START = 64, WIRE_NEXT = 32, WIRE_FRAMES = 3,
    GRID_COUNT = 13 * 13
};

typedef struct lua_State Lua;
typedef int (__cdecl *LuaFunction)(Lua *);
typedef int (__cdecl *LuaContinuation)(Lua *, int, intptr_t);
static int (__cdecl *lua_load)(Lua *, const char *, size_t, const char *, const char *);
static int (__cdecl *lua_top)(Lua *);
static void (__cdecl *lua_reset_top)(Lua *, int);
static void (__cdecl *lua_copy)(Lua *, int, int);
static void (__cdecl *lua_push_function)(Lua *, LuaFunction, int);
static void (__cdecl *lua_push_integer)(Lua *, int64_t);
static void (__cdecl *lua_push_bool)(Lua *, int);
static int64_t (__cdecl *lua_integer)(Lua *, int, int *);
static const char *(__cdecl *lua_string)(Lua *, int, size_t *);

static int (__fastcall *native_action_bit)(unsigned);
static uint64_t (__thiscall *profile_steam_id)(const void *);
static int (*friend_room_role)(void);
static uint64_t (*friend_room_owner)(void);
static void (*report)(const char *);
static const uint8_t *script_path;
static const uint8_t *const *net_state;
static int (__cdecl *const *stock_pcall)(Lua *, int, int, int, intptr_t, LuaContinuation);
static void (__cdecl *stock_lua_unref)(Lua *, int, int);
static Lua *live_lua_state;
static bool installed, initialized, rng_initialized, timemachine_ready = true;

static const char companion[] = {
#embed "goodtrip.lua"
};
static const char shared_rng[] = {
#embed "shared_rng.lua"
};

static const unsigned char gt_asset_minimap0_anm2[] = {
#embed "goodtrip_assets/minimap0.anm2"
};
static const unsigned char gt_asset_minimap0_png[] = {
#embed "goodtrip_assets/minimap0.png"
};
static const unsigned char gt_asset_ui_anm2[] = {
#embed "goodtrip_assets/gt_ui.anm2"
};
static const unsigned char gt_asset_ui1_png[] = {
#embed "goodtrip_assets/gt_ui1.png"
};
static const unsigned char gt_asset_select_png[] = {
#embed "goodtrip_assets/select.png"
};
static const unsigned char gt_asset_exit_anm2[] = {
#embed "goodtrip_assets/gt_exit.anm2"
};
static const unsigned char gt_asset_exit_png[] = {
#embed "goodtrip_assets/gt_exit.png"
};

static void extract_goodtrip_assets(void)
{
    static const struct {
        const WCHAR *name;
        const unsigned char *data;
        size_t size;
    } assets[] = {
        { L"minimap0.anm2", gt_asset_minimap0_anm2, sizeof(gt_asset_minimap0_anm2) },
        { L"minimap0.png", gt_asset_minimap0_png, sizeof(gt_asset_minimap0_png) },
        { L"gt_ui.anm2", gt_asset_ui_anm2, sizeof(gt_asset_ui_anm2) },
        { L"gt_ui1.png", gt_asset_ui1_png, sizeof(gt_asset_ui1_png) },
        { L"select.png", gt_asset_select_png, sizeof(gt_asset_select_png) },
        { L"gt_exit.anm2", gt_asset_exit_anm2, sizeof(gt_asset_exit_anm2) },
        { L"gt_exit.png", gt_asset_exit_png, sizeof(gt_asset_exit_png) }
    };

    WCHAR root[1024];
    const DWORD count = GetModuleFileNameW(NULL, root, ARRAYSIZE(root));
    if (!count || count >= ARRAYSIZE(root) - 64)
        return;
    WCHAR *slash = wcsrchr(root, L'\\');
    if (!slash)
        return;
    slash[1] = L'\0';

    WCHAR dir[1024];
    if (_snwprintf(dir, ARRAYSIZE(dir), L"%lsresources", root) < 0)
        return;
    CreateDirectoryW(dir, NULL);
    if (_snwprintf(dir, ARRAYSIZE(dir), L"%lsresources\\bm_goodtrip", root) < 0)
        return;
    CreateDirectoryW(dir, NULL);

    for (size_t i = 0; i < ARRAYSIZE(assets); ++i) {
        WCHAR file_path[1024];
        if (_snwprintf(file_path, ARRAYSIZE(file_path), L"%ls\\%ls", dir, assets[i].name) < 0)
            continue;
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(file_path, GetFileExInfoStandard, &info) &&
            info.nFileSizeHigh == 0 && info.nFileSizeLow == (DWORD)assets[i].size)
            continue;
        FILE *out = _wfopen(file_path, L"wb");
        if (!out)
            continue;
        fwrite(assets[i].data, 1, assets[i].size, out);
        fclose(out);
    }
}

/* J511 carries these fields in both the local and remote lockstep input queues. */
typedef struct {
    uint32_t number;
    uint16_t flags;
    uint16_t analog[4];
    uint8_t menu, control;
} FrameInput;

_Static_assert(sizeof(FrameInput) == 16 && offsetof(FrameInput, control) == 15, "J511 input layout");

static struct {
    const void *device;
    uint32_t last_frame;
    unsigned command, position, last_symbol;
} transmit;
static struct {
    uint32_t last_frame;
    unsigned remaining, command;
    bool has_frame;
} receive;
static unsigned arrival;
static bool arrived, map_held;
static ULONGLONG map_sample;
static const void *map_device;

void bm_goodtrip_reset(void)
{
    memset(&transmit, 0, sizeof(transmit));
    memset(&receive, 0, sizeof(receive));
    arrived = map_held = false;
    map_sample = 0;
    map_device = NULL;
}

static void pack_symbol(FrameInput *frame, unsigned symbol)
{
    // Keep the two control bits that J511 reads. Its serializer carries the whole byte.
    frame->control = (uint8_t)((frame->control & NATIVE_CONTROL_BITS) | ((symbol & 63) << 2));
    frame->flags = (uint16_t)((frame->flags & ~SPARE_FLAG) | ((symbol & WIRE_START) ? SPARE_FLAG : 0));
}

static unsigned command_symbol(unsigned command, unsigned position)
{
    // One start symbol carries six bits. Two continuations carry five bits each.
    if (position == 0)
        return WIRE_START | (command >> 10);
    return WIRE_NEXT | ((command >> (position == 1 ? 5 : 0)) & (WIRE_NEXT - 1));
}

__attribute__((used, noinline))
static int __cdecl pack_frame(unsigned action, FrameInput *frame, void *device)
{
    if (action != MAP_ACTION)
        return native_action_bit(action);
    if (friend_room_role() != ROLE_HOST)
        return NO_WIRE_BIT;
    void **methods = *(void ***)device;
    bool (__thiscall *pressed)(void *, unsigned) = methods[DEVICE_PRESSED / sizeof(void *)];
    const uint64_t now = GetTickCount64();
    if (!map_device || map_device == device || now - map_sample >= 500) {
        map_held = pressed(device, MAP_ACTION);
        map_device = map_held ? device : NULL;
        map_sample = now;
    }
    if (transmit.device == device && frame->number == transmit.last_frame) {
        pack_symbol(frame, transmit.last_symbol);
        return NO_WIRE_BIT;
    }
    if (!transmit.position || (transmit.position > 1 && transmit.device != device))
        return NO_WIRE_BIT;
    transmit.device = device;
    unsigned position = transmit.position - 1;
    if (position && frame->number != transmit.last_frame + 1)
        position = 0;
    transmit.last_symbol = command_symbol(transmit.command, position);
    pack_symbol(frame, transmit.last_symbol);
    transmit.last_frame = frame->number;
    transmit.position = position + 1 == WIRE_FRAMES ? 0 : position + 2;
    return NO_WIRE_BIT;
}

static void decode_frame(const FrameInput *frame)
{
    if (receive.has_frame && frame->number == receive.last_frame)
        return;
    if (frame->number != receive.last_frame + 1)
        receive.remaining = 0;
    receive.has_frame = true;
    receive.last_frame = frame->number;
    const unsigned symbol = ((frame->flags & SPARE_FLAG) ? WIRE_START : 0) | (frame->control >> 2);
    if (symbol & WIRE_START) {
        receive.command = symbol & (WIRE_START - 1);
        receive.remaining = WIRE_FRAMES - 1;
        return;
    }
    if (!(symbol & WIRE_NEXT)) {
        receive.remaining = 0;
        return;
    }
    if (!receive.remaining)
        return;

    receive.command = (receive.command << 5) | (symbol & (WIRE_NEXT - 1));
    if (!--receive.remaining && !__builtin_parity(receive.command)) {
        const unsigned command = receive.command >> 1;
        if (command < GRID_COUNT * GRID_COUNT && command / GRID_COUNT != command % GRID_COUNT) {
            arrival = command;
            arrived = true;
        }
    }
}

__attribute__((used, noinline))
static int __cdecl unpack_frame(unsigned action, const uint8_t *device, const FrameInput *frame)
{
    if (action != MAP_ACTION)
        return native_action_bit(action);
    const uint64_t owner = friend_room_owner();
    if (owner && frame && profile_steam_id(device + DEVICE_PROFILE) == owner)
        decode_frame(frame);
    return NO_WIRE_BIT;
}

__attribute__((naked))
static int __fastcall pack_action(unsigned action __attribute__((unused)))
{
    __asm__ volatile("pushl 12(%ebp)\n\tpushl %edi\n\tpushl %ecx\n\t"
                     "call _pack_frame\n\taddl $12, %esp\n\tret");
}

__attribute__((naked))
static int __fastcall unpack_action(unsigned action __attribute__((unused)))
{
    __asm__ volatile("pushl -28(%ebp)\n\tpushl -52(%ebp)\n\tpushl %ecx\n\t"
                     "call _unpack_frame\n\taddl $12, %esp\n\tret");
}

static int __cdecl room_role_lua(Lua *state)
{
    lua_push_integer(state, friend_room_role());
    return 1;
}

static int __cdecl input_lua(Lua *state)
{
    lua_push_bool(state, map_held);
    return 1;
}

static int __cdecl send_lua(Lua *state)
{
    int have_from, have_to;
    const int64_t from = lua_integer(state, 1, &have_from);
    const int64_t to = lua_integer(state, 2, &have_to);
    const bool accepted = friend_room_role() == ROLE_HOST && !transmit.position && !arrived &&
                          have_from && have_to && from >= 0 && from < GRID_COUNT &&
                          to >= 0 && to < GRID_COUNT && from != to;
    if (accepted) {
        const unsigned command = (unsigned)from * GRID_COUNT + (unsigned)to;
        transmit.command = (command << 1) | __builtin_parity(command);
        transmit.position = 1;
    }
    lua_push_bool(state, accepted);
    return 1;
}

static int __cdecl take_lua(Lua *state)
{
    if (!arrived)
        return 0;
    arrived = false;
    lua_push_integer(state, arrival / GRID_COUNT);
    lua_push_integer(state, arrival % GRID_COUNT);
    return 2;
}

static int __cdecl reset_lua(Lua *state)
{
    (void)state;
    bm_goodtrip_reset();
    return 0;
}

static int __cdecl simulation_frame_lua(Lua *state)
{
    const uint8_t *net = *net_state;
    if (!net || !net[NET_ACTIVE])
        return 0;
    uint32_t frame;
    memcpy(&frame, net + NET_FRAME, sizeof(frame));
    lua_push_integer(state, frame);
    return 1;
}

static const char *current_script(size_t *size)
{
    uint32_t length, capacity;
    memcpy(&length, script_path + 16, sizeof(length));
    memcpy(&capacity, script_path + 20, sizeof(capacity));
    if (length > 32767 || capacity < length)
        return NULL;
    const char *path = (const char *)script_path;
    if (capacity >= 16)
        memcpy(&path, script_path, sizeof(path));
    *size = length;
    return path;
}

static bool separator(char value)
{
    return value == '/' || value == '\\';
}

static bool script_ends_with(const char *path, size_t length, const char *suffix)
{
    const size_t count = strlen(suffix);
    if (!path || length < count || (length > count && !separator(path[length - count - 1])))
        return false;
    path += length - count;
    for (size_t i = 0; i < count; ++i) {
        char c = path[i];
        if (separator(c))
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != suffix[i])
            return false;
    }
    return true;
}

static bool goodtrip_name(const char *name, size_t length)
{
    for (size_t i = 0; i + 8 <= length; ++i) {
        if (_strnicmp(name + i, "good", 4) != 0)
            continue;
        size_t next = i + 4;
        while (next < length && name[next] == ' ')
            ++next;
        if (next + 4 <= length && _strnicmp(name + next, "trip", 4) == 0)
            return true;
    }
    return false;
}

static bool catalog_blocks(const char *name, size_t length)
{
    WCHAR path[1024];
    const WCHAR filename[] = L"\\blocked-mods.txt";
    const DWORD size = GetEnvironmentVariableW(L"BETTERMULTIPLAYER_MODSYNC", path, ARRAYSIZE(path));
    if (!size || size >= ARRAYSIZE(path) || size + ARRAYSIZE(filename) > ARRAYSIZE(path))
        return false;
    memcpy(path + size, filename, sizeof(filename));
    FILE *catalog = _wfopen(path, L"rb");
    if (!catalog)
        return false;

    WCHAR folder[MAX_PATH], ansi_folder[MAX_PATH];
    const int utf8_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, (int)length, folder, ARRAYSIZE(folder));
    const int ansi_count = MultiByteToWideChar(CP_ACP, 0, name, (int)length, ansi_folder, ARRAYSIZE(ansi_folder));
    bool blocked = false;
    char line[1024];
    while (!blocked && fgets(line, sizeof(line), catalog)) {
        WCHAR entry[MAX_PATH];
        const int entry_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line,
                                                    (int)strcspn(line, "\r\n"), entry, ARRAYSIZE(entry));
        blocked = entry_count &&
                  ((utf8_count && CompareStringOrdinal(folder, utf8_count, entry, entry_count, TRUE) == CSTR_EQUAL) ||
                   (ansi_count && CompareStringOrdinal(ansi_folder, ansi_count, entry, entry_count, TRUE) == CSTR_EQUAL));
    }
    fclose(catalog);
    return blocked;
}

static bool blocked_mod_script(const char *path, size_t length)
{
    if (!script_ends_with(path, length, "main.lua") || length < sizeof("mods/a/main.lua") - 1)
        return false;
    const size_t end = length - (sizeof("/main.lua") - 1);
    size_t start = end;
    while (start && !separator(path[start - 1]))
        --start;
    return start > 0 && script_ends_with(path, start - 1, "mods") &&
           (goodtrip_name(path + start, end - start) || catalog_blocks(path + start, end - start));
}

static void __cdecl safe_lua_unref(Lua *state, int table, int ref)
{
    if (!state || state != live_lua_state || ref <= 0 || !stock_lua_unref)
        return;
    stock_lua_unref(state, table, ref);
}

static void install_lua_unref_guard(HMODULE lua)
{
    if (stock_lua_unref)
        return;
    void (__cdecl *native_unref)(Lua *, int, int) = (void (__cdecl *)(Lua *, int, int))(void *)GetProcAddress(lua, "luaL_unref");
    if (!native_unref)
        return;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    void **slot = (void **)(image + LUA_UNREF_IMPORT);
    if (*slot != (void *)native_unref)
        return;
    DWORD previous, discarded;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous))
        return;
    stock_lua_unref = native_unref;
    *slot = (void *)safe_lua_unref;
    VirtualProtect(slot, sizeof(*slot), previous, &discarded);
}

static bool load_lua_api(void);

static bool replace_timemachine_draw(Lua *state, const char *path, size_t path_length)
{
    static const char original[] = "math.floor(tmmc.speed[i]-0.5+math.random())";
    static const char replacement[] = "math.floor(tmmc.speed[i]-0.5+BetterMultiplayerRng.TimeMachine(slot.InitSeed, pl.InitSeed))";
    static const char orig_input_lf[] =
        "if ((Input.IsActionPressed(ButtonAction.ACTION_UP,pl.ControllerIndex) and dy > 0) or (Input.IsActionPressed(ButtonAction.ACTION_DOWN,pl.ControllerIndex) and dy < 0))\n"
        "            and (not Input.IsActionPressed(ButtonAction.ACTION_RIGHT,pl.ControllerIndex)) and (not Input.IsActionPressed(ButtonAction.ACTION_LEFT,pl.ControllerIndex)) then";
    static const char orig_input_crlf[] =
        "if ((Input.IsActionPressed(ButtonAction.ACTION_UP,pl.ControllerIndex) and dy > 0) or (Input.IsActionPressed(ButtonAction.ACTION_DOWN,pl.ControllerIndex) and dy < 0))\r\n"
        "            and (not Input.IsActionPressed(ButtonAction.ACTION_RIGHT,pl.ControllerIndex)) and (not Input.IsActionPressed(ButtonAction.ACTION_LEFT,pl.ControllerIndex)) then";
    static const char repl_input[] =
        "local mv = pl:GetMovementInput() if ((mv.Y < -0.5 and dy > 0) or (mv.Y > 0.5 and dy < 0)) and math.abs(mv.X) < 0.2 then";
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    const bool seeked = fseek(file, 0, SEEK_END) == 0;
    const long size = seeked ? ftell(file) : -1;
    if (size <= 0 || size > 128 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *source = malloc((size_t)size + sizeof(replacement) + sizeof(repl_input));
    char *chunk = malloc(path_length + 2);
    bool complete = source && chunk && fread(source, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (complete) {
        size_t cur_size = (size_t)size;
        source[cur_size] = '\0';
        char *inp = strstr(source, orig_input_crlf);
        size_t inp_len = sizeof(orig_input_crlf) - 1;
        if (!inp) {
            inp = strstr(source, orig_input_lf);
            inp_len = sizeof(orig_input_lf) - 1;
        }
        if (inp) {
            const size_t repl_len = sizeof(repl_input) - 1;
            memmove(inp + repl_len, inp + inp_len, (size_t)(source + cur_size - inp) - inp_len + 1);
            memcpy(inp, repl_input, repl_len);
            cur_size = cur_size - inp_len + repl_len;
        }
        char *draw = strstr(source, original);
        complete = draw && !strstr(draw + sizeof(original) - 1, original);
        if (complete) {
            memmove(draw + sizeof(replacement) - 1, draw + sizeof(original) - 1,
                    (size_t)(source + cur_size - draw) - (sizeof(original) - 1) + 1);
            memcpy(draw, replacement, sizeof(replacement) - 1);
            cur_size = cur_size + sizeof(replacement) - sizeof(original);
            chunk[0] = '@';
            memcpy(chunk + 1, path, path_length + 1);
            const size_t skip_bom = cur_size >= 3 && memcmp(source, "\xef\xbb\xbf", 3) == 0 ? 3 : 0;
            const int top = lua_top(state);
            complete = lua_load(state, source + skip_bom, cur_size - skip_bom, chunk, "t") == 0;
            if (complete)
                lua_copy(state, -1, top);
            lua_reset_top(state, top);
        }
    }
    free(chunk);
    free(source);
    return complete;
}

static bool run_companion(Lua *state, const char *name, const char *code, size_t length,
                          const LuaFunction *inputs, unsigned count)
{
    const int top = lua_top(state);
    int status = lua_load(state, code, length, name, "t");
    if (!status) {
        for (unsigned i = 0; i < count; ++i)
            lua_push_function(state, inputs[i], 0);
        status = (*stock_pcall)(state, (int)count, 0, 0, 0, NULL);
    }
    if (status) {
        const char *error = lua_string(state, -1, NULL);
        if (error) report(error);
    }
    lua_reset_top(state, top);
    return status == 0;
}

static int __cdecl run_script(Lua *state, int args, int results, int err, intptr_t context, LuaContinuation next)
{
    size_t length = 0;
    const char *path = current_script(&length);
    const bool startup = script_ends_with(path, length, "resources/scripts/main.lua");
    if (startup)
        live_lua_state = state;
    if (args == 0 && results == -1 && blocked_mod_script(path, length) && lua_reset_top) {
        lua_reset_top(state, -2);
        report("Skipped a GoodTrip mod in the friends edition.\r\n");
        return 0;
    }
    const bool timemachine = args == 0 && results == -1 &&
                             script_ends_with(path, length, "timemachine_2617557401/main.lua");
    if (timemachine)
        timemachine_ready = rng_initialized && replace_timemachine_draw(state, path, length);

    const int result = (*stock_pcall)(state, args, results, err, context, next);
    if (timemachine) {
        timemachine_ready &= result == 0;
        report(timemachine_ready ? "TimeMachine shared RNG ready.\r\n" :
                                   "TimeMachine shared RNG could not be installed.\r\n");
    }
    if (!startup)
        return result;
    initialized = rng_initialized = false;
    timemachine_ready = true;
    if (result || !load_lua_api()) {
        report("Could not initialize the Better Multiplayer Lua support.\r\n");
        return result;
    }

    const LuaFunction random_inputs[] = {simulation_frame_lua};
    rng_initialized = run_companion(state, "BetterMultiplayer/SharedRng", shared_rng, sizeof(shared_rng),
                                    random_inputs, ARRAYSIZE(random_inputs));
    extract_goodtrip_assets();
    const LuaFunction goodtrip_inputs[] = {room_role_lua, input_lua, send_lua, take_lua, reset_lua};
    initialized = run_companion(state, "BetterMultiplayer/GoodTrip", companion, sizeof(companion),
                                goodtrip_inputs, ARRAYSIZE(goodtrip_inputs));
    report(initialized ? "Built-in GoodTrip ready.\r\n" : "Could not initialize built-in GoodTrip.\r\n");
    return result;
}

static bool calls_action(const uint8_t *image, unsigned site)
{
    int32_t distance;
    memcpy(&distance, image + site + 1, sizeof(distance));
    return image[site] == 0xe8 && distance == (int32_t)(NATIVE_ACTION - site - 5);
}

static bool load_lua_api(void)
{
    HMODULE lua = GetModuleHandleW(L"Lua5.3.3r.dll");
    if (!lua)
        return false;
    #define BIND(target, name) target = (__typeof__(target))(void *)GetProcAddress(lua, name); if (!target) return false
    BIND(lua_load, "luaL_loadbufferx");
    BIND(lua_top, "lua_gettop");
    BIND(lua_reset_top, "lua_settop");
    BIND(lua_copy, "lua_copy");
    BIND(lua_push_function, "lua_pushcclosure");
    BIND(lua_push_integer, "lua_pushinteger");
    BIND(lua_push_bool, "lua_pushboolean");
    BIND(lua_integer, "lua_tointegerx");
    BIND(lua_string, "lua_tolstring");
    #undef BIND
    install_lua_unref_guard(lua);
    return true;
}

bool bm_goodtrip_install(int (*room_role)(void), uint64_t (*room_owner)(void), void (*reporter)(const char *))
{
    if (installed)
        return true;
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < NET_STATE + sizeof(void *) ||
        !calls_action(image, PACK_ACTION) || !calls_action(image, UNPACK_ACTION) ||
        memcmp(image + BM_GAME_RVA(0x50ea35, 0x50ef25), "\x0f\xb6\x46\x0f", 4) || // Serialize the whole control byte.
        memcmp(image + BM_GAME_RVA(0x50eb55, 0x50f045), "\x88\x47\x0f", 3) ||     // Deserialize the whole control byte.
        memcmp(image + BM_GAME_RVA(0x523622, 0x523b32), "\xf6\x41\x0f\x02", 4) || // Read native control bit 1.
        memcmp(image + BM_GAME_RVA(0x523688, 0x523b98), "\x0f\xbe\x41\x0f", 4) ||
        memcmp(image + BM_GAME_RVA(0x523694, 0x523ba4), "\x83\xe0\x01", 3))      // Read native control bit 0.
        return false;

    uint8_t *pcall = image + SCRIPT_CALL;
    uintptr_t import;
    memcpy(&import, pcall + 2, sizeof(import));
    if (pcall[0] != 0xff || pcall[1] != 0x15 || import != (uintptr_t)(image + LUA_PCALL_IMPORT))
        return false;

    uint8_t *sites[] = { image + PACK_ACTION, image + UNPACK_ACTION, pcall };
    const uintptr_t targets[] = { (uintptr_t)pack_action, (uintptr_t)unpack_action, (uintptr_t)run_script };
    DWORD access[3], discarded;
    unsigned count = 0;
    while (count < 3 && VirtualProtect(sites[count], 6, PAGE_EXECUTE_READWRITE, &access[count]))
        ++count;
    if (count == 3) {
        native_action_bit = (int (__fastcall *)(unsigned))(image + NATIVE_ACTION);
        profile_steam_id = (uint64_t (__thiscall *)(const void *))(image + PROFILE_STEAM_ID);
        friend_room_role = room_role;
        friend_room_owner = room_owner;
        script_path = image + SCRIPT_PATH;
        net_state = (const uint8_t *const *)(image + NET_STATE);
        stock_pcall = (void *)(image + LUA_PCALL_IMPORT);
        report = reporter;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t offset = (uint32_t)(targets[i] - ((uintptr_t)sites[i] + 5));
            sites[i][0] = 0xe8;
            memcpy(sites[i] + 1, &offset, sizeof(offset));
        }
        pcall[5] = 0x90;
        for (unsigned i = 0; i < count; ++i)
            FlushInstructionCache(GetCurrentProcess(), sites[i], 6);
        installed = true;
    }
    while (count > 0) {
        --count;
        VirtualProtect(sites[count], 6, access[count], &discarded);
    }
    return installed;
}

bool bm_goodtrip_ready(void)
{
    return initialized;
}

bool bm_shared_rng_ready(void)
{
    return rng_initialized && timemachine_ready;
}
