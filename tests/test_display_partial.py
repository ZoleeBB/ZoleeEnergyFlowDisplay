#!/usr/bin/env python3
"""Exercise the actual CPU partial flush body across recycled framebuffers."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('partial_patch', ROOT / 'tools/patch_display_partial.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
parser = argparse.ArgumentParser()
parser.add_argument('--adapter', type=Path)
args = parser.parse_args()

# Alaptesztnél az adapter forgatóhívása határfelület; firmware CI-ben a
# ténylegesen buildelt 0.6.4 CPU-forgatófüggvényt is fordítjuk és futtatjuk.
rotation = r'''
static void display_rotate_copy_region(const void *source, void *target,
    uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t stride,
    uint16_t w, uint16_t h, int angle, uint8_t bpp, int small, int large)
{
    (void)small; (void)large;
    for (unsigned y = y1; y <= y2; y++) for (unsigned x = x1; x <= x2; x++) {
        unsigned px = angle == 90 ? w - 1 - y : y;
        unsigned py = angle == 90 ? x : h - 1 - x;
        memcpy((uint8_t *)target + (py * w + px) * bpp,
               (const uint8_t *)source + ((y-y1)*stride+x-x1)*bpp, bpp);
    }
}
'''
if args.adapter:
    bridge = (args.adapter / 'src/display/bridge/v9/lvgl_bridge_v9.c').read_text()
    corrected = module.patch(bridge)
    assert module.patch(corrected) == corrected
    common = (args.adapter / 'src/display/bridge/common/display_bridge_common.c').read_text()
    start = common.index('void IRAM_ATTR display_rotate_copy_region(')
    body = common.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (common[end] == '{') - (common[end] == '}')
        end += 1
    rotation = common[start:end]

fixture = module.ANCHOR + module.SIGNATURE + '\n}\n'
fixed = module.patch(fixture)
assert module.patch(fixed) == fixed
for unsupported in (fixture + fixture, fixture.replace(module.ANCHOR, ''), 'unsupported'):
    try:
        module.patch(unsupported)
    except ValueError:
        pass
    else:
        raise AssertionError('Ambiguous or unsupported source accepted')

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_LV_ADAPTER_ROTATE_0 0
#define ESP_LV_ADAPTER_ROTATE_90 90
#define ESP_LV_ADAPTER_ROTATE_180 180
#define ESP_LV_ADAPTER_ROTATE_270 270
typedef int esp_lv_adapter_rotation_t;
typedef int esp_err_t;
typedef struct { int x1,y1,x2,y2; } lv_area_t;
typedef struct { struct {unsigned stride;} header; } draw_buf_t;
typedef struct { draw_buf_t *buf_act; bool last; } lv_display_t;
struct display_pipeline_buf { void *buffer; };
typedef struct {
    void *disp_fb,*draw_fb;
    struct {size_t frame_buffer_size;} runtime;
    int pipeline;
    bool cpu_partial_started;
    unsigned bpp;
    int angle;
} esp_lv_adapter_display_bridge_v9_t;
static _Alignas(128) uint8_t pool[3][16*24*3];
static unsigned writes, submits, ready, marks, failures;
static bool fail_submit, fail_free;
static void *written;
#define ESP_LOGE(tag,...) do { (void)(tag); failures++; } while(0)
static const char *TAG = "test";
static const char *esp_err_to_name(int result) { (void)result; return "test"; }
static unsigned bridge_color_bytes(esp_lv_adapter_display_bridge_v9_t *i) { return i->bpp; }
static size_t compact_stride_to_packed(uint8_t *p,const lv_area_t *a,lv_display_t *d,unsigned b) {
    (void)p;(void)a;(void)d;(void)b;assert(false);return 0;
}
static bool lv_display_flush_is_last(lv_display_t *d) { return d->last; }
static void display_cache_msync_framebuffer(void *buffer,size_t n) {
    assert(n && buffer);written=buffer;writes++;
}
static int display_bridge_v9_blit_full_frame(esp_lv_adapter_display_bridge_v9_t *i,void *p,bool sync) {
    assert(p==i->draw_fb && p!=i->disp_fb && written==p && sync);
    written=NULL;submits++;return fail_submit ? -1 : ESP_OK;
}
static void display_bridge_pipeline_mark_buf_busy(int *p,void *old) { (void)p;assert(old);marks++; }
static struct display_pipeline_buf *display_bridge_pipeline_wait_free_buf(int *p) {
    static struct display_pipeline_buf next;
    if(fail_free)return NULL;
    unsigned selected=(*p+1+submits%2)%3;
    if(selected==(unsigned)*p)selected=(selected+1)%3;
    next.buffer=pool[selected];*p=selected;return &next;
}
static void display_manager_flush_ready(lv_display_t *d) { (void)d;ready++; }
'''
wrapper = r'''
static void rotate_copy_strided_region(const void *src,void *dst,
    uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t stride,
    esp_lv_adapter_display_bridge_v9_t *impl)
{
    display_rotate_copy_region(src,dst,x1,y1,x2,y2,stride,16,24,impl->angle,impl->bpp,4,8);
}
'''
main = r'''
static uint8_t scene[24*16*3], expected[16*24*3], front[16*24*3];
static _Alignas(128) uint8_t stripe[26*4*3];
static uint32_t seed=12345;
static unsigned next_random(void) { seed=seed*1664525U+1013904223U;return seed; }
static void rectangle(esp_lv_adapter_display_bridge_v9_t *impl,lv_area_t rect,unsigned paint,bool last) {
    unsigned bpp=impl->bpp,width=rect.x2-rect.x1+1;
    unsigned height=rect.y2-rect.y1+1;
    assert(height<=4);
    unsigned stride=width+2;
    memset(stripe,0x99,sizeof(stripe));
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)for(unsigned c=0;c<bpp;c++) {
        uint8_t pixel=(paint*37+x*11+y*17+c*53)&255;
        scene[((rect.y1+y)*24+rect.x1+x)*bpp+c]=pixel;
        stripe[(y*stride+x)*bpp+c]=pixel;
    }
    draw_buf_t buffer={.header.stride=stride*bpp};
    lv_display_t display={.buf_act=&buffer,.last=last};
    unsigned old_writes=writes,old_submits=submits,old_ready=ready;
    memcpy(front,impl->disp_fb,impl->runtime.frame_buffer_size);
    void *old_front=impl->disp_fb;
    display_bridge_v9_flush_partial_cpu(impl,&display,&rect,stripe);
    assert(ready==old_ready+1);
    assert(!memcmp(front,old_front,impl->runtime.frame_buffer_size));
    assert(writes==old_writes+(unsigned)last && submits==old_submits+(unsigned)last);
}
static void check_frame(esp_lv_adapter_display_bridge_v9_t *impl) {
    unsigned bpp=impl->bpp;
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<24;x++) {
        unsigned px=impl->angle==90?15-y:y;
        unsigned py=impl->angle==90?x:23-x;
        memcpy(expected+(py*16+px)*bpp,scene+(y*24+x)*bpp,bpp);
    }
    assert(!memcmp(expected,impl->disp_fb,impl->runtime.frame_buffer_size));
    assert(!impl->cpu_partial_started);
}
int main(void) {
    for(unsigned bpp=2;bpp<=3;bpp++)for(int angle=90;angle<=270;angle+=180) {
        memset(pool,0,sizeof(pool));memset(scene,0,sizeof(scene));
        esp_lv_adapter_display_bridge_v9_t impl={.disp_fb=pool[0],.draw_fb=pool[1],
            .pipeline=1,.runtime.frame_buffer_size=16*24*bpp,.bpp=bpp,.angle=angle};
        for(unsigned frame=0;frame<120;frame++) {
            if (frame % 30 == 0) {
                for (unsigned y = 0; y < 16; y += 4)
                    rectangle(&impl, (lv_area_t){0,(int)y,23,(int)y+3},frame+y,y==12);
                check_frame(&impl);
            }
            unsigned rectangles=1+next_random()%8;
            for(unsigned r=0;r<rectangles;r++) {
                unsigned x=next_random()%20,y=next_random()%13;
                lv_area_t area={(int)x,(int)y,(int)(x+1+next_random()%3),(int)(y+next_random()%4)};
                rectangle(&impl,area,frame*8+r,r+1==rectangles);
            }
            check_frame(&impl);
        }
        /* Sikertelen submit után a következő kép a valóban leadott állapotból épüljön. */
        uint8_t saved[sizeof(scene)];memcpy(saved,scene,sizeof(saved));
        void *old=impl.disp_fb;fail_submit=true;
        rectangle(&impl,(lv_area_t){0,0,3,2},7,true);
        assert(impl.disp_fb==old && !impl.cpu_partial_started);
        memcpy(scene,saved,sizeof(scene));fail_submit=false;
        rectangle(&impl,(lv_area_t){20,12,23,15},9,true);check_frame(&impl);
        /* Nem létező szabad puffer után a már leadott képre tilos írni. */
        fail_free=true;rectangle(&impl,(lv_area_t){4,4,7,7},11,true);
        assert(!impl.draw_fb);check_frame(&impl);
        memcpy(front,impl.disp_fb,impl.runtime.frame_buffer_size);
        lv_display_t ignored={0};lv_area_t point={0,0,0,0};
        unsigned old_ready=ready;
        display_bridge_v9_flush_partial_cpu(&impl,&ignored,&point,stripe);
        assert(ready==old_ready+1 && !memcmp(front,impl.disp_fb,impl.runtime.frame_buffer_size));
        fail_free=false;
    }
    assert(failures==8 && marks==submits-4);
    puts("PASS: CPU partial flush preserves complete frames across 480 recycled-buffer frames, overlapping/padded stripes, 90/270 rotation, RGB565/RGB888 and submit/free-buffer failures");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='zolee-partial-') as directory:
    for broken in (True, False):
        body = module.CPU_PARTIAL
        if broken:
            body = body.replace('memcpy(impl->draw_fb, impl->disp_fb, impl->runtime.frame_buffer_size);', '(void)impl;')
        path = Path(directory)/('broken.c' if broken else 'fixed.c')
        path.write_text(prefix + rotation + wrapper + body + main)
        binary = path.with_suffix('')
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function','-fsanitize=undefined','-fno-sanitize-recover=all',
                        str(path),'-o',str(binary)],check=True)
        result = subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode == 0) == (not broken), result.stderr
        if not broken:print(result.stdout.strip())
if args.adapter:
    print('PASS: build-only patch applied to actual pinned adapter; actual upstream CPU rotation executed')
