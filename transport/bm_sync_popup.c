#include <windows.h>
#include "bm_game_build.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bm_sync_popup.h"

#define NET_STATE BM_GAME_RVA(0x89a094, 0x89b194)
#define POPUP_CONSTRUCT BM_GAME_RVA(0x5d9db0, 0x5da440)
#define POPUP_TEXT BM_GAME_RVA(0x5d9ed0, 0x5da560)
#define POPUP_CONFIRM BM_GAME_RVA(0x5db640, 0x5dbcd0)
#define POPUP_ADD BM_GAME_RVA(0x5db270, 0x5db900)
#define POPUP_CLOSE BM_GAME_RVA(0x5db340, 0x5db9d0)

enum {
    POPUP_CONTEXT = 0x4b144, POPUP_MANAGER = 0x4d440,
    POPUP_DESTROY = 0x21680,
    POPUP_ENTRIES = 0x54, CONTINUE_CHOICE = 1, CANCEL_CHOICE = 2
};

typedef struct { float x, y; } Position;
typedef void (__cdecl *PopupHandler)(const uint32_t *, void *);
typedef struct {
    uint32_t type;
    const char *text;
    uint32_t choice, flags;
    float color[5];
    Position position;
    int format;
    void *context;
} PopupEntry;
_Static_assert(sizeof(PopupEntry) == 0x34 && offsetof(PopupEntry, position) == 0x24, "J511 popup entry layout");

static uint8_t *const *net_state;
static void (__thiscall *construct)(void *, void *, PopupHandler, void *, const Position *);
static void (__thiscall *add_text)(void *, const char *, const Position *, const int *, const float *, void *);
static void (__fastcall *confirm)(const char *, void *);
static int (__thiscall *add_popup)(void *, void *);
static void (__thiscall *close_popup)(void *, const int *, const uint32_t *);
static void (__thiscall *destroy)(void *);

static int handle = -1;
static uintptr_t generation;
static BmSyncPopupChoice result;

static void __cdecl choose(const uint32_t *choice, void *context)
{
    if (result == BM_SYNC_POPUP_WAITING && (uintptr_t)context == generation) {
        handle = -1;
        result = choice && *choice == CONTINUE_CHOICE ? BM_SYNC_POPUP_CONTINUE : BM_SYNC_POPUP_CANCEL;
    }
}

bool bm_sync_popup_init(void)
{
    uint8_t *image = (uint8_t *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage < NET_STATE + sizeof(void *) ||
        memcmp(image + POPUP_CONSTRUCT, "\x55\x8b\xec\x8b\x45\x08\x56", 7) ||
        memcmp(image + POPUP_TEXT, "\x55\x8b\xec\x83\xec\x34\x56", 7) ||
        memcmp(image + POPUP_CONFIRM, "\x55\x8b\xec\x83\xe4\xf8\x83\xec\x38", 9) ||
        memcmp(image + POPUP_ADD, "\x55\x8b\xec\x6a\xff\x68", 6) ||
        memcmp(image + POPUP_CLOSE, "\x55\x8b\xec\x51\x53\x8b\x5d\x08", 8) ||
        memcmp(image + POPUP_DESTROY, "\x56\x8b\xf1\x8d\x4e\x60", 6))
        return false;

    net_state = (uint8_t *const *)(image + NET_STATE);
    construct = (__typeof__(construct))(image + POPUP_CONSTRUCT);
    add_text = (__typeof__(add_text))(image + POPUP_TEXT);
    confirm = (__typeof__(confirm))(image + POPUP_CONFIRM);
    add_popup = (__typeof__(add_popup))(image + POPUP_ADD);
    close_popup = (__typeof__(close_popup))(image + POPUP_CLOSE);
    destroy = (__typeof__(destroy))(image + POPUP_DESTROY);
    return true;
}

bool bm_sync_popup_show(const char *title, const char *const *lines, unsigned count)
{
    uint8_t *net = net_state ? *net_state : NULL;
    if (!net || handle != -1)
        return false;

    _Alignas(16) uint8_t popup[0xa4];
    static const float ink[] = { 0.2157f, 0.1686f, 0.1765f, 1, 0 };
    const int format = 1;
    const Position center = { -1, -1 };
    ++generation;
    construct(popup, net + POPUP_CONTEXT, choose, (void *)generation, &center);
    // The exit confirmation builds the title and localized Yes/No choices.
    confirm(title, popup);
    PopupEntry *entries[2];
    memcpy(entries, popup + POPUP_ENTRIES, sizeof(entries));
    if (!entries[0] || (uintptr_t)entries[1] - (uintptr_t)entries[0] != 3 * sizeof(PopupEntry) ||
        entries[0][1].choice != CONTINUE_CHOICE || entries[0][2].choice != CANCEL_CHOICE) {
        destroy(popup);
        return false;
    }
    entries[0][1].position.y += 12 * count;
    entries[0][2].position.y += 12 * count;
    for (unsigned i = 0; i < count; ++i)
        add_text(popup, lines[i], &(Position){ 0, 12 + 12 * i }, &format, ink, NULL);

    result = BM_SYNC_POPUP_WAITING;
    handle = add_popup(net + POPUP_MANAGER, popup);
    destroy(popup);
    if (handle == -1)
        result = BM_SYNC_POPUP_IDLE;
    return handle != -1;
}

BmSyncPopupChoice bm_sync_popup_choice(void)
{
    return result;
}

void bm_sync_popup_dismiss(void)
{
    static const uint32_t cancel = CANCEL_CHOICE;
    ++generation;
    result = BM_SYNC_POPUP_IDLE;
    const int previous = handle;
    handle = -1;
    uint8_t *net = net_state ? *net_state : NULL;
    if (net && previous != -1)
        close_popup(net + POPUP_MANAGER, &previous, &cancel);
}
