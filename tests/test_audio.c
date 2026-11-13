/* Valódi Audio-katalógus, állapot és parancsmodellek; hálózat nélkül. */
#include "app_loxone_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZONE "40000000-0000-0000-0000000000000001"
#define ACTION "40000000-0000-0000-00000000000000a1"

static cJSON *catalog(void)
{
    const char *json = "{\"mediaServer\":{\"server-one\":{\"type\":2,\"localIP\":\"192.168.1.40\",\"host\":\"ignored.example:7091\"}},"
        "\"controls\":{\"" ZONE "\":{\"type\":\"AudioZoneV2\",\"name\":\"Nappali\","
        "\"uuidAction\":\"" ACTION "\",\"isSecured\":false,"
        "\"details\":{\"server\":\"server-one\",\"playerid\":3,\"validOutputs\":true,\"volumeModes\":1},"
        "\"states\":{\"volume\":\"40000000-0000-0000-0000000000000011\","
        "\"isLocked\":\"40000000-0000-0000-0000000000000012\","
        "\"jLocked\":\"40000000-0000-0000-0000000000000013\"}}}}";
    return lx_catalog_parse(json, strlen(json));
}

static void test_catalog(void)
{
    cJSON *list = catalog();
    assert(list);
    const cJSON *entry = lx_audio_catalog_find(list, ZONE);
    assert(entry && !lx_audio_catalog_find(list, ACTION));
    lx_audio_endpoint_t endpoint;
    assert(lx_audio_endpoint(entry, &endpoint));
    assert(!endpoint.blocked && endpoint.player == 3 && endpoint.port == 7091);
    assert(!strcmp(endpoint.host, "192.168.1.40"));
    assert(!strcmp(endpoint.action, ACTION));
    char command[96];
    assert(lx_audio_command_build(entry, LX_AUDIO_PAUSE, 0, command));
    assert(!strcmp(command, "jdev/sps/io/" ACTION "/Pause"));
    assert(lx_audio_command_build(entry, LX_AUDIO_VOLUME, 37, command));
    assert(!strcmp(command, "jdev/sps/io/" ACTION "/volume/37"));
    assert(!lx_audio_command_build(entry, LX_AUDIO_VOLUME, 101, command));
    assert(!lx_audio_command_build(entry, LX_AUDIO_FAVORITE, 1, command));
    assert(!lx_audio_command_build(entry, (lx_audio_action_t)999, 0, command));
    assert(lx_audio_native_command(3, LX_AUDIO_FAVORITE, 9001, command));
    assert(!strcmp(command, "audio/3/roomfav/play/9001"));
    assert(!lx_audio_native_command(0, LX_AUDIO_FAVORITE, 3, command));
    assert(!lx_audio_native_command(3, LX_AUDIO_FAVORITE, 0, command));
    assert(!lx_audio_native_command(3, LX_AUDIO_VOLUME, 30, command));
    cJSON *meta = cJSON_Parse(cJSON_GetObjectItemCaseSensitive(entry, "audio")->valuestring);
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(meta, "states")));
    cJSON_ReplaceItemInObjectCaseSensitive(meta, "volumeModes", cJSON_CreateNumber(4));
    char *encoded = cJSON_PrintUnformatted(meta);
    cJSON_ReplaceItemInObjectCaseSensitive((cJSON *)entry, "audio", cJSON_CreateString(encoded));
    free(encoded);
    assert(!lx_audio_command_build(entry, LX_AUDIO_VOLUME, 30, command));
    assert(lx_audio_command_build(entry, LX_AUDIO_VOLUME_UP, 0, command));
    assert(strstr(command, "/volUp"));
    cJSON_ReplaceItemInObjectCaseSensitive((cJSON *)entry, "secured", cJSON_CreateBool(true));
    assert(!lx_audio_command_build(entry, LX_AUDIO_PLAY, 0, command));
    cJSON_Delete(meta);
    cJSON_Delete(list);
}

