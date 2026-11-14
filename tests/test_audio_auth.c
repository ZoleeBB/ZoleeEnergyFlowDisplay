/* Az elkészült hitelesítő tényleges RSA/AES visszafejtése tesztkulccsal. */
#include "app_loxone_audio_auth.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"

static int test_random(void *context, unsigned char *output, size_t size)
{
    (void)context;
    /* Csak teszt-RNG: a korábbi 255 bájtos ciklus az RSA blinding
     * tartomány-mintavételében újra és újra ugyanazt a hibás jelöltet adta.
     * A firmware továbbra is az esp_fill_random hardveres forrását kapja. */
    static uint32_t state = 0x6d2b79f5U;
    for (size_t i = 0; i < size; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        output[i] = (unsigned char)state;
    }
    return 0;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && length < 8192);
    rewind(file);
    char *text = calloc(1, (size_t)length + 1);
    assert(text && fread(text, 1, length, file) == (size_t)length);
    fclose(file);
    return text;
}

static void decode_uri(char *text)
{
    char *to = text;
    for (const char *from = text; *from; ++from)
    {
        if (*from == '%')
        {
            unsigned value;
            assert(from[1] && from[2] && sscanf(from + 1, "%2x", &value) == 1);
            *to++ = (char)value;
            from += 2;
        }
        else
            *to++ = *from;
    }
    *to = 0;
}

static size_t auth_roundtrip(mbedtls_pk_context *key, const char *public_key,
                            const char *username, size_t jwt_length, const char *session)
{
    char *jwt = calloc(jwt_length + 1, 1);
    assert(jwt);
    for (size_t i = 0; i < jwt_length; ++i)
        jwt[i] = (char)('A' + i % 26);
    char *command = lx_audio_auth_create(public_key, username, jwt, session, test_random, NULL);
    assert(command);
    size_t command_length = strlen(command);
    assert(command_length <= LX_AUDIO_AUTH_COMMAND_MAX);
    assert(command_length < LX_AUDIO_AUTH_WS_BUFFER_SIZE);
    /* Az AppHub AuthenticateSchema első paramétere nyers név. Csak a másik
     * két paraméter encodeURIComponent(Base64(...)); a saját dekóderrel
     * végzett körpróba ezt az átviteli eltérést önmagában nem mutatná ki. */
    const char *prefix = "secure/authenticate/";
    assert(!strncmp(command, prefix, strlen(prefix)));
    const char *user_text = command + strlen(prefix);
    assert(!strncmp(user_text, username, strlen(username)));
    assert(user_text[strlen(username)] == '/');
    const char *rsa_start = user_text + strlen(username) + 1;
    const char *separator = strchr(rsa_start, '/');
    assert(separator);
    char *rsa_text = calloc((size_t)(separator - rsa_start) + 1, 1);
    char *aes_text = strdup(separator + 1);
    assert(rsa_text);
    assert(aes_text);
    memcpy(rsa_text, rsa_start, (size_t)(separator - rsa_start));
    decode_uri(rsa_text);
    decode_uri(aes_text);
    unsigned char rsa[LX_AUDIO_AUTH_RSA_BYTES_MAX];
    unsigned char cipher[LX_AUDIO_AUTH_AES_PADDED_MAX], plain[LX_AUDIO_AUTH_AES_PADDED_MAX];
    size_t rsa_size, cipher_size, plain_size;
    assert(!mbedtls_base64_decode(rsa, sizeof(rsa), &rsa_size, (const unsigned char *)rsa_text, strlen(rsa_text)));
    assert(!mbedtls_base64_decode(cipher, sizeof(cipher), &cipher_size, (const unsigned char *)aes_text, strlen(aes_text)));
    assert(rsa_size == mbedtls_pk_get_len(key));
    assert(cipher_size == (jwt_length / 16 + 1) * 16);
    int decrypt_result = mbedtls_pk_decrypt(key, rsa, rsa_size, plain, &plain_size,
                                           sizeof(plain), test_random, NULL);
    if (decrypt_result)
        fprintf(stderr, "Audio RSA decrypt: -0x%04X (mbedTLS %s)\n",
            (unsigned)-decrypt_result, MBEDTLS_VERSION_STRING);
    assert(decrypt_result == 0);
    plain[plain_size] = 0;
    assert(plain_size == 64 + 1 + 32 + 1 + strlen(session));
    assert(!strcmp((char *)plain + 98, session));
    unsigned char secret[32], iv[16];
    for (unsigned i = 0; i < 32; ++i)
    {
        unsigned value;
        assert(sscanf((char *)plain + i * 2, "%2x", &value) == 1);
        secret[i] = (unsigned char)value;
    }
    for (unsigned i = 0; i < 16; ++i)
    {
        unsigned value;
        assert(sscanf((char *)plain + 65 + i * 2, "%2x", &value) == 1);
        iv[i] = (unsigned char)value;
    }
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    assert(!mbedtls_aes_setkey_dec(&aes, secret, 256));
    assert(!mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher_size, iv, cipher, plain));
    assert(!memcmp(plain, jwt, strlen(jwt)));
    for (size_t i = strlen(jwt); i < cipher_size; ++i)
        assert(plain[i] == cipher_size - strlen(jwt));
    mbedtls_aes_free(&aes);
    free(rsa_text);
    free(aes_text);
    lx_audio_auth_free(command);
    free(jwt);
    return command_length;
}

