#ifndef BM_GAME_BUILD_H
#define BM_GAME_BUILD_H

#include <stdbool.h>

extern bool bm_game_j514;

#define BM_GAME_RVA(j511, j514) (bm_game_j514 ? (j514) : (j511))

#endif
