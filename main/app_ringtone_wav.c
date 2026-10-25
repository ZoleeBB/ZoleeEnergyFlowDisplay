#include "app_ringtone_wav.h"
#include <string.h>

static uint32_t little(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool app_ringtone_name(const char* name)
{
    if (!name)
        return false;
    size_t n = strlen(name);
    if (n < 5 || n > 64 || strcmp(name + n - 4, ".wav") || strstr(name, ".."))
        return false;
    for (size_t i = 0; i < n - 4; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
            return false;
    }
    return true;
}

bool app_ringtone_wav(const uint8_t* data, size_t size, size_t* offset, size_t* bytes)
{
    if (!data || !offset || !bytes || size < 44 || size > APP_RINGTONE_MAX || memcmp(data, "RIFF", 4)
        || memcmp(data + 8, "WAVE", 4) || little(data + 4) != size - 8)
        return false;
    bool format = false, samples = false;
    size_t start = 0, length = 0;
    for (size_t p = 12; p < size;) {
        if (size - p < 8)
            return false;
        uint32_t n = little(data + p + 4);
        if (n > size - p - 8)
            return false;
        if (!memcmp(data + p, "fmt ", 4)) {
            if (format || n < 16 || data[p + 8] != 1 || data[p + 9] || data[p + 10] != 1 || data[p + 11]
                || little(data + p + 12) != 16000 || little(data + p + 16) != 32000 || data[p + 20] != 2
                || data[p + 21] || data[p + 22] != 16 || data[p + 23])
                return false;
            format = true;
        } else if (!memcmp(data + p, "data", 4)) {
            if (samples || !n || n > APP_RINGTONE_PCM_MAX || n % 2)
                return false;
            samples = true;
            start = p + 8;
            length = n;
        }
        size_t step = 8U + n + (n & 1U);
        if (step > size - p)
            return false;
        p += step;
    }
    if (!format || !samples)
        return false;
    *offset = start;
    *bytes = length;
    return true;
}

const char* app_ringtone_load_message(app_ringtone_load_status_t status)
{
    switch (status) {
    case APP_RINGTONE_LOAD_BUSY:
        return "A csengőhangra várakozás lejárt: az SD-kártya foglalt.";
    case APP_RINGTONE_LOAD_MISSING:
        return "A mentett csengőhang hiányzik. Válassz a telepített hangok közül és mentsd.";
    case APP_RINGTONE_LOAD_MEMORY:
        return "Nincs elég szabad memória a csengőhang betöltéséhez.";
    case APP_RINGTONE_LOAD_IO:
        return "A csengőhang olvasása sikertelen (SD/fájlhozzáférés).";
    case APP_RINGTONE_LOAD_NETWORK:
        return "A Slave nem tudta letölteni a csengőhangot a Masterről.";
    case APP_RINGTONE_LOAD_AUTH:
        return "A Master nem engedélyezte a csengőhang letöltését. Ellenőrizd a Slave kapcsolatát.";
    default:
        return "A csengőhang WAV-formátuma hibás vagy nem támogatott.";
    }
}