static void test_events(void)
{
    lx_audio_track_t *track = calloc(1, sizeof(*track));
    assert(track);
    cJSON *message = cJSON_Parse("{\"audio_event\":[{\"playerid\":9,\"title\":\"Other\"},"
        "{\"playerid\":3,\"title\":\"Song\",\"artist\":\"Artist\",\"mode\":\"play\",\"volume\":37,"
        "\"coverurl\":\"https://cdn.example/cover.png?version=1\"}]}");
    assert(lx_audio_track_update(track, 3, message));
    assert(track->mode == LX_AUDIO_MODE_PLAY && track->volume_known && track->volume == 37);
    assert(!strcmp(track->title, "Song") && !strcmp(track->artist, "Artist"));
    unsigned version = track->version;
    assert(!lx_audio_track_update(track, 3, message));
    assert(track->version == version);
    cJSON_Delete(message);
    message = cJSON_Parse("{\"audio_event\":[{\"playerid\":3,\"volume\":38,\"mode\":\"pause\"}]}");
    assert(lx_audio_track_update(track, 3, message));
    assert(track->version == version && !strcmp(track->title, "Song"));
    cJSON_Delete(message);
    message = cJSON_Parse("{\"status_result\":[{\"playerid\":3,\"title\":\"Next song\",\"volume\":-1,"
        "\"coverurl\":\"http://user:password@cdn.example/cover.png\"}]}");
    assert(lx_audio_track_update(track, 3, message));
    assert(track->version != version && !track->cover[0] && !track->volume_known);
    assert(!lx_audio_track_update(track, 4, message));
    cJSON_Delete(message);
    free(track);
}

static void test_favorites(void)
{
    lx_audio_favorites_t *page = calloc(1, sizeof(*page));
    assert(page);
    cJSON *message = cJSON_Parse("{\"getroomfavs_result\":[{\"id\":3,\"start\":32,\"totalitems\":34,"
        "\"items\":[{\"id\":400,\"slot\":7,\"name\":\"Jazz\"},{\"id\":9001,\"slot\":null,\"title\":\"Radio\"}]}]}");
    assert(lx_audio_favorites_parse(page, 3, 32, message));
    assert(page->ready && page->start == 32 && page->total == 34 && page->count == 2);
    assert(page->items[0].id == 400 && page->items[0].slot == 7);
    assert(page->items[1].id == 9001 && page->items[1].slot == -1);
    assert(!lx_audio_favorites_parse(page, 4, 32, message));
    assert(!lx_audio_favorites_parse(page, 3, 0, message));
    assert(page->items[0].id == 400);
    cJSON *group = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(message, "getroomfavs_result"), 0);
    cJSON *second = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(group, "items"), 1);
    cJSON_ReplaceItemInObjectCaseSensitive(second, "id", cJSON_CreateNumber(400));
    assert(!lx_audio_favorites_parse(page, 3, 32, message));
    assert(page->items[1].id == 9001);
    cJSON_Delete(message);
    message = cJSON_Parse("{\"getroomfavs_result\":[{\"id\":3,\"start\":0,\"totalitems\":0,\"items\":[]}]}");
    assert(lx_audio_favorites_parse(page, 3, 0, message));
    assert(page->ready && !page->count && !page->total);
    cJSON_Delete(message);
    free(page);
}

static void test_inputs(void)
{
    char session[LX_AUDIO_SESSION_SIZE];
    assert(lx_audio_session_token("LWSS V 17.3 | ~API:1~ | Session-Token: test-session\r\n", session));
    assert(!strcmp(session, "test-session"));
    assert(!lx_audio_session_token("Session-Token: test-session", session));
    assert(!lx_audio_session_token("LWSS V 17 | Session-Token: ", session));
    assert(!lx_audio_session_token("LWSS V 17 | Session-Token: bad:token", session));
    assert(lx_audio_cover_url("http://192.168.1.40:7091/imgcache/?item=foo&viaproxy=1"));
    assert(lx_audio_cover_url("https://i.scdn.co/image/test"));
    assert(!lx_audio_cover_url("https://user@host/image"));
    assert(!lx_audio_cover_url("file:///tmp/image.png"));
    assert(!lx_audio_cover_url("http://host/cover\r\nAuthorization: secret"));
    assert(!lx_audio_cover_url("//host/image"));
}

int main(void)
{
    test_catalog();
    test_events();
    test_favorites();
    test_inputs();
    puts("PASS: Audio-katalógus, parancsok, részleges események és lapozott kedvencek");
    return 0;
}
