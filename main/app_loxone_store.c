/* Külön, tranzakciós SD-konfiguráció. Titkos jelszó nem kerül a webválaszba. */
#include "app_loxone_store.h"
#include "app_storage.h"
#include "esp_random.h"
#include "nvs.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LX_PATH APP_SD_MOUNT_POINT "/loxone.json"
#define LX_TMP LX_PATH ".tmp"
#define LX_BACKUP LX_PATH ".bak"
#define LX_STORE_MAX 16384
static void hex(const unsigned char *data, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 15];
    }
    out[2 * n] = 0;
}
static bool unhex(const char *s, unsigned char *out, size_t n)
{
    if (!s || strlen(s) != n * 2)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        unsigned a, b;
        char x = s[i * 2], y = s[i * 2 + 1];
        if (x >= '0' && x <= '9')
            a = x - '0';
        else if (x >= 'a' && x <= 'f')
            a = x - 'a' + 10;
        else
            return false;
        if (y >= '0' && y <= '9')
            b = y - '0';
        else if (y >= 'a' && y <= 'f')
            b = y - 'a' + 10;
        else
            return false;
        out[i] = (a << 4) | b;
    }
    return true;
}
static esp_err_t device_key(unsigned char key[32], bool create)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("loxone_secret", create ? NVS_READWRITE : NVS_READONLY, &h);
    if (e != ESP_OK)
        return e;
    size_t n = 32;
    e = nvs_get_blob(h, "key", key, &n);
    if (e == ESP_ERR_NVS_NOT_FOUND && create)
    {
        esp_fill_random(key, 32);
        e = nvs_set_blob(h, "key", key, 32);
        if (e == ESP_OK)
            e = nvs_commit(h);
    }
    else if (e == ESP_OK && n != 32)
        e = ESP_ERR_INVALID_SIZE;
    nvs_close(h);
    return e;
}
static const char *text(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
/* Felhasználó és Miniserver is hitelesített kiegészítő adat: nem cserélhető ki észrevétlenül. */
static size_t aad(const lx_config_t *cfg, char *out, size_t n)
{
    return snprintf(out,
                    n,
                    "loxone-v1|%s|%u|%u|%s",
                    cfg->host,
                    (unsigned)cfg->port,
                    (unsigned)cfg->tls,
                    cfg->username);
}
static bool crypt_password(lx_config_t *cfg, cJSON *root, bool encrypt)
{
    unsigned char key[32] = {0}, nonce[12] = {0}, tag[16] = {0}, cipher[LX_PASSWORD_MAX] = {0};
    char encoded[LX_PASSWORD_MAX * 2 + 1], nonce_hex[25], tag_hex[33], associated[256];
    bool ok = false;
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    if (device_key(key, encrypt) != ESP_OK)
        goto done;
    if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
        goto done;
    size_t a = aad(cfg, associated, sizeof(associated));
    if (a >= sizeof(associated))
        goto done;
    if (encrypt)
    {
        size_t n = strlen(cfg->password);
        if (!n || n >= LX_PASSWORD_MAX)
            goto done;
        esp_fill_random(nonce, sizeof(nonce));
        if (mbedtls_gcm_crypt_and_tag(&ctx,
                                      MBEDTLS_GCM_ENCRYPT,
                                      n,
                                      nonce,
                                      sizeof(nonce),
                                      (unsigned char *)associated,
                                      a,
                                      (unsigned char *)cfg->password,
                                      cipher,
                                      sizeof(tag),
                                      tag) != 0)
            goto done;
        hex(cipher, n, encoded);
        hex(nonce, sizeof(nonce), nonce_hex);
        hex(tag, sizeof(tag), tag_hex);
        cJSON *secret = cJSON_CreateObject();
        if (!secret)
            goto done;
        if (!cJSON_AddStringToObject(secret, "cipher", encoded) ||
            !cJSON_AddStringToObject(secret, "nonce", nonce_hex) ||
            !cJSON_AddStringToObject(secret, "tag", tag_hex) ||
            !cJSON_AddItemToObject(root, "secret", secret))
        {
            cJSON_Delete(secret);
            goto done;
        }
        ok = true;
    }
    else
    {
        const cJSON *secret = cJSON_GetObjectItemCaseSensitive(root, "secret");
        const char *c = text(secret, "cipher");
        if (!c || !strlen(c) || strlen(c) % 2 || strlen(c) >= LX_PASSWORD_MAX * 2)
            goto done;
        size_t n = strlen(c) / 2;
        if (!unhex(c, cipher, n) || !unhex(text(secret, "nonce"), nonce, sizeof(nonce)) ||
            !unhex(text(secret, "tag"), tag, sizeof(tag)))
            goto done;
        if (mbedtls_gcm_auth_decrypt(&ctx,
                                     n,
                                     nonce,
                                     sizeof(nonce),
                                     (unsigned char *)associated,
                                     a,
                                     tag,
                                     sizeof(tag),
                                     cipher,
                                     (unsigned char *)cfg->password) != 0)
            goto done;
        cfg->password[n] = 0;
        ok = true;
    }
done:
    if (!ok)
        mbedtls_platform_zeroize(cfg->password, sizeof(cfg->password));
    mbedtls_gcm_free(&ctx);
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(cipher, sizeof(cipher));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    return ok;
}
esp_err_t app_loxone_store_load(lx_config_t *out)
{
    if (app_storage_lock() != ESP_OK)
        return ESP_FAIL;
    if (access(LX_PATH, F_OK) != 0 && access(LX_BACKUP, F_OK) == 0)
        (void)rename(LX_BACKUP, LX_PATH);
    FILE *f = fopen(LX_PATH, "rb");
    char *body = NULL;
    esp_err_t e = ESP_FAIL;
    if (!f)
        goto done;
    if (fseek(f, 0, SEEK_END) != 0)
        goto done;
    long size = ftell(f);
    if (size < 1 || size > LX_STORE_MAX || fseek(f, 0, SEEK_SET) != 0)
        goto done;
    body = calloc(1, (size_t)size + 1);
    if (!body)
        goto done;
    if (fread(body, 1, size, f) != (size_t)size)
        goto done;
    cJSON *root = cJSON_Parse(body);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (version && version->valueint == 1 && lx_config_parse(root, out, false))
    {
        /* Másik készülék kártyáján a párosítások betöltődnek, a jelszó üres marad. */
        (void)crypt_password(out, root, false);
        e = ESP_OK;
    }
    cJSON_Delete(root);
done:
    if (f)
        fclose(f);
    free(body);
    app_storage_unlock();
    return e;
}
esp_err_t app_loxone_store_save(const lx_config_t *cfg)
{
    cJSON *root = lx_config_json(cfg);
    if (!root)
        return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(root, "version", 1);
    /* A kriptográfiai művelet nem változtatja a hívó konfigurációját. */
    lx_config_t *copy = malloc(sizeof(*copy));
    if (!copy)
    {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    *copy = *cfg;
    bool ok = crypt_password(copy, root, true);
    mbedtls_platform_zeroize(copy, sizeof(*copy));
    free(copy);
    char *body = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body)
        return ESP_FAIL;
    esp_err_t e = app_storage_lock();
    if (e != ESP_OK)
    {
        free(body);
        return e;
    }
    FILE *f = fopen(LX_TMP, "wb");
    ok = f != NULL;
    if (f)
    {
        size_t n = strlen(body);
        ok = fwrite(body, 1, n, f) == n;
        if (fflush(f) != 0 || fsync(fileno(f)) != 0)
            ok = false;
        if (fclose(f) != 0)
            ok = false;
    }
    if (ok)
    {
        /* FAT nem támogat létező célra minden esetben atomi rename-et. */
        bool exists = access(LX_PATH, F_OK) == 0;
        if (exists)
        {
            if (unlink(LX_BACKUP) != 0 && access(LX_BACKUP, F_OK) == 0)
                ok = false;
            if (ok && rename(LX_PATH, LX_BACKUP) != 0)
                ok = false;
        }
        if (ok && rename(LX_TMP, LX_PATH) != 0)
        {
            ok = false;
            if (exists)
                (void)rename(LX_BACKUP, LX_PATH);
        }
    }
    if (!ok)
        (void)unlink(LX_TMP);
    app_storage_unlock();
    free(body);
    return ok ? ESP_OK : ESP_FAIL;
}
