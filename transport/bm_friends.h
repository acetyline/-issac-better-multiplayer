#ifndef BETTERMULTIPLAYER_FRIENDS_H
#define BETTERMULTIPLAYER_FRIENDS_H

#include <stdbool.h>
#include <stdint.h>

bool bm_friends_only(void);
void *bm_verify_matchmaking(void *original);
bool bm_accept_friends_lobby(uint64_t lobby, uint64_t local);
bool bm_friends_room_marked(uint64_t lobby, uint64_t owner);
bool bm_friends_member_marked(uint64_t lobby, uint64_t peer);
bool bm_friends_lobby_enrolled(uint64_t lobby, uint64_t owner);
void bm_forget_friends_lobby(uint64_t lobby);
uint64_t bm_friends_lobby_owner(uint64_t lobby);
bool bm_friends_recovery_room(uint64_t lobby, uint64_t owner);
uint64_t bm_friends_retry_join(uint64_t *owner);
void bm_friends_join_failed(uint64_t lobby);
bool bm_friends_install_cancel(void);
bool bm_friends_discard_cancelled_join(uint64_t lobby);
bool bm_friends_creating(uint64_t request);
void bm_friends_create_failed(uint64_t request);
void bm_friends_set_reporter(void (*reporter)(const char *));
bool bm_friends_mark_synced(uint64_t lobby);
bool bm_friends_synced(uint64_t lobby, uint64_t member);

#endif
