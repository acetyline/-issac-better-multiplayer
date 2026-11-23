#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "bm_goodtrip.h"

enum {
    PACK_ACTION = 0x50ebf2, UNPACK_ACTION = 0x523563, NATIVE_ACTION = 0x50ee10,
    SCRIPT_CALL = 0x47851b, LUA_PCALL_IMPORT = 0x73a43c, SCRIPT_PATH = 0x85d350,
    PROFILE_STEAM_ID = 0x63d410, DEVICE_PROFILE = 0x370, DEVICE_PRESSED = 0x30,
    MAP_ACTION = 13, SPARE_FLAG = 1 << 15, NO_WIRE_BIT = 255, ROLE_HOST = 1,
    HEADER_SIZE = 5, DATA_BITS = 9, WIRE_FRAMES = HEADER_SIZE + 2 * DATA_BITS,
    MAX_GRID_INDEX = 168
};

typedef struct lua_State Lua;
typedef int (__cdecl *LuaFunction)(Lua *);
typedef int (__cdecl *LuaContinuation)(Lua *, int, intptr_t);
static int (__cdecl *lua_pcall)(Lua *, int, int, int, intptr_t, LuaContinuation);
static int (__cdecl *lua_load)(Lua *, const char *, size_t, const char *, const char *);
static int (__cdecl *lua_top)(Lua *);
static void (__cdecl *lua_reset_top)(Lua *, int);
static void (__cdecl *lua_push_function)(Lua *, LuaFunction, int);
static void (__cdecl *lua_push_integer)(Lua *, int64_t);
static void (__cdecl *lua_push_bool)(Lua *, int);
static int64_t (__cdecl *lua_integer)(Lua *, int, int *);
static const char *(__cdecl *lua_string)(Lua *, int, size_t *);
static const char *(__cdecl *lua_get_upvalue)(Lua *, int, int);
static const char *(__cdecl *lua_set_upvalue)(Lua *, int, int);
static void (__cdecl *lua_copy_to_top)(Lua *, int);

static int (__fastcall *native_action_bit)(unsigned);
static uint64_t (__thiscall *profile_steam_id)(const void *);
static int (*friend_room_role)(void);
static uint64_t (*friend_room_owner)(void);
static void (*report)(const char *);
static const uint8_t *script_path;
static bool installed;

static const char companion[] = {
#embed "goodtrip.lua"
};

/* J511 carries these fields in both the local and remote lockstep input queues. */
typedef struct {
    uint32_t number;
    uint16_t flags;
} FrameInput;

