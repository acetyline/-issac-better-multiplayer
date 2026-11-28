#ifndef BM_LOCATE_H
#define BM_LOCATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Pattern-based code site location for the J511 game image.
 *
 * Hard-coded RVAs break when a code-modifying patch (for example the
 * community Chinese localization that injects a loader and shifts the
 * whole .text section) is applied to the game executable. The locator
 * below scans the mapped .text section of the running image for unique
 * byte patterns that identify each hook site independently of its
 * final address, so the hooks keep working on both the stock J511
 * binary and patched variants of the same build.
 */

/* Locates every pattern in one pass. Returns true when all sites were
 * found exactly once; the resolved RVAs are stored in bm_locate_sites().
 * Must be called once from DllMain (or the DLL entry) before any
 * bm_*_install() function runs. */
bool bm_locate_all(void);

/* Resolved RVAs. Valid after bm_locate_all() returns true.
 * Index constants live in bm_locate.h as well. */
enum bm_site {
    BM_SITE_RECOVERY_COMPLETE = 0,
    BM_SITE_RECOVERY_REBUILD,
    BM_SITE_RECOVERY_TIMEOUT,
    BM_SITE_GOODTRIP_PACK,
    BM_SITE_GOODTRIP_UNPACK,
    BM_SITE_GOODTRIP_SCRIPT,
    BM_SITE_MODSYNC_START1,
    BM_SITE_MODSYNC_START2,
    BM_SITE_MODSYNC_SEND_INPUT,
    BM_SITE_CHARACTERS_LOBBY,
    BM_SITE_CHARACTERS_TOGGLE,
    BM_SITE_CHARACTERS_TAINTED_BASE, /* first of four consecutive sites */
    BM_SITE_FRIENDS_DIALOG_PUSH,
    BM_SITE_COUNT
};

uint32_t bm_locate_site(enum bm_site site);

#endif
