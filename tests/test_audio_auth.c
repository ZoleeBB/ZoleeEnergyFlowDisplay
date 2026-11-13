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
    const char *jwt = "test.jwt.payload.signature", *session = "test-session-42";
    char *command = lx_audio_auth_create(public_key, "user@test", jwt, session, test_random, NULL);
    assert(command && !strstr(command, jwt));
    const char *prefix = "secure/authenticate/user%40test/";
    assert(!strncmp(command, prefix, strlen(prefix)));
    char *rsa_text = command + strlen(prefix), *aes_text = strchr(rsa_text, '/');
    assert(aes_text);
    *aes_text++ = 0;
    decode_uri(rsa_text);
    decode_uri(aes_text);
    unsigned char rsa[512], cipher[1024], plain[1024];
    size_t rsa_size, cipher_size, plain_size;
    assert(!mbedtls_base64_decode(rsa, sizeof(rsa), &rsa_size, (const unsigned char *)rsa_text, strlen(rsa_text)));
    assert(!mbedtls_base64_decode(cipher, sizeof(cipher), &cipher_size, (const unsigned char *)aes_text, strlen(aes_text)));
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
#if MBEDTLS_VERSION_MAJOR >= 3
    assert(!mbedtls_pk_parse_key(&key, (const unsigned char *)private_key, strlen(private_key) + 1,
                                NULL, 0, test_random, NULL));
#else
    assert(!mbedtls_pk_parse_key(&key, (const unsigned char *)private_key, strlen(private_key) + 1, NULL, 0));
#endif
    int decrypt_result = mbedtls_pk_decrypt(&key, rsa, rsa_size, plain, &plain_size,
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
    mbedtls_pk_free(&key);
    free(command);
    assert(!lx_audio_auth_create("bad key", "user", jwt, session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "", jwt, session, test_random, NULL));
    assert(!lx_audio_auth_create(public_key, "user", "", session, test_random, NULL));
    free(public_key);
    free(private_key);
    puts("PASS: Audioserver hitelesítő RSA-visszafejtése és AES/PKCS7 ellenőrzése");
    return 0;
}
