#pragma once

#include <stddef.h>

/* Egyetlen, hívó által felszabadítandó WS-parancs. A kész hitelesítő érzékeny:
 * küldés után a teljes sztringet nullázni kell, naplózni tilos. */
char *lx_audio_auth_create(const char *public_key, const char *username,
                           const char *jwt, const char *session,
                           int (*random_bytes)(void *, unsigned char *, size_t), void *random_context);
void lx_audio_auth_free(char *command);
