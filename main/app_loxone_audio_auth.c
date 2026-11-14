/* Modern Audioserver: RSA-PKCS1-v1.5 kulcsboríték + AES-256-CBC JWT.
 * A protokoll leírása és kompatibilitási határa: docs/audio-controller.md. */
#include "app_loxone_audio_auth.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

_Static_assert(LX_AUDIO_AUTH_COMMAND_MAX + 1 <= LX_AUDIO_AUTH_WS_BUFFER_SIZE,
               "Az Audio hitelesito teljes parancsa ferjen egyetlen WS-keretbe");

static void *auth_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}

void lx_audio_auth_free(char *command)
{
    if (command)
    {
        mbedtls_platform_zeroize(command, strlen(command));
        free(command);
    }
}

static void auth_hex(const unsigned char *value, size_t size, char *out)
{
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i)
    {
        out[2 * i] = digits[value[i] >> 4];
        out[2 * i + 1] = digits[value[i] & 15];
    }
    out[2 * size] = 0;
}

static bool auth_username_valid(const char *username)
{
    if (!username || !username[0] || strlen(username) > LX_AUDIO_AUTH_USERNAME_MAX)
        return false;
    /* A név nem URI-komponens: az App nyersen illeszti a parancsba. Az útvonal
     * elválasztója és az ASCII vezérlőbájtok viszont nem juthatnak a hálózatra. */
    for (const unsigned char *p = (const unsigned char *)username; *p; ++p)
        if (*p == '/' || *p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

static char *auth_escape(const unsigned char *input, size_t length)
{
    size_t encoded_size = ((length + 2) / 3) * 4;
    unsigned char *encoded = auth_alloc(encoded_size + 1);
    if (!encoded)
        return NULL;
    bool ok = mbedtls_base64_encode(encoded, encoded_size + 1, &encoded_size, input, length) == 0;
    char *escaped = ok ? auth_alloc(encoded_size * 3 + 1) : NULL;
    if (escaped)
    {
        char *next = escaped;
        for (size_t i = 0; i < encoded_size; ++i)
        {
            unsigned char c = encoded[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || strchr("-_.~", c))
                *next++ = (char)c;
            else
            {
                snprintf(next, 4, "%%%02X", c);
                next += 3;
            }
        }
        *next = 0;
    }
    mbedtls_platform_zeroize(encoded, encoded_size);
    free(encoded);
    return escaped;
}

char *lx_audio_auth_create(const char *public_key, const char *username,
                           const char *jwt, const char *session,
                           int (*random_bytes)(void *, unsigned char *, size_t), void *random_context)
{
    if (!public_key || !auth_username_valid(username) ||
        !jwt || !jwt[0] || strlen(jwt) > LX_AUDIO_AUTH_JWT_MAX ||
        !session || !session[0] || strlen(session) > LX_AUDIO_AUTH_SESSION_MAX || !random_bytes)
        return NULL;
    /* Egy sorba lapított PEM-et is fogadunk, de kizárólag nyilvános RSA kulcsot. */
    const char *begin = strstr(public_key, "-----BEGIN PUBLIC KEY-----");
    const char *end = strstr(public_key, "-----END PUBLIC KEY-----");
    if (!begin || !end || end < begin || end - begin > 2048)
        return NULL;
    begin += strlen("-----BEGIN PUBLIC KEY-----");
    char *pem = auth_alloc((size_t)(end - begin) + 64);
    if (!pem)
        return NULL;
    snprintf(pem, (size_t)(end - begin) + 64, "-----BEGIN PUBLIC KEY-----\n%.*s\n-----END PUBLIC KEY-----\n",
             (int)(end - begin), begin);
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool ok = mbedtls_pk_parse_public_key(&key, (const unsigned char *)pem, strlen(pem) + 1) == 0 &&
              mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) &&
              mbedtls_pk_get_bitlen(&key) >= LX_AUDIO_AUTH_RSA_BITS_MIN &&
              mbedtls_pk_get_bitlen(&key) <= LX_AUDIO_AUTH_RSA_BITS_MAX;
    free(pem);
    unsigned char secret[32] = {0}, iv[16] = {0}, working_iv[16] = {0};
    char key_hex[65] = {0}, iv_hex[33] = {0};
    char *envelope = NULL, *rsa_part = NULL, *aes_part = NULL, *command = NULL;
    unsigned char *rsa = NULL, *padded = NULL, *cipher = NULL;
    size_t padded_size = (strlen(jwt) / LX_AUDIO_AUTH_AES_BLOCK_SIZE + 1) *
        LX_AUDIO_AUTH_AES_BLOCK_SIZE;
    size_t rsa_size = 0;
    if (ok)
        ok = random_bytes(random_context, secret, sizeof(secret)) == 0 &&
             random_bytes(random_context, iv, sizeof(iv)) == 0;
    if (ok)
    {
        auth_hex(secret, sizeof(secret), key_hex);
        auth_hex(iv, sizeof(iv), iv_hex);
        envelope = auth_alloc(strlen(session) + 100);
        rsa = auth_alloc(mbedtls_pk_get_len(&key));
        padded = auth_alloc(padded_size);
        cipher = auth_alloc(padded_size);
        ok = envelope && rsa && padded && cipher;
    }
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (ok)
    {
        snprintf(envelope, strlen(session) + 100, "%s:%s:%s", key_hex, iv_hex, session);
        ok = strlen(envelope) + 11 <= mbedtls_pk_get_len(&key) &&
             mbedtls_pk_encrypt(&key, (const unsigned char *)envelope, strlen(envelope), rsa,
                                &rsa_size, mbedtls_pk_get_len(&key), random_bytes, random_context) == 0;
        size_t length = strlen(jwt);
        memcpy(padded, jwt, length);
        memset(padded + length, (unsigned char)(padded_size - length), padded_size - length);
        memcpy(working_iv, iv, sizeof(iv));
        ok = ok && mbedtls_aes_setkey_enc(&aes, secret, 256) == 0 &&
             mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_size,
                                  working_iv, padded, cipher) == 0;
    }
    if (ok)
    {
        rsa_part = auth_escape(rsa, rsa_size);
        aes_part = auth_escape(cipher, padded_size);
        ok = rsa_part && aes_part;
    }
    if (ok)
    {
        size_t length = strlen(username) + strlen(rsa_part) + strlen(aes_part) +
            sizeof("secure/authenticate/") + 2;
        if (length <= LX_AUDIO_AUTH_COMMAND_MAX + 1)
            command = auth_alloc(length);
        if (command)
            snprintf(command, length, "secure/authenticate/%s/%s/%s", username, rsa_part, aes_part);
    }
    mbedtls_aes_free(&aes);
    mbedtls_pk_free(&key);
    mbedtls_platform_zeroize(secret, sizeof(secret));
    mbedtls_platform_zeroize(iv, sizeof(iv));
    mbedtls_platform_zeroize(working_iv, sizeof(working_iv));
    mbedtls_platform_zeroize(key_hex, sizeof(key_hex));
    mbedtls_platform_zeroize(iv_hex, sizeof(iv_hex));
    if (padded)
        mbedtls_platform_zeroize(padded, padded_size);
    if (cipher)
        mbedtls_platform_zeroize(cipher, padded_size);
    if (rsa)
        mbedtls_platform_zeroize(rsa, rsa_size);
    free(padded);
    free(cipher);
    free(rsa);
    lx_audio_auth_free(envelope);
    lx_audio_auth_free(rsa_part);
    lx_audio_auth_free(aes_part);
    return command;
}
