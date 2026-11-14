#pragma once

#include <stddef.h>

/* A hitelesítő és a WS-küldő közös bájtkorlátai. A Base64 minden karakterére
 * három URI-bájtot számolunk: ez konzervatív felső becslés, nem átlagméret.
 * Az AES a pontos blokkhatáron is teljes PKCS7-blokkot fűz a JWT után. */
enum
{
    LX_AUDIO_AUTH_USERNAME_MAX = 95,
    LX_AUDIO_AUTH_JWT_MAX = 1023,
    LX_AUDIO_AUTH_SESSION_MAX = 256,
    LX_AUDIO_AUTH_RSA_BITS_MIN = 2048,
    LX_AUDIO_AUTH_RSA_BITS_MAX = 4096,
    LX_AUDIO_AUTH_RSA_BYTES_MAX = LX_AUDIO_AUTH_RSA_BITS_MAX / 8,
    LX_AUDIO_AUTH_AES_BLOCK_SIZE = 16,
    LX_AUDIO_AUTH_AES_PADDED_MAX =
        (LX_AUDIO_AUTH_JWT_MAX / LX_AUDIO_AUTH_AES_BLOCK_SIZE + 1) * LX_AUDIO_AUTH_AES_BLOCK_SIZE,
    LX_AUDIO_AUTH_RSA_URI_MAX = 3 * ((LX_AUDIO_AUTH_RSA_BYTES_MAX + 2) / 3 * 4),
    LX_AUDIO_AUTH_AES_URI_MAX = 3 * ((LX_AUDIO_AUTH_AES_PADDED_MAX + 2) / 3 * 4),
    LX_AUDIO_AUTH_COMMAND_MAX = sizeof("secure/authenticate/") - 1 + 2 +
        LX_AUDIO_AUTH_USERNAME_MAX + LX_AUDIO_AUTH_RSA_URI_MAX + LX_AUDIO_AUTH_AES_URI_MAX,
    LX_AUDIO_AUTH_WS_BUFFER_SIZE = 8192
};

/* Egyetlen, hívó által felszabadítandó WS-parancs. A kész hitelesítő érzékeny:
 * küldés után a teljes sztringet nullázni kell, naplózni tilos. A gyártói
 * protokollban a név nyers UTF-8; elválasztó és vezérlőkarakter nem lehet benne. */
char *lx_audio_auth_create(const char *public_key, const char *username,
                           const char *jwt, const char *session,
                           int (*random_bytes)(void *, unsigned char *, size_t), void *random_context);
void lx_audio_auth_free(char *command);
