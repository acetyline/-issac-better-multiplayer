#ifndef BETTERMULTIPLAYER_ROOM_RECOVERY_H
#define BETTERMULTIPLAYER_ROOM_RECOVERY_H

#include <stdbool.h>

bool bm_room_item_copy_install(void);
bool bm_pickup_anim_install(void);
bool bm_room_recovery_install(bool (*host_recovering)(void), void (*report)(const char *));

#endif
