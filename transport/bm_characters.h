#ifndef BETTERMULTIPLAYER_CHARACTERS_H
#define BETTERMULTIPLAYER_CHARACTERS_H

#include <stdbool.h>
#include <stddef.h>

bool bm_characters_install(void);
bool bm_characters_prepare(void);
bool bm_characters_roster(char *text, size_t capacity);

#endif
