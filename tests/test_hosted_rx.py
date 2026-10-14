#!/usr/bin/env python3
"""Fault-inject the patched streaming RX block: no assert, leak, or invalid copy."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hosted_patch', ROOT/'tools/patch_hosted_rx.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original = '\n'.join(old for old, new in module.REPLACEMENTS)
fixed = module.patch(original)
assert module.patch(fixed) == fixed
try:
    module.patch(original.replace('assert(pkt_rxbuff);', 'assert(other);'))
except ValueError:
    pass
else:
    raise AssertionError('Unknown driver source must fail closed')

# Exercise the actual replacement block with allocator/queue fault injection.
block = module.REPLACEMENTS[2][1]
network_block = module.REPLACEMENTS[3][1]
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int fail_alloc, fail_queue, allocations, outstanding, queued;
static uint8_t expected[64];
static void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    ++allocations;
    if(fail_alloc)return NULL;
    ++outstanding;return calloc(n,size);
}
static void heap_caps_free(void *p){assert(p);--outstanding;free(p);}
static int sdio_push_pkt_to_queue(uint8_t *p,uint16_t len,uint16_t offset){
    assert(memcmp(p,expected,len+offset)==0);
    if(fail_queue)return -1;
    ++queued;heap_caps_free(p);return 0;
}
static int receive(uint8_t *buf,uint32_t buf_len,uint16_t len,uint16_t offset){
    uint8_t *pkt_rxbuff=NULL;uint32_t packet_size;
''' + block + r'''
    return 0;
}
static int network_receive(int fail){
    struct packet { void *priv_buffer_handle;void (*free_buf_handle)(void *);int payload_len; } packet;
    struct packet *buf_handle=&packet;
    buf_handle->priv_buffer_handle=heap_caps_calloc(1,64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    buf_handle->free_buf_handle=heap_caps_free;buf_handle->payload_len=48;
    for(int i=0;i<1;i++){
        void *copy_payload=fail?NULL:malloc(48);
#define H_FREE_PTR_WITH_FUNC(fn,p) do{fn(p);p=NULL;}while(0)
''' + network_block + r'''

        free(copy_payload);
        H_FREE_PTR_WITH_FUNC(buf_handle->free_buf_handle,buf_handle->priv_buffer_handle);
    }
    return outstanding;
}
int main(void){
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(uint8_t)i;
    assert(receive(expected,64,48,16)==0);
    assert(queued==1&&outstanding==0);
    fail_alloc=1;
    assert(receive(expected,64,48,16)==ESP_ERR_NO_MEM);
    assert(queued==1&&outstanding==0);
    fail_alloc=0;fail_queue=1;
    assert(receive(expected,64,48,16)==0);
    assert(outstanding==0&&queued==1);
    int prior=allocations;
    assert(receive(expected,32,48,16)==ESP_FAIL);
    assert(allocations==prior&&outstanding==0);
    fail_queue=0;
    assert(receive(expected,64,48,16)==0);
    assert(queued==2&&outstanding==0);
    assert(network_receive(1)==0);assert(network_receive(0)==0);
    puts("PASS: PSRAM streaming RX, allocation/queue failures, malformed size and recovery");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.c').write_text(source)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                    str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
