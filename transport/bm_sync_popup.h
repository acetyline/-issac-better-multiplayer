#ifndef BETTERMULTIPLAYER_SYNC_POPUP_H
#define BETTERMULTIPLAYER_SYNC_POPUP_H

#include <stdbool.h>

typedef enum {
    BM_SYNC_POPUP_IDLE,
    BM_SYNC_POPUP_WAITING,
    BM_SYNC_POPUP_CONTINUE,
    BM_SYNC_POPUP_CANCEL
} BmSyncPopupChoice;

bool bm_sync_popup_init(void);
bool bm_sync_popup_show(const char *title, const char *const *lines, unsigned count);
BmSyncPopupChoice bm_sync_popup_choice(void);
void bm_sync_popup_dismiss(void);

#endif
