#ifndef BETTERMULTIPLAYER_GOODTRIP_H
#define BETTERMULTIPLAYER_GOODTRIP_H

#include <stdbool.h>
#include <stdint.h>

bool bm_goodtrip_install(int (*room_role)(void), uint64_t (*room_owner)(void), void (*reporter)(const char *));
void bm_goodtrip_reset(void);

#endif
