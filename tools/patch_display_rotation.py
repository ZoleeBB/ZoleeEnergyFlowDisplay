#!/usr/bin/env python3
"""Keep adapter 0.6.4 CPU rotation and partial repair cache-coherent in a build-only copy."""
import argparse
from pathlib import Path


def patch(source):
    replacements = [
        ('if (hw_resource.ppa_handle == NULL)',
         'if (cfg->base.profile.enable_ppa_accel && hw_resource.ppa_handle == NULL)', 1),
        ('if (hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3))',
         'if (impl->cfg.base.profile.enable_ppa_accel && hw_resource.ppa_handle && (color_bytes == 2 || color_bytes == 3))', 2),
        ('if (!hw_resource.ppa_handle)',
         'if (!impl->cfg.base.profile.enable_ppa_accel || !hw_resource.ppa_handle)', 1),
        # CPU rotation and DMA repair can modify separate pixels on the same
        # cache line. Late CPU write-back then overwrites the DMA repair.
        ('if (display_bridge_dma2d_window_is_compatible(\n                    impl->disp_fb',
         'if (impl->cfg.base.profile.enable_ppa_accel &&\n                display_bridge_dma2d_window_is_compatible(\n                    impl->disp_fb', 1),
        ('if (display_bridge_dma2d_window_is_compatible(impl->disp_fb, hor_res, offset_x',
         'if (impl->cfg.base.profile.enable_ppa_accel && display_bridge_dma2d_window_is_compatible(impl->disp_fb, hor_res, offset_x', 1),
        ('if (src_stride % bytes_per_pixel == 0 &&',
         'if (impl->cfg.base.profile.enable_ppa_accel && src_stride % bytes_per_pixel == 0 &&', 1),
        ('display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);',
         'if (impl->cfg.base.profile.enable_ppa_accel) {\n            display_cache_msync_invalidate_framebuffer(impl->draw_fb, impl->runtime.frame_buffer_size);\n        }', 2),
    ]
    for old, fixed, count in replacements:
        if source.count(fixed) == count and source.replace(fixed, '').count(old) == 0:
            continue
        elif source.count(old) == count and source.count(fixed) == 0:
            source = source.replace(old, fixed)
        else:
            raise ValueError('Unsupported PPA rotation source: refusing an ambiguous or missing patch')
    return source


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        raise SystemExit('The managed source must not be overwritten')
    result = patch(args.input.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, encoding='utf-8')
    print('Display rotation and partial-buffer repair respect CPU ownership in build-only adapter copy')
