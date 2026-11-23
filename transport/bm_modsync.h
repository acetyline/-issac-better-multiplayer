#ifndef BETTERMULTIPLAYER_MODSYNC_H
#define BETTERMULTIPLAYER_MODSYNC_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

enum { BM_MODSYNC_MAX_PACKET = 48 * 1024 + 8 };
typedef void (*BmModSyncSend)(uint64_t peer, const void *data, uint32_t size);
void bm_modsync_init(HMODULE steam, bool (*start_ready)(void), BmModSyncSend send, bool hooks_ready);
void bm_modsync_tick(uint64_t room, uint64_t local, uint64_t owner);
bool bm_modsync_wants_messages(void);
void bm_modsync_receive(uint64_t peer, const void *data, uint32_t size);
bool bm_modsync_local_ready(void);
uint64_t bm_modsync_rejoin(uint64_t room, uint64_t *host);

#endif
