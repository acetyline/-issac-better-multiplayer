#include "bm_locate.h"

#include <windows.h>
#include <stddef.h>
#include <string.h>

/*
 * Each pattern below was extracted from the stock J511 .text section and
 * verified to appear exactly once in both the stock image and the image
 * produced by the community Chinese patch (loader injection shifting the
 * section). Immediate fields that hold absolute addresses or call
 * displacements are masked out ('?' entries) because a patched image
 * legitimately changes them.
 */

typedef struct {
    const uint8_t *bytes;
    const uint8_t *mask; /* 1 = wildcard */
    size_t length;
} Pattern;

#define PATTERN(...) /* helper defined per-entry below */

#include <stdint.h>

#define W0 0
#define W1 1

#define PAT(n, ...) /* not used; table written literally */

/* Wildcard-friendly pattern tables. */
static const uint8_t pattern_recovery_complete[] = {
    0xe8, W0, W0, W0, W0, 0xe9, 0xe6, 0x02, 0x00, 0x00, 0x83, 0xf9, 0x04
};
static const uint8_t mask_recovery_complete[] = {
    W1, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0, W0, W0
};

static const uint8_t pattern_recovery_rebuild[] = {
    0xe8, 0xd1, 0xcc, 0x00, 0x00, 0x8b, 0x0d, W0, W0, W0, W0, W0, W0
};
static const uint8_t mask_recovery_rebuild[] = {
    W0, W0, W0, W0, W0, W0, W0, W1, W1, W1, W1, W1, W1
};

static const uint8_t pattern_recovery_timeout[] = {
    0xe8, 0x31, 0xc9, 0x00, 0x00, 0x8b, 0x15, W0, W0, W0, W0, W0, W0, W0
};
static const uint8_t mask_recovery_timeout[] = {
    W0, W0, W0, W0, W0, W0, W0, W1, W1, W1, W1, W1, W1, W1
};

static const uint8_t pattern_goodtrip_pack[] = {
    0xe8, W0, W0, W0, W0, 0x8b, 0x0d, W0, W0, W0, W0, 0x8b, 0xd8,
    0x83, 0xb9, W0, W0, W0, W0, 0x00, 0x74, 0x13
};
static const uint8_t mask_goodtrip_pack[] = {
    W1, W1, W1, W1, W1, W0, W0, W1, W1, W1, W1, W0, W0,
    W0, W0, W1, W1, W1, W1, W0, W0, W0
};

static const uint8_t pattern_goodtrip_unpack[] = {
    0xe8, W0, W0, W0, W0, 0x3d, 0xff, 0x00, 0x00, 0x00, 0x74, 0x2f,
    0x8b, 0xc8, 0xba, 0x01, 0x00, 0x00, 0x00, 0x8b
};
static const uint8_t mask_goodtrip_unpack[] = {
    W1, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0, W0,
    W0, W0, W0, W0, W0, W0, W0, W0
};

