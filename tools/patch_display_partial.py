#!/usr/bin/env python3
"""Build-only CPU partial rotation with a coherent complete-frame baseline."""
import argparse
from pathlib import Path

FIELD = '    bool cpu_partial_started;\n'
ANCHOR = '    bool frame_buf_submit_pending;\n'
SIGNATURE = '''static void display_bridge_v9_flush_partial_rotate(esp_lv_adapter_display_bridge_v9_t *impl,
                                                   lv_display_t *disp,
                                                   const lv_area_t *area,
                                                   uint8_t *color_map)
{'''
DISPATCH = '''
    if (!impl->cfg.base.profile.enable_ppa_accel) {
        display_bridge_v9_flush_partial_cpu(impl, disp, area, color_map);
        return;
    }
'''
CPU_PARTIAL = '''/* CPU által írt pufferen nem keverünk DMA-javítást és CPU-cache-t.
 * A legutóbb leadott teljes képből egyszer készül az új frame alapja;
 * csak a megváltozott LVGL-részleteket rajzoljuk/forgatjuk felül.
 * Az előző kész puffer érintetlen, amíg a panel még olvashatja. */
static void display_bridge_v9_flush_partial_cpu(esp_lv_adapter_display_bridge_v9_t *impl,
                                              lv_display_t *disp,
                                              const lv_area_t *area,
                                              uint8_t *color_map)
{
    if (!impl->cpu_partial_started) {
        memcpy(impl->draw_fb, impl->disp_fb, impl->runtime.frame_buffer_size);
        impl->cpu_partial_started = true;
    }

    const uint8_t color_bytes = bridge_color_bytes(impl);
    size_t stride_bytes = disp->buf_act->header.stride;
    if (stride_bytes % color_bytes != 0) {
        stride_bytes = compact_stride_to_packed(color_map, area, disp, color_bytes);
    }
    rotate_copy_strided_region(color_map, impl->draw_fb,
                               area->x1, area->y1, area->x2, area->y2,
                               stride_bytes / color_bytes, impl);

    if (lv_display_flush_is_last(disp)) {
        impl->cpu_partial_started = false;
        /* A panel DMA-ja csak az összes részlet után láthatja az új képet.
         * Nincs M2C invalidálás: minden célképpontot a CPU birtokol. */
        display_cache_msync_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);
        esp_err_t result = display_bridge_v9_blit_full_frame(impl, impl->draw_fb, true);
        if (result == ESP_OK) {
            display_bridge_pipeline_mark_buf_busy(&impl->pipeline, impl->disp_fb);
            struct display_pipeline_buf *next = display_bridge_pipeline_wait_free_buf(&impl->pipeline);
            impl->disp_fb = impl->draw_fb;
            if (next) {
                impl->draw_fb = next->buffer;
            } else {
                /* Az adapter végtelen várakozása normál üzemben nem tér
                 * vissza NULL-lal. Hiba után ne írhassunk a leadott képre. */
                ESP_LOGE(TAG, "No free partial framebuffer");
                impl->draw_fb = NULL;
            }
        } else {
            ESP_LOGE(TAG, "Partial frame blit failed: %s", esp_err_to_name(result));
        }
    }
    display_manager_flush_ready(disp);
}

'''
# A hiányzó szabad puffer hibája után nincs írható cél; a panel képe megmarad.
CPU_PARTIAL = CPU_PARTIAL.replace('    if (!impl->cpu_partial_started) {', '''    if (!impl->draw_fb) {
        display_manager_flush_ready(disp);
        return;
    }
    if (!impl->cpu_partial_started) {''', 1)


def patch(source):
    if source.count(CPU_PARTIAL) == 1 and source.count(SIGNATURE + DISPATCH) == 1 and source.count(FIELD) == 1:
        return source
    if source.count(ANCHOR) != 1 or source.count(SIGNATURE) != 1 or FIELD in source or 'display_bridge_v9_flush_partial_cpu' in source:
        raise ValueError('Unsupported partial-rotation adapter; expected pinned 0.6.4 source')
    source = source.replace(ANCHOR, ANCHOR + FIELD, 1)
    return source.replace(SIGNATURE, CPU_PARTIAL + SIGNATURE + DISPATCH, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        raise SystemExit('The managed source must not be overwritten')
    result = patch(args.input.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print('CPU partial rotation uses one coherent baseline and submits only completed frames')
