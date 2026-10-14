#!/usr/bin/env python3
"""Compile starvation and TX exhaustion reproducers; verify image working-set budget."""
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py')
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
scheduler=module('patch_display_scheduler');tx=module('patch_hosted_tx')
for patcher,source in [(scheduler,scheduler.OLD),(tx,tx.OLD+'\n'+tx.OLD)]:
    assert patcher.patch(patcher.patch(source))==patcher.patch(source)
    try:patcher.patch(source.replace('pdTRUE','pdFALSE') if patcher is scheduler else tx.OLD)
    except ValueError:pass
    else:raise AssertionError('Unsupported source must fail closed')
code=r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERR_ESP_NETIF_TX_FAILED 99
static int idle_runs,locked;
static void vTaskDelay(unsigned ticks){assert(!locked&&ticks>=1);++idle_runs;}
static void ulTaskNotifyTake(int clear,unsigned wait){(void)clear;(void)wait;/* Pending notify: no sleep. */}
static void original_cycle(void){unsigned task_delay_ms=1;
''' + scheduler.OLD + r'''
}
static void fixed_cycle(void){unsigned task_delay_ms=1;
''' + scheduler.patch(scheduler.OLD) + r'''
}
static int copies;
static int send_packet(void *copy_buff){
''' + tx.NEW + r'''
    ++copies;return 0;
}
int main(void){
    for(int i=0;i<100;i++)original_cycle();assert(idle_runs==0);
    for(int i=0;i<100;i++)fixed_cycle();assert(idle_runs==100);
    assert(send_packet(NULL)==ESP_ERR_ESP_NETIF_TX_FAILED&&copies==0&&errno==ENOBUFS);
    assert(send_packet(&copies)==0&&copies==1);
    puts("PASS: pending notifications cannot suppress idle ticks; TX OOM returns error before copy");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                    '-fsanitize=undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
config=(ROOT/'sdkconfig.defaults').read_text()
cache=int(re.search(r'^CONFIG_LV_CACHE_DEF_SIZE=(\d+)$',config,re.M)[1])
working_sets=[]
for descriptor in (ROOT/'templates').glob('*/template.json'):
    doc=json.loads(descriptor.read_text())
    for mode in ('light','dark'):
        # All referenced variants together are a conservative upper bound.
        scene=json.dumps(doc[mode]);total=0
        for image in descriptor.parent.glob('*.png'):
            if image.name in scene:
                w,h=struct.unpack('>II',image.read_bytes()[16:24]);total+=w*h*4
        working_sets.append(total)
assert 6*1024*1024<max(working_sets)<cache
assert 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0' in config
assert 'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536' in config
assert '(10 * 1024 * 1024)' in (ROOT/'tests/preview/lv_conf.h').read_text()
print(f'PASS: maximum template image working set {max(working_sets)} fits {cache}-byte cache; DMA reserve protected')
