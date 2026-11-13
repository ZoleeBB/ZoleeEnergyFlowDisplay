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

static void test_native_display_text(void)
{
    lx_audio_track_t *track = calloc(1, sizeof(*track));
    assert(track);
    cJSON *message = cJSON_Parse("{\"audio_event\":[{\"playerid\":3,"
        "\"title\":\"%C5%90szi%20dal%20%2B%20r%C3%A1di%C3%B3\","
        "\"artist\":\"El%C5%91ad%C3%B3\",\"album\":\"A+B\","
        "\"station\":\"R%C3%A1di%C3%B3%20Teszt\","
        "\"coverurl\":\"https://cdn.example/cover%20image.png\"}]}");
    assert(lx_audio_track_update(track, 3, message));
    assert(!strcmp(track->title, "Őszi dal + rádió"));
    assert(!strcmp(track->artist, "Előadó") && !strcmp(track->album, "A+B"));
    assert(!strcmp(track->station, "Rádió Teszt"));
    assert(!strcmp(track->cover, "https://cdn.example/cover%20image.png"));
    unsigned version = track->version;
    assert(!lx_audio_track_update(track, 3, message) && track->version == version);
    cJSON *row = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(message, "audio_event"), 0);
    char encoded[481];
    for (unsigned i = 0; i < 80; ++i)
        memcpy(encoded + i * 6, "%C5%91", 6);
    encoded[480] = 0;
    cJSON_ReplaceItemInObjectCaseSensitive(row, "title", cJSON_CreateString(encoded));
    assert(lx_audio_track_update(track, 3, message));
    assert(strlen(track->title) == 160 && !strncmp(track->title, "őőő", 6));
    assert(!lx_audio_track_update(track, 3, message));
    const char *invalid[] = {"100%", "%GG", "%C3%28", "%00title", "%C0%AF", "%ED%A0%80"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        cJSON_ReplaceItemInObjectCaseSensitive(row, "title", cJSON_CreateString(invalid[i]));
        assert(lx_audio_track_update(track, 3, message));
        assert(!strcmp(track->title, invalid[i]));
    }
    cJSON_Delete(message);
    free(track);
}

static void test_endpoint_port(void)
{
    const char *json = "{\"mediaServer\":{\"server-one\":{\"type\":2,"
        "\"localIP\":\"192.168.1.40\",\"host\":\"audio.example:17091\"}},"
        "\"controls\":{\"" ZONE "\":{\"type\":\"AudioZoneV2\",\"name\":\"Audio\","
        "\"uuidAction\":\"" ACTION "\",\"details\":{\"server\":\"server-one\",\"playerid\":3}}}}";
    cJSON *list = lx_catalog_parse(json, strlen(json));
    assert(list);
    lx_audio_endpoint_t endpoint;
    assert(lx_audio_endpoint(lx_audio_catalog_find(list, ZONE), &endpoint));
    assert(!endpoint.blocked && !strcmp(endpoint.host, "192.168.1.40") && endpoint.port == 17091);
    cJSON_Delete(list);
}

static void test_observed_native_events(void)
{
    /* Valós paneles DevTools-események; a helyiség- és fióknév tesztadat.
     * A borító URL-jét a parser nem dekódolja vagy helyettesíti. */
    const char *radio = "{\"audio_event\":[{\"album\":\"\",\"artist\":\"\","
        "\"audiopath\":\"tunein:station:s25966\",\"audiotype\":1,\"clientState\":\"on\","
        "\"coverurl\":\"https://cdn-profiles.tunein.com/s25966/images/logoq.jpg?t=637069206200000000\","
        "\"duration\":0,\"duration_ms\":0,\"mode\":\"stop\",\"name\":\"Tesztszoba\","
        "\"parent\":null,\"pbr\":16397,\"playerid\":5,\"plrepeat\":0,\"plshuffle\":1,"
        "\"position_ms\":0,\"power\":\"on\",\"qid\":\"7EMD53ei\",\"qindex\":0,"
        "\"station\":\"95.8 Sláger FM\",\"time\":0,\"title\":\"\",\"type\":2,\"volume\":6}]}";
    const char *spotify = "{\"audio_event\":[{\"album\":\"Here Comes The Hotstepper\","
        "\"artist\":\"iNi Kamoze\",\"audiopath\":\"spotify:track:3QRM0qZB7oMYavveH0iEqx\","
        "\"audiotype\":0,\"clientState\":\"on\","
        "\"coverurl\":\"https://i.scdn.co/image/ab67616d0000b2736ffa23f19d8c125204e6220c\","
        "\"duration\":250.46600000000001,\"mode\":\"stop\",\"name\":\"Tesztszoba\","
        "\"parent\":null,\"pbr\":15423,\"playerid\":5,\"plrepeat\":1,\"plshuffle\":1,"
        "\"power\":\"on\",\"qid\":\"50b592d16d0a1bb47256\",\"qindex\":0,"
        "\"sourceName\":\"Tesztfelhasználó\",\"station\":\"\",\"time\":0,"
        "\"title\":\"Here Comes the Hotstepper - Heartical Mix\",\"type\":3,\"volume\":12}]}";
    lx_audio_track_t *track = calloc(1, sizeof(*track));
    assert(track);
    cJSON *message = cJSON_Parse(radio);
    assert(message && lx_audio_track_update(track, 5, message));
    assert(track->mode == LX_AUDIO_MODE_STOP && track->volume_known && track->volume == 6);
    assert(!track->title[0] && !track->artist[0] && !strcmp(track->station, "95.8 Sláger FM"));
    assert(!strcmp(track->cover, "https://cdn-profiles.tunein.com/s25966/images/logoq.jpg?t=637069206200000000"));
    assert(!lx_audio_track_update(track, 5, message));
    unsigned version = track->version;
    cJSON_Delete(message);
    message = cJSON_Parse(spotify);
    assert(message && !lx_audio_track_update(track, 4, message) && track->version == version);
    assert(lx_audio_track_update(track, 5, message));
    assert(!strcmp(track->title, "Here Comes the Hotstepper - Heartical Mix"));
    assert(!strcmp(track->artist, "iNi Kamoze") && !strcmp(track->album, "Here Comes The Hotstepper"));
    assert(!track->station[0] && track->volume_known && track->volume == 12);
    assert(!strcmp(track->cover, "https://i.scdn.co/image/ab67616d0000b2736ffa23f19d8c125204e6220c"));
    assert(track->version != version && !lx_audio_track_update(track, 5, message));
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
    cJSON_ReplaceItemInObjectCaseSensitive(second, "title", cJSON_CreateString("R%C3%A1di%C3%B3%20%2B%20Jazz"));
    assert(lx_audio_favorites_parse(page, 3, 32, message));
    assert(!strcmp(page->items[1].name, "Rádió + Jazz"));
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
    test_endpoint_port();
    test_events();
    test_native_display_text();
    test_observed_native_events();
    test_favorites();
    test_inputs();
    puts("PASS: Audio-katalógus, parancsok, részleges események és lapozott kedvencek");
    return 0;
}
