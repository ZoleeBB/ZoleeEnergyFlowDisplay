#!/usr/bin/env python3
"""Reproduce disabled-profile PPA calls and verify the adapter source correction."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('rotation_patch', ROOT/'tools/patch_display_rotation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

# Conditions from the pinned 0.6.4 registration, both rotation functions and
# cache write-back branch. Hardware calls become observable counters.
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
struct profile { bool enable_ppa_accel; };
struct config { struct { struct profile profile; } base; };
struct impl { struct config cfg; void *disp_fb, *draw_fb; struct {size_t frame_buffer_size;} runtime; };
static struct { void *ppa_handle; } hw_resource;
static int registrations, ppa_calls, cpu_calls, writebacks;
static void setup(struct config *cfg) {
    (void)cfg;
    if (hw_resource.ppa_handle == NULL) {
        ++registrations; hw_resource.ppa_handle = (void *)1;
    }
}
static void rotate_strided(struct impl *impl, int color_bytes) {
    (void)impl;
    if (hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3)) {
        ++ppa_calls; return;
    }
    ++cpu_calls;
}
static void rotate_region(struct impl *impl, int color_bytes) {
    (void)impl;
    if (hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3)) {
        ++ppa_calls; return;
    }
    ++cpu_calls;
}
static void flush(struct impl *impl) {
    (void)impl;
    if (!hw_resource.ppa_handle) { ++writebacks; }
}
/* Simulate a cache line shared by newly rendered and repaired pixels. CPU
 * stores stay cached; DMA writes RAM. A later write-back clobbers DMA pixels. */
static unsigned memory[4], cached[4];
static int dma_repairs, cpu_repairs, invalidations;
static bool display_bridge_dma2d_window_is_compatible(void *p,int w,int x,int n,int b) {
    (void)p;(void)w;(void)x;(void)n;(void)b;return true;
}
static void display_cache_msync_invalidate_framebuffer(void *p,size_t n) {
    (void)p;(void)n;++invalidations;memcpy(cached,memory,sizeof(cached));
}
static void repair_diff(struct impl *impl) {
    (void)impl;
    int hor_res=8,copy_w_px=2,color_bytes=2;
    struct {int x1;} rect={0};
    if (display_bridge_dma2d_window_is_compatible(
                    impl->disp_fb, hor_res, rect.x1, copy_w_px, color_bytes)) {
        memory[2]=9;++dma_repairs;
    } else {cached[2]=9;++cpu_repairs;}
}
static void repair_baseline(struct impl *impl) {
    (void)impl;
    int hor_res=8,offset_x=0,copy_w_px=2,color_bytes=2;
    if (display_bridge_dma2d_window_is_compatible(impl->disp_fb, hor_res, offset_x, copy_w_px, color_bytes)) {
        memory[2]=9;++dma_repairs;
    } else {cached[2]=9;++cpu_repairs;}
}
static void copy_stripe(struct impl *impl) {
    (void)impl;
    int src_stride=8,bytes_per_pixel=2;
    if (src_stride % bytes_per_pixel == 0 && true) {memory[2]=9;++dma_repairs;}
    else {cached[2]=9;++cpu_repairs;}
}
static void submit_rotated(struct impl *impl) {
    (void)impl;memcpy(memory,cached,sizeof(memory));
    display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);
}
static void submit_unrotated(struct impl *impl) {
    (void)impl;memcpy(memory,cached,sizeof(memory));
    display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);
}
static void render_and_repair(struct impl *impl,void (*repair)(struct impl *),bool rotated) {
    memory[1]=3;memory[2]=3;memcpy(cached,memory,sizeof(cached));
    cached[1]=7; /* new text pixel, same cache line as repair pixel */
    repair(impl);
    if(rotated)submit_rotated(impl);else submit_unrotated(impl);
    assert(memory[1]==7 && memory[2]==9);
}
int main(void) {
    struct impl disabled = {.cfg.base.profile.enable_ppa_accel=false};
    setup(&disabled.cfg);
    rotate_strided(&disabled, 2); rotate_region(&disabled, 2); flush(&disabled);
    assert(registrations == 0 && ppa_calls == 0 && cpu_calls == 2 && writebacks == 1);
    /* Another display may own the singleton PPA handle: disabled stays CPU. */
    struct impl enabled = {.cfg.base.profile.enable_ppa_accel=true};
    setup(&enabled.cfg);
    rotate_strided(&enabled, 2); rotate_region(&enabled, 3); flush(&enabled);
    assert(registrations == 1 && ppa_calls == 2 && cpu_calls == 2 && writebacks == 1);
    setup(&disabled.cfg);
    rotate_strided(&disabled, 2); rotate_region(&disabled, 3); flush(&disabled);
    assert(registrations == 1 && ppa_calls == 2 && cpu_calls == 4 && writebacks == 2);
    rotate_region(&enabled, 4);
    assert(cpu_calls == 5);
    render_and_repair(&disabled,repair_diff,true);
    render_and_repair(&disabled,repair_baseline,true);
    render_and_repair(&disabled,copy_stripe,false);
    assert(cpu_repairs==3 && dma_repairs==0 && invalidations==0);
    return 0;
}
'''
fixed = module.patch(source)
unsafe_mixed = fixed.replace('if (impl->cfg.base.profile.enable_ppa_accel &&\n                display_bridge_dma2d_window_is_compatible(', 'if (display_bridge_dma2d_window_is_compatible(', 1)
assert module.patch(fixed) == fixed
for broken in [source.replace('if (!hw_resource.ppa_handle)', 'if (false)'), source + source, 'unsupported']:
    try:
        module.patch(broken)
    except ValueError:
        pass
    else:
        raise AssertionError('Unsupported source must fail closed')
with tempfile.TemporaryDirectory(prefix='zolee-rotation-') as directory:
    tmp = Path(directory)
    for name, code in [('original', source), ('unsafe_mixed', unsafe_mixed), ('fixed', fixed)]:
        path = tmp/(name+'.c'); path.write_text(code)
        binary = tmp/name
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', str(path), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        assert (result.returncode == 0) == (name == 'fixed')
print('PASS: original ignores disabled PPA; corrected registration, both rotation paths and cache fallback honor profile')
print('PASS: mixed CPU/DMA repair loses a neighboring pixel on write-back; CPU ownership preserves both and avoids invalidation')
