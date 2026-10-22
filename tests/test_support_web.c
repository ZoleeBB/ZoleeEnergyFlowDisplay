/* A tényleges támogatási válasz darabolása, lokalizációja és socket-hibái. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_support_web.h"

static char response[16384];
static size_t used;
static int calls, fail_at, endings, direct_sends;

static void reset_response(void)
{
    response[0] = 0;
    used = 0;
    calls = endings = direct_sends = 0;
    fail_at = -1;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, int size)
{
    (void)req;
    assert(size >= 0);
    if (calls++ == fail_at) {
        return ESP_FAIL;
    }
    if (!data && size == 0) {
        endings++;
        return ESP_OK;
    }
    assert(data && size > 0 && endings == 0);
    assert(used + (size_t)size < sizeof(response));
    memcpy(response + used, data, (size_t)size);
    used += (size_t)size;
    response[used] = 0;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *data)
{
    (void)req;
    direct_sends++;
    snprintf(response, sizeof(response), "%s", data);
    return ESP_OK;
}

int main(void)
{
    const char page[] = "<html><body><!--APP_SUPPORT--><form>Settings</form></body></html>";
    const char *cards[] = {app_support_card_en, app_support_card_hu};
    for (int hu = 0; hu < 2; hu++) {
        reset_response();
        assert(app_web_send_page(NULL, page, hu) == ESP_OK);
        assert(strstr(response, "<form>Settings</form>"));
        assert(strstr(response, "</body></html>"));
#if SUPPORT_ENABLED
        assert(endings == 1 && calls == 4 && direct_sends == 0);
        assert(strstr(response, cards[hu]));
        assert(strstr(response, cards[hu]) < strstr(response, "<form>"));
        assert(!strstr(response, "<!--APP_SUPPORT-->"));
        assert(strstr(response, hu ? "A fejlesztés támogatása" : "Support development"));
        assert(strstr(response, "https://revolut.me/supporttest"));
        assert(strstr(response, "https://ko-fi.com/supporttest"));
        for (int failure = 0; failure < 4; failure++) {
            reset_response();
            fail_at = failure;
            assert(app_web_send_page(NULL, page, hu) == ESP_FAIL);
            assert(calls == failure + 1 && endings == 0);
        }
        reset_response();
        assert(app_web_send_page(NULL, "<!--APP_SUPPORT-->", hu) == ESP_OK);
        assert(!strcmp(response, cards[hu]) && endings == 1 && calls == 2);
#else
        assert(!cards[hu][0]);
        assert(!strcmp(response, page));
        assert(direct_sends == 1 && calls == 0 && endings == 0);
#endif
        reset_response();
        assert(app_web_send_page(NULL, "<html>Unmarked</html>", hu) == ESP_OK);
        assert(!strcmp(response, "<html>Unmarked</html>") && direct_sends == 1);
        reset_response();
        assert(app_web_send_support(NULL, hu) == ESP_OK);
        assert(!strcmp(response, cards[hu]) && endings == 0);
    }
    puts("PASS: support HU/EN, exact page preservation, single termination, send errors and empty links");
    return 0;
}
