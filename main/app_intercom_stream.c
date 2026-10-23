/* A videófolyam keretezése platformfüggetlen, korlátos és külön tesztelhető. */
#include "app_intercom_stream.h"

enum { SEEK, SOI, MARKER, CODE, LENGTH_HIGH, LENGTH_LOW, SKIP, SCAN, SCAN_FF };

void app_intercom_stream_feed(app_intercom_stream_t *s, const uint8_t *data,
                              size_t size, app_intercom_frame_fn frame, void *context)
{
    if (!s || !s->buffer || s->capacity < 4 || (!data && size))
        return;
    for (size_t i = 0; i < size; ++i)
    {
        unsigned byte = data[i];
        if (s->state >= MARKER && s->used >= s->capacity)
        {
            s->oversized_frames++;
            s->used = 0;
            s->state = SEEK;
        }
        if (s->state >= MARKER)
            s->buffer[s->used++] = (uint8_t)byte;
        switch (s->state)
        {
            case SEEK:
                if (byte == 0xff) s->state = SOI;
                break;
            case SOI:
                if (byte == 0xd8)
                {
                    s->frame_starts++;
                    s->buffer[0] = 0xff;
                    s->buffer[1] = 0xd8;
                    s->used = 2;
                    s->state = MARKER;
                }
                else if (byte != 0xff) s->state = SEEK;
                break;
            case MARKER:
                s->state = byte == 0xff ? CODE : SEEK;
                break;
            case SCAN:
                if (byte == 0xff) s->state = SCAN_FF;
                break;
            case SCAN_FF:
                if (byte == 0 || (byte >= 0xd0 && byte <= 0xd7))
                {
                    s->state = SCAN;
                    break;
                }
                /* Az entrópiakódolt rész végét ugyanaz a markerkezelő fogadja. */
                /* fall through */
            case CODE:
                if (byte == 0xff) break;
                if (byte == 0xd9)
                {
                    if (frame) frame(context, s->buffer, s->used);
                    s->state = SEEK;
                    s->used = 0;
                }
                else if (byte == 0xd8)
                {
                    s->frame_starts++;
                    s->buffer[0] = 0xff;
                    s->buffer[1] = 0xd8;
                    s->used = 2;
                    s->state = MARKER;
                }
                else if (byte == 1 || (byte >= 0xd0 && byte <= 0xd7)) s->state = MARKER;
                else if (byte == 0) s->state = SEEK;
                else
                {
                    s->marker = byte;
                    s->state = LENGTH_HIGH;
                }
                break;
            case LENGTH_HIGH:
                s->length = byte << 8;
                s->state = LENGTH_LOW;
                break;
            case LENGTH_LOW:
                s->length |= byte;
                if (s->length < 2) s->state = SEEK;
                else
                {
                    s->remaining = s->length - 2;
                    s->state = s->remaining ? SKIP : s->marker == 0xda ? SCAN : MARKER;
                }
                break;
            case SKIP:
                if (!--s->remaining) s->state = s->marker == 0xda ? SCAN : MARKER;
                break;
            default:
                s->state = SEEK;
        }
    }
}
