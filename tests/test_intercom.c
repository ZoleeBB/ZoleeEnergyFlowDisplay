/* Valódi JPEG-keretező és Loxone-delta teszt, minden TCP-darabolással. */
#include "app_intercom_stream.h"
#include "app_loxone_home_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned frames;
/* APP szegmensben szándékos FF D9: ez nem zárhatja le a képet.
 * A scan részben FF00 stuffing és restart marker is szerepel. */
static const uint8_t jpeg[] = {0xff,0xd8,0xff,0xe0,0,6,0xff,0xd9,3,4,
    0xff,0xda,0,2,1,2,0xff,0,0xd9,0xff,0xd0,5,0xff,0xd9};
static void frame(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    assert(size == sizeof(jpeg) && !memcmp(data, jpeg, size));
    frames++;
}
static void number(uint8_t data[24], double value)
{
    memset(data, 0, 24);
    uint64_t bits;
    memcpy(&bits, &value, 8);
    for (unsigned i = 0; i < 8; ++i) data[16+i] = (uint8_t)(bits >> (8*i));
}
int main(void)
{
    uint8_t buffer[128];
    for (size_t chunk = 1; chunk <= sizeof(jpeg); ++chunk)
    {
        app_intercom_stream_t stream = {.buffer=buffer,.capacity=sizeof(buffer)};
        frames = 0;
        const uint8_t header[] = "\r\n--boundary\r\nContent-Type: image/jpeg\r\n\r\n";
        for (unsigned repeat = 0; repeat < 3; ++repeat)
        {
            app_intercom_stream_feed(&stream, header, sizeof(header)-1, frame, NULL);
            for (size_t i = 0; i < sizeof(jpeg); i += chunk)
            {
                size_t count = sizeof(jpeg) - i;
                if (count > chunk) count = chunk;
                app_intercom_stream_feed(&stream, jpeg+i, count, frame, NULL);
            }
        }
        assert(frames == 3);
    }
    app_intercom_stream_t stream = {.buffer=buffer,.capacity=sizeof(buffer)};
    uint8_t oversized[300];
    memset(oversized, 3, sizeof(oversized));
    memcpy(oversized, jpeg, 14);
    frames = 0;
    app_intercom_stream_feed(&stream, oversized, sizeof(oversized), frame, NULL);
    app_intercom_stream_feed(&stream, jpeg, sizeof(jpeg), frame, NULL);
    assert(frames == 1 && stream.used == 0);
    lx_intercom_config_t config = {.enabled=true};
    strcpy(config.bell_uuid, "00000000-0000-0000-0000000000000000");
    bool ringing = false;
    uint8_t data[24];
    number(data, 1); assert(lx_intercom_bell_update(&ringing,&config,2,data,24) && ringing);
    number(data, NAN); assert(lx_intercom_bell_update(&ringing,&config,2,data,24) && ringing);
    number(data, 2); assert(lx_intercom_bell_update(&ringing,&config,2,data,24) && ringing);
    number(data, 0); assert(!lx_intercom_bell_update(&ringing,&config,2,data,23) && ringing);
    assert(lx_intercom_bell_update(&ringing,&config,3,data,24) && ringing);
    data[0] = 7; assert(lx_intercom_bell_update(&ringing,&config,2,data,24) && ringing);
    data[0] = 0; assert(lx_intercom_bell_update(&ringing,&config,2,data,24) && !ringing);
    static lx_home_config_t home, reloaded;
    const char *json = "{\"schema\":2,\"pages\":[],\"intercom\":{\"enabled\":true,"
        "\"stream_url\":\"http://192.168.2.21/mjpg/video.mjpg\","
        "\"bell_uuid\":\"00000000-0000-0000-0000000000000000\","
        "\"gate\":{\"id\":\"intercom-gate\",\"name\":\"Kapunyitás\",\"icon\":\"door\","
        "\"type\":\"pulse\",\"control_uuid\":\"11223344-5566-7788-99aabbccddeeff00\"}}}";
    cJSON *root = cJSON_Parse(json);
    char error[LX_HOME_ERROR_SIZE], command[96];
    assert(root && lx_home_config_parse(root, &home, error));
    assert(home.intercom.enabled && home.card_count == 0);
    cJSON *roundtrip = lx_home_config_json(&home);
    assert(roundtrip && lx_home_config_parse(roundtrip, &reloaded, error));
    assert(!memcmp(&home, &reloaded, sizeof(home)));
    cJSON_Delete(roundtrip);
    cJSON *door = cJSON_GetObjectItemCaseSensitive(root, "intercom");
    const char *bad_urls[] = {"http://u:p@host/mjpg/video.mjpg", "https://host/video", "file:///etc/passwd",
                             "http:///video", "http://host/", "http://host?query/video", "http://host/a b"};
    for (unsigned i = 0; i < sizeof(bad_urls)/sizeof(bad_urls[0]); ++i)
    {
        cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(door, "stream_url"), bad_urls[i]);
        assert(!lx_home_config_parse(root, &reloaded, error) && !memcmp(&home, &reloaded, sizeof(home)));
    }
    cJSON_Delete(root);
    cJSON *catalog = cJSON_Parse("[{\"control_uuid\":\"11223344-5566-7788-99aabbccddeeff00\","
        "\"action_uuid\":\"00000000-0000-0000-0000000000000000\",\"type\":\"Pushbutton\",\"secured\":false}]");
    assert(catalog && lx_home_command_build(&home, catalog, LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE, command));
    assert(!strcmp(command, "jdev/sps/io/00000000-0000-0000-0000000000000000/pulse"));
    assert(!lx_home_command_build(&home, catalog, LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_ON, command));
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(catalog,0), "secured", cJSON_CreateTrue());
    assert(!lx_home_command_build(&home, catalog, LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE, command));
    cJSON_Delete(catalog);
    puts("PASS: bounded MJPEG framing, all chunk sizes, embedded EOI, stuffing/restart, overflow recovery; explicit bell 1/0, unrelated/malformed/non-finite deltas preserve visibility");
}
