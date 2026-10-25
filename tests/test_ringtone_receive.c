/* A valódi ZIP-fogadó hálózati várakozás és SD-hiba mellett sem tarthat zárat. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "app_storage.h"
static bool locked, available = true;
static int fail_after = -1, timeouts;
static size_t position;
static unsigned char payload[5001];
esp_err_t app_storage_lock(void)
{
    assert(!locked);
    if (!available)
        return ESP_FAIL;
    locked = true;
    return ESP_OK;
}
void app_storage_unlock(void) { assert(locked); locked = false; }
bool app_storage_is_ok(void) { return available; }
int httpd_req_recv(httpd_req_t *req, char *out, size_t size)
{
    /* Az SD-t a hálózati hívás alatt más feladat is szabadon elérheti. */
    assert(!locked);
    if (timeouts > 0) { --timeouts; return HTTPD_SOCK_ERR_TIMEOUT; }
    if (fail_after >= 0 && position >= (size_t)fail_after)
        return -1;
    if (size > req->content_len - position)
        size = req->content_len - position;
    if (size > 317)
        size = 317;
    memcpy(out, payload + position, size);
    position += size;
    return (int)size;
}
#include "../main/app_web_upload.inc"
int main(int argc, char **argv)
{
    assert(argc == 2);
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (unsigned char)i;
    httpd_req_t req = {.content_len = sizeof(payload)};
    char error[160];
    timeouts = 3;
    assert(receive_ringtone_zip(&req, argv[1], error));
    assert(!locked);
    FILE *file = fopen(argv[1], "rb");
    unsigned char actual[sizeof(payload)];
    assert(file && fread(actual, 1, sizeof(actual), file) == sizeof(actual));
    assert(!memcmp(actual, payload, sizeof(actual)) && fgetc(file) == EOF);
    fclose(file);
    /* Azonos útvonal ismételt feltöltése nem fűzheti hozzá a régi ZIP-et. */
    position = 0;
    assert(receive_ringtone_zip(&req, argv[1], error));
    position = 0;
    fail_after = 317;
    assert(!receive_ringtone_zip(&req, argv[1], error));
    assert(!locked && strstr(error, "317/5001"));
    position = 0;
    fail_after = -1;
    timeouts = 4;
    assert(!receive_ringtone_zip(&req, argv[1], error) && !locked);
    available = false;
    assert(!receive_ringtone_zip(&req, argv[1], error) && !locked);
    available = true;
    assert(!receive_ringtone_zip(&req, "/missing-directory/upload.zip", error) && !locked);
    unlink(argv[1]);
    puts("PASS: ringtone receive exact bytes, replace, timeout/disconnect/SD failures; SD unlocked during every socket receive");
    return 0;
}