static const uint8_t pattern_goodtrip_script[] = {
    0xff, 0x15, W0, W0, W0, W0, 0x83, 0xc4, 0x18, 0xc7, 0x85,
    0x68, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t mask_goodtrip_script[] = {
    W0, W0, W1, W1, W1, W1, W0, W0, W0, W0, W0,
    W0, W0, W0, W0, W0, W0, W0, W0
};

static const uint8_t pattern_modsync_start1[] = {
    0x8b, 0xce, 0xe8, W0, W0, W0, W0, 0xc7, 0x86, 0x58, 0x09
};
static const uint8_t mask_modsync_start1[] = {
    W0, W0, W1, W1, W1, W1, W1, W0, W0, W0, W0
};

static const uint8_t pattern_modsync_start2[] = {
    0xe8, W0, W0, W0, W0, 0x80, 0xbe, 0xc1, 0x06, 0x00, 0x00, 0x00,
    0x74, 0x44, 0xff, 0xb6, 0xc4, 0x06, 0x00, 0x00, 0x68, W0, W0,
    W0, W0, 0x6a, 0x01, 0xe8
};
static const uint8_t mask_modsync_start2[] = {
    W1, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0, W0,
    W0, W0, W0, W0, W0, W0, W0, W0, W0, W1, W1,
    W1, W1, W0, W0, W0
};

static const uint8_t pattern_modsync_send_input[] = {
    0xe8, W0, W0, W0, W0, 0xe9, 0xb6, 0x00, 0x00, 0x00, 0x56
};
static const uint8_t mask_modsync_send_input[] = {
    W1, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0
};

static const uint8_t pattern_characters_lobby[] = {
    0xe8, W0, W0, W0, W0, 0x84, 0xc0, 0x75, 0x0b, 0x6a, 0x01, 0x6a, 0x01,
    0x8b, 0xcf, 0xe8, W0, W0, W0, W0, 0x8b, 0x0e, 0x32, 0xd2
};
static const uint8_t mask_characters_lobby[] = {
    W1, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0, W0, W0,
    W0, W0, W1, W1, W1, W1, W0, W0, W0, W0, W0
};

static const uint8_t pattern_characters_toggle[] = {
    0x0f, 0xb6, 0x47, 0x08, 0x0f, 0x44, 0xd3, 0x03, 0xd0, 0x8d, 0x04, 0x52,
    0x8d, 0x0c, 0xc5, 0x50, W0, W0, W0, 0xe8, W0, W0, W0, W0,
    0x8b, 0x35, W0, W0, W0, W0
};
static const uint8_t mask_characters_toggle[] = {
    W0, W0, W0, W0, W0, W0, W0, W0, W0, W0, W0, W0,
    W0, W0, W0, W0, W1, W1, W1, W1, W1, W1, W1, W1,
    W0, W0, W1, W1, W1, W1
};

static const uint8_t pattern_characters_tainted[] = {
    0x80, 0x7e, 0x09, 0x00, 0x75, 0x09, 0xe8, W0, W0, W0, W0,
    0x84, 0xc0, 0x74, 0x41, 0x6a, 0x01, 0x68, 0xe9, 0x03, 0x00, 0x00
};
static const uint8_t mask_characters_tainted[] = {
    W0, W0, W0, W0, W0, W0, W1, W1, W1, W1, W1,
    W0, W0, W0, W0, W0, W0, W0, W0, W0, W0, W0
};

static const uint8_t pattern_friends_dialog_push[] = {
    0x68, W0, W0, W0, W0, 0x50, 0x8d, 0x8d, 0xc4, 0xfe, 0xff, 0xff, 0xe8
};
static const uint8_t mask_friends_dialog_push[] = {
    W0, W1, W1, W1, W1, W0, W0, W0, W0, W0, W0, W0, W1
};

struct SiteEntry {
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t length;
    uint32_t resolved;
};

#define ENTRY(pattern, mask) { pattern, mask, sizeof(pattern), 0 }

_Static_assert(sizeof(pattern_recovery_complete) == sizeof(mask_recovery_complete), "lobby pattern/mask length");
_Static_assert(sizeof(pattern_recovery_rebuild) == sizeof(mask_recovery_rebuild), "rebuild pattern/mask length");
_Static_assert(sizeof(pattern_recovery_timeout) == sizeof(mask_recovery_timeout), "timeout pattern/mask length");
_Static_assert(sizeof(pattern_goodtrip_pack) == sizeof(mask_goodtrip_pack), "pack pattern/mask length");
_Static_assert(sizeof(pattern_goodtrip_unpack) == sizeof(mask_goodtrip_unpack), "unpack pattern/mask length");
_Static_assert(sizeof(pattern_goodtrip_script) == sizeof(mask_goodtrip_script), "script pattern/mask length");
_Static_assert(sizeof(pattern_modsync_start1) == sizeof(mask_modsync_start1), "start1 pattern/mask length");
_Static_assert(sizeof(pattern_modsync_start2) == sizeof(mask_modsync_start2), "start2 pattern/mask length");
_Static_assert(sizeof(pattern_modsync_send_input) == sizeof(mask_modsync_send_input), "input pattern/mask length");
_Static_assert(sizeof(pattern_characters_lobby) == sizeof(mask_characters_lobby), "char-lobby pattern/mask length");
_Static_assert(sizeof(pattern_characters_toggle) == sizeof(mask_characters_toggle), "char-toggle pattern/mask length");
_Static_assert(sizeof(pattern_characters_tainted) == sizeof(mask_characters_tainted), "char-tainted pattern/mask length");
_Static_assert(sizeof(pattern_friends_dialog_push) == sizeof(mask_friends_dialog_push), "push pattern/mask length");

static struct SiteEntry sites[BM_SITE_COUNT] = {
    ENTRY(pattern_recovery_complete, mask_recovery_complete),
    ENTRY(pattern_recovery_rebuild, mask_recovery_rebuild),
    ENTRY(pattern_recovery_timeout, mask_recovery_timeout),
    ENTRY(pattern_goodtrip_pack, mask_goodtrip_pack),
    ENTRY(pattern_goodtrip_unpack, mask_goodtrip_unpack),
    ENTRY(pattern_goodtrip_script, mask_goodtrip_script),
    ENTRY(pattern_modsync_start1, mask_modsync_start1),
    ENTRY(pattern_modsync_start2, mask_modsync_start2),
    ENTRY(pattern_modsync_send_input, mask_modsync_send_input),
    ENTRY(pattern_characters_lobby, mask_characters_lobby),
    ENTRY(pattern_characters_toggle, mask_characters_toggle),
    ENTRY(pattern_characters_tainted, mask_characters_tainted),
    ENTRY(pattern_friends_dialog_push, mask_friends_dialog_push)
};

static uint32_t scan_section(const uint8_t *base, size_t size,
                             const uint8_t *bytes, const uint8_t *mask, size_t length)
{
    uint32_t hit = 0;
    bool found = false;
    if (size < length)
        return 0;
    for (size_t i = 0; i + length <= size; ++i) {
        bool match = true;
        for (size_t j = 0; j < length; ++j) {
            if (mask[j])
                continue;
            if (base[i + j] != bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            if (found)
                return 0; /* ambiguous: refuse to guess */
            found = true;
            hit = (uint32_t)i;
        }
    }
    return found ? hit : 0;
}

static const uint8_t *text_base;
static uint32_t text_va;
static size_t text_size;

static bool find_text_section(void)
{
    const uint8_t *image = (const uint8_t *)GetModuleHandleW(NULL);
    if (!image)
        return false;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(pe);
    for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i) {
        if (memcmp(section[i].Name, ".text", sizeof(".text") - 1) == 0) {
            text_base = image + section[i].VirtualAddress;
            text_size = section[i].Misc.VirtualSize;
            text_va = section[i].VirtualAddress;
            return true;
        }
    }
    return false;
}

static bool attempted, ready;
static int failed_site = -1;

int bm_locate_failure(void)
{
    return failed_site;
}

bool bm_locate_all(void)
{
    if (attempted)
        return ready;
    attempted = true;
    ready = false;

    if (!find_text_section()) {
        failed_site = -2; /* section lookup failed */
        return false;
    }

    for (unsigned i = 0; i < BM_SITE_COUNT; ++i) {
        const uint32_t offset = scan_section(text_base, text_size,
                                             sites[i].bytes, sites[i].mask, sites[i].length);
        if (!offset) {
            failed_site = (int)i;
            return false;
        }
        /* Store the RVA (section offset plus the .text virtual address)
         * so callers can use the value directly as an image offset. */
        sites[i].resolved = offset + text_va;
    }
    ready = true;
    return true;
}

uint32_t bm_locate_site(enum bm_site site)
{
    if ((unsigned)site >= BM_SITE_COUNT)
        return 0;
    return sites[site].resolved;
}
