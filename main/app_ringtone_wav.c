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
            if (samples || !n || n > 256000 || n % 2)
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