static void rejected_inputs(const char *public_key)
{
    const char *jwt = "test.jwt.payload.signature", *session = "test-session-42";
    assert(!lx_audio_auth_create("bad key", "user", jwt, session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "", jwt, session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "user", "", session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "user", jwt, "", test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "user/name", jwt, session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "user\r\n", jwt, session, test_random, NULL));
    char control_name[] = {'u', 1, 'r', 0};
    for (unsigned c = 1; c <= 0x7f; ++c)
    {
        if (c >= 0x20 && c != 0x7f)
            continue;
        control_name[1] = (char)c;
        assert(!lx_audio_auth_create(public_key, control_name, jwt, session, test_random, NULL));
    }
    char *overlong = calloc(LX_AUDIO_AUTH_JWT_MAX + 2, 1);
    assert(overlong);
    memset(overlong, 'x', LX_AUDIO_AUTH_USERNAME_MAX + 1);
    assert(!lx_audio_auth_create(public_key, overlong, jwt, session, test_random, NULL));
    memset(overlong, 'x', LX_AUDIO_AUTH_SESSION_MAX + 1);
    assert(!lx_audio_auth_create(public_key, "user", jwt, overlong, test_random, NULL));
    memset(overlong, 'x', LX_AUDIO_AUTH_JWT_MAX + 1);
    assert(!lx_audio_auth_create(public_key, "user", overlong, session, test_random, NULL));
    free(overlong);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    char *private_key = read_file(argv[1]), *public_key = read_file(argv[2]);
    /* Valódi Audioserver egy sorba lapított PEM-válasza is működjön. */
    char *destination = public_key;
    for (char *source = public_key; *source; ++source)
        if (*source != '\n' && *source != '\r')
            *destination++ = *source;
    *destination = 0;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
#if MBEDTLS_VERSION_MAJOR >= 3
    assert(!mbedtls_pk_parse_key(&key, (const unsigned char *)private_key, strlen(private_key) + 1,
                                NULL, 0, test_random, NULL));
#else
    assert(!mbedtls_pk_parse_key(&key, (const unsigned char *)private_key, strlen(private_key) + 1, NULL, 0));
#endif
    size_t bits = mbedtls_pk_get_bitlen(&key);
    assert(bits == 2048 || bits == 4096);
    const size_t jwt_lengths[] = {1, 15, 16, 17, 1023};
    const char *session = "test-session-42";
    for (size_t i = 0; i < sizeof(jwt_lengths) / sizeof(jwt_lengths[0]); ++i)
        auth_roundtrip(&key, public_key, "ZoleeEnergyDisplay", jwt_lengths[i], session);
    const char *names[] = {"user@test", "Árvíztűrő tükör", "név%20érték"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        auth_roundtrip(&key, public_key, names[i], 17, session);

    /* A leghosszabb név/JWT együtt is egy keretbe fér. A 2048 bites RSA
     * borítékába legfeljebb 147 bájtos session fér, a 4096 bitesnél a
     * saját 256 bájtos korlátunk érvényesül. Mindkettőt visszafejtjük. */
    char maximum_name[LX_AUDIO_AUTH_USERNAME_MAX + 1];
    memset(maximum_name, '%', sizeof(maximum_name) - 1);
    maximum_name[sizeof(maximum_name) - 1] = 0;
    char maximum_session[LX_AUDIO_AUTH_SESSION_MAX + 2];
    size_t session_length = mbedtls_pk_get_len(&key) - 11 - 98;
    if (session_length > LX_AUDIO_AUTH_SESSION_MAX)
        session_length = LX_AUDIO_AUTH_SESSION_MAX;
    memset(maximum_session, 's', session_length);
    maximum_session[session_length] = 0;
    size_t maximum_command = auth_roundtrip(&key, public_key, maximum_name,
        LX_AUDIO_AUTH_JWT_MAX, maximum_session);
    if (bits == 4096)
        assert(maximum_command > 2048);
    maximum_session[session_length] = 's';
    maximum_session[session_length + 1] = 0;
    assert(!lx_audio_auth_create(public_key, "user", "token", maximum_session, test_random, NULL));
    rejected_inputs(public_key);
    mbedtls_pk_free(&key);
    free(public_key);
    free(private_key);
    printf("PASS: Audio RSA%zu, JWT 1/15/16/17/1023, nyers nevek és elutasítások; max-input parancs %zu B, korlát %u B\n",
        bits, maximum_command, (unsigned)LX_AUDIO_AUTH_COMMAND_MAX);
    return 0;
}
