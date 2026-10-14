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
struct profile { bool enable_ppa_accel; };
struct config { struct { struct profile profile; } base; };
struct impl { struct config cfg; };
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
    return 0;
}
'''
fixed = module.patch(source)
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
    for name, code in [('original', source), ('fixed', fixed)]:
        path = tmp/(name+'.c'); path.write_text(code)
        binary = tmp/name
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all', str(path), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        assert (result.returncode == 0) == (name == 'fixed')
print('PASS: original ignores disabled PPA; corrected registration, both rotation paths and cache fallback honor profile')
