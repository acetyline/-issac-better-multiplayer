#ifndef BETTERMULTIPLAYER_RECOVERY_H
#define BETTERMULTIPLAYER_RECOVERY_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

bool bm_recovery_install(void (*begin)(bool rebuilding));
void bm_recovery_begin(uint64_t origin, uint64_t host, int members, uint64_t now,
                       bool rebuilding, void *friends);
bool bm_recovery_due(uint64_t now);
bool bm_recovery_room_host(uint64_t lobby, uint64_t owner, uint64_t local, uint64_t now);
uint64_t bm_recovery_poll(uint64_t local, uint64_t lobby, uint64_t owner, int members,
                          uint64_t now, void *friends, uint64_t *host);
void bm_recovery_initialize(HMODULE steam);
void bm_recovery_reset(void *friends);

#endif
