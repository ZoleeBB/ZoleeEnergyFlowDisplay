#!/usr/bin/env python3
"""Create a build-only copy fixing the adapter's hardcoded internal partial buffer."""
import argparse
from pathlib import Path
import re


def patch(source):
    prefix = r'display_manager_alloc_draw_buffer\(\s*cfg->draw_buf_pixels\s*\*\s*color_size\s*,\s*'
    suffix = r'\s*,\s*false\s*\)'
    old = prefix + 'false' + suffix
    fixed = prefix + r'profile->use_psram' + suffix
    if 'case ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL' not in source:
        raise ValueError('Unsupported adapter: triple-partial allocation code not found')
    matches = list(re.finditer(old, source))
    if len(matches) == 1:
        match = matches[0]
        call = source[match.start():match.end()]
        call = re.sub(r',\s*false\s*,', ', profile->use_psram,', call, count=1)
        return source[:match.start()] + call + source[match.end():]
    if not matches and len(list(re.finditer(fixed, source))) == 1:
        return source  # Upstream already corrected this exact allocation.
    raise ValueError('Unsupported adapter allocation: refusing an ambiguous or missing patch')


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
    print('PSRAM partial display allocation configured in build-only adapter copy')
