#ifndef APP_WEB_UPLOAD_INCLUDED
#define APP_WEB_UPLOAD_INCLUDED
#include "app_storage.h"
#include "esp_http_server.h"
#include <stdio.h>
#include <errno.h>

/* Csak a hangcsomagé. Minden írás lezárul a zár elengedése előtt:
 * hálózati várakozáskor nincs nyitott SD-fájl és nincs fogva az SD-zár. */
static bool append_ringtone_zip(const char *path, const char *mode,
                                const char *data, size_t size, char error[160])
{
    if (app_storage_lock() != ESP_OK) {
        snprintf(error, 160, "%s", "Az SD-zár nem érhető el.");
        return false;
    }
    FILE *file = app_storage_is_ok() ? fopen(path, mode) : NULL;
    bool ok = file != NULL;
    if (file) {
        ok = fwrite(data, 1, size, file) == size;
        if (fclose(file))
            ok = false;
    }
    if (!ok)
        snprintf(error, 160, "A ZIP SD-írása/lezárása sikertelen (errno=%d).", errno);
    app_storage_unlock();
    return ok;
}

static bool receive_ringtone_zip(httpd_req_t *req, const char *path, char error[160])
{
    if (!append_ringtone_zip(path, "wb", "", 0, error))
        return false;
    char buffer[2048];
    size_t left = req->content_len;
    unsigned timeouts = 0;
    while (left) {
        int count = httpd_req_recv(req, buffer, left > sizeof(buffer) ? sizeof(buffer) : left);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3)
            continue;
        if (count <= 0) {
            snprintf(error, 160, "A ZIP fogadása megszakadt (%u/%u bájt, hálózati kód: %d). A régi hangcsomag már törölve lett.",
                (unsigned)(req->content_len - left), (unsigned)req->content_len, count);
            return false;
        }
        timeouts = 0;
        if (!append_ringtone_zip(path, "ab", buffer, (size_t)count, error))
            return false;
        left -= (size_t)count;
    }
    return true;
}
#endif