static struct {
    const void *device;
    uint32_t last_frame;
    unsigned payload, position;
} transmit;
static struct {
    uint32_t last_frame;
    unsigned ones, position, payload;
    bool receiving, has_frame;
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

static bool command_bit(unsigned position, unsigned payload)
{
    /* Four ones start a command. Each payload bit follows a zero. */
    if (position < HEADER_SIZE)
        return position < HEADER_SIZE - 1;
    position -= HEADER_SIZE;
    return (position & 1) && ((payload >> (position / 2)) & 1);
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
    if (!transmit.position)
        return NO_WIRE_BIT;
    if (!transmit.device)
        transmit.device = device;
    if (transmit.device != device)
        return NO_WIRE_BIT;
    unsigned position = transmit.position - 1;
    if (position && frame->number != transmit.last_frame + 1)
        position = 0;
    if (command_bit(position, transmit.payload))
        frame->flags |= SPARE_FLAG;
    transmit.last_frame = frame->number;
    transmit.position = position + 1 == WIRE_FRAMES ? 0 : position + 2;
    return NO_WIRE_BIT;
}

static void decode_frame(const FrameInput *frame)
{
    if (receive.has_frame && frame->number == receive.last_frame)
        return;
    if (frame->number != receive.last_frame + 1) {
        receive.receiving = false;
        receive.ones = 0;
    }
    receive.has_frame = true;
    receive.last_frame = frame->number;
    const bool bit = (frame->flags & SPARE_FLAG) != 0;
    if (!receive.receiving) {
        if (bit) {
            if (receive.ones < HEADER_SIZE)
                ++receive.ones;
        } else {
            receive.receiving = receive.ones == HEADER_SIZE - 1;
            receive.ones = receive.position = receive.payload = 0;
        }
        return;
    }
    const unsigned position = receive.position++;
    if (!(position & 1) && bit) {
        receive.receiving = false;
        receive.ones = 1;
    } else if (position & 1) {
        receive.payload |= (unsigned)bit << (position / 2);
        if (receive.position == 2 * DATA_BITS) {
            receive.receiving = false;
            if ((receive.payload & 255) <= MAX_GRID_INDEX) {
                arrival = receive.payload;
                arrived = true;
            }
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
    int integer;
    int64_t grid = lua_integer(state, 1, &integer);
    int64_t animation = lua_integer(state, 2, NULL);
    if (friend_room_role() == ROLE_HOST && !transmit.position && !arrived &&
        integer && grid >= 0 && grid <= MAX_GRID_INDEX) {
        transmit.device = NULL;
        transmit.payload = (unsigned)grid | (animation == 3 ? 256 : 0);
        transmit.position = 1;
    }
    return 0;
}

static int __cdecl take_lua(Lua *state)
{
    if (!arrived)
        return 0;
    arrived = false;
    lua_push_integer(state, arrival & 255);
    lua_push_integer(state, (arrival & 256) ? 3 : 1);
    return 2;
}

static int __cdecl upvalue_lua(Lua *state)
{
    const char *name = lua_string(state, 2, NULL);
    const bool replace = lua_top(state) == 3;
    if (name) {
        for (int index = 1; ; ++index) {
            const char *candidate = lua_get_upvalue(state, 1, index);
            if (!candidate)
                break;
            if (strcmp(name, candidate) == 0) {
                if (replace) {
                    lua_copy_to_top(state, 3);
                    lua_set_upvalue(state, 1, index);
                }
                return 1;
            }
            lua_reset_top(state, -2);
        }
    }
    return 0;
}

static int __cdecl reset_lua(Lua *state)
{
    (void)state;
    bm_goodtrip_reset();
    return 0;
}

static bool is_goodtrip_script(void)
{
    static const char suffix[] = "goodtrip_1630477831/main.lua";
    uint32_t length, capacity;
    memcpy(&length, script_path + 16, sizeof(length));
    memcpy(&capacity, script_path + 20, sizeof(capacity));
    const size_t count = sizeof(suffix) - 1;
    if (length <= count || length > 32767 || capacity < length)
        return false;
    const char *path = (const char *)script_path;
    if (capacity >= 16)
        memcpy(&path, script_path, sizeof(path));
    if (!path || (path[length - count - 1] != '/' && path[length - count - 1] != '\\'))
        return false;
    path += length - count;
    for (size_t i = 0; i < count; ++i) {
        char c = path[i];
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != suffix[i]) return false;
    }
    return true;
}

static int __cdecl run_script(Lua *state, int args, int results, int err, intptr_t context, LuaContinuation next)
{
    const bool goodtrip = is_goodtrip_script();
    int result = lua_pcall(state, args, results, err, context, next);
    if (goodtrip && result == 0) {
        const int top = lua_top(state);
        int status = lua_load(state, companion, sizeof(companion), "BetterMultiplayer/GoodTrip", "t");
        if (status == 0) {
            const LuaFunction inputs[] = {room_role_lua, input_lua, send_lua, take_lua, upvalue_lua, reset_lua};
            for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
                lua_push_function(state, inputs[i], 0);
            status = lua_pcall(state, sizeof(inputs) / sizeof(inputs[0]), 0, 0, 0, NULL);
        }
        if (status) {
            const char *error = lua_string(state, -1, NULL);
            if (error)
                report(error);
        }
        lua_reset_top(state, top);
        report(status ? "GoodTrip friend-room integration error.\r\n" : "GoodTrip friend-room integration loaded.\r\n");
    }
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
    BIND(lua_pcall, "lua_pcallk");
    BIND(lua_load, "luaL_loadbufferx");
    BIND(lua_top, "lua_gettop");
    BIND(lua_reset_top, "lua_settop");
    BIND(lua_push_function, "lua_pushcclosure");
    BIND(lua_push_integer, "lua_pushinteger");
    BIND(lua_push_bool, "lua_pushboolean");
    BIND(lua_integer, "lua_tointegerx");
    BIND(lua_string, "lua_tolstring");
    BIND(lua_get_upvalue, "lua_getupvalue");
    BIND(lua_set_upvalue, "lua_setupvalue");
    BIND(lua_copy_to_top, "lua_pushvalue");
    #undef BIND
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
        pe->OptionalHeader.SizeOfImage < SCRIPT_PATH + 24 || !load_lua_api() ||
        !calls_action(image, PACK_ACTION) || !calls_action(image, UNPACK_ACTION))
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
