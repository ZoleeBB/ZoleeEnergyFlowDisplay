#!/usr/bin/env python3
"""Make adapter 0.6.4 respect the disabled PPA profile, in a build-only copy."""
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
    ]
    for old, fixed, count in replacements:
        if source.count(old) == count and source.count(fixed) == 0:
            source = source.replace(old, fixed)
        elif source.count(old) == 0 and source.count(fixed) == count:
            continue
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
    print('Display rotation respects the PPA profile in build-only adapter copy')
