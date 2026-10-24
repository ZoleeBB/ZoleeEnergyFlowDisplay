#ifndef APP_WEB_UPLOAD_INCLUDED
#define APP_WEB_UPLOAD_INCLUDED
#include "app_storage.h"
#include "esp_http_server.h"
#include <stdio.h>
#include <errno.h>

/* A témák és hangok azonos SD-fogadóútja. A hálózati hiba nem csomaghiba;
 * rövid socket-időtúllépést ismétlünk, minden hibánál lezárjuk az állományt/zárat. */
static bool receive_sd_zip(httpd_req_t *req, const char *path, char error[160])
{
    if (!app_storage_is_ok() || app_storage_lock() != ESP_OK) {
        snprintf(error, 160, "%s", "Az SD-kártya nem érhető el.");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        snprintf(error, 160, "A feltöltési fájl nem nyitható meg (SD, errno=%d).", errno);
        app_storage_unlock();
        return false;
    }
    char buffer[2048];
    size_t left = req->content_len;
    unsigned timeouts = 0;
    bool ok = true;
    while (left) {
        int count = httpd_req_recv(req, buffer, left > sizeof(buffer) ? sizeof(buffer) : left);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3)
            continue;
        if (count <= 0) {
            snprintf(error, 160, "A ZIP fogadása megszakadt (%u/%u bájt, hálózati kód: %d).",
                (unsigned)(req->content_len - left), (unsigned)req->content_len, count);
            ok = false;
            break;
        }
        timeouts = 0;
        if (fwrite(buffer, 1, count, file) != (size_t)count) {
            snprintf(error, 160, "A ZIP SD-írása sikertelen (%u bájt után, errno=%d).",
                (unsigned)(req->content_len - left), errno);
            ok = false;
            break;
        }
        left -= (size_t)count;
    }
    if (fclose(file) && ok) {
        snprintf(error, 160, "A ZIP SD-mentése/lezárása sikertelen (errno=%d).", errno);
        ok = false;
    }
    app_storage_unlock();
    return ok;
}
#endif
