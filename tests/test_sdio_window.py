#!/usr/bin/env python3
"""Compile actual driver replacement blocks; fault-inject DMA fragmentation/OOM."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('rx',root/'tools/patch_hosted_rx.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
r=m.REPLACEMENTS
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGW(...) ((void)0)
#define H_SDIO_HOST_RX_MODE 1
#define H_SDIO_HOST_STREAMING_MODE 1
#define DMA_ATTR
#define ESP_SLAVE_CMD53_END_ADDR 0x1f800
#define ACQUIRE_LOCK 0
static uint8_t fifo[32768], *stored;
static size_t cursor, available, capacity;
static int fail_alloc, calls, fail_read=-1;
static void *heap_caps_malloc(size_t size,unsigned caps){
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 return fail_alloc?NULL:malloc(size);
}
static void heap_caps_free(void *p){free(p);}
''' + r[4][1].split('#include "esp_attr.h"\n')[1] + r'''
static uint8_t *storage(size_t len){uint8_t **buf=&stored;
 if(len>capacity){
''' + r[5][1] + r'''
 capacity=len;
 }return stored;
}
static int read_block(void *handle,unsigned addr,uint8_t *p,size_t n,int lock){
 (void)handle;(void)lock;
 size_t valid=ESP_SLAVE_CMD53_END_ADDR-addr;
 assert(p==sdio_rx_dma_scratch && ((uintptr_t)p%64)==0);
 assert(valid>0&&valid<=1024&&n<=1024&&n>=valid);
 assert(cursor+valid<=available);
 if(calls==fail_read)return -1;
 memcpy(p,fifo+cursor,valid);memset(p+valid,0,n-valid);
 cursor+=valid;calls++;return 0;
}
static struct{int (*_h_sdio_read_block)(void*,unsigned,uint8_t*,size_t,int);} funcs={read_block};
static struct{typeof(funcs)*funcs;} g_h={&funcs};
static void transfer(size_t len){
 uint8_t *rxbuff=storage(len), *pos=rxbuff;size_t data_left=len,len_to_read;int ret=0;
 void *sdio_handle=NULL;
 do{
''' + r[7][1] + r'''
#if PADDED
 size_t block_read_len=(len_to_read+511)/512*512;
 ret=g_h.funcs->_h_sdio_read_block(sdio_handle,
''' + r[8][1] + r''',ACQUIRE_LOCK);
#else
 ret=g_h.funcs->_h_sdio_read_block(sdio_handle,
''' + r[9][1] + r''',ACQUIRE_LOCK);
#endif
 if(ret)break;
''' + r[10][1] + r'''
 }while(data_left);
 size_t len_from_slave=len,sdio_rx_byte_count=0;
''' + r[12][1] + r'''
 assert(sdio_rx_byte_count==cursor);
 if(!ret)assert(cursor==len);
 if(rxbuff&&!ret)assert(memcmp(rxbuff,fifo,len)==0);
}
static void run(size_t len){
 for(size_t i=0;i<len;i++)fifo[i]=(uint8_t)(i*31+7);
 cursor=0;available=len;calls=0;transfer(len);
 assert(calls==(int)((len+1023)/1024));
}
int main(void){
 run(3072);run(5632);run(5633);run(7);run(32768);
 cursor=0;available=5632;calls=0;fail_read=2;transfer(5632);assert(cursor==2048);fail_read=-1;
 uint8_t *old=stored;size_t oldcap=capacity;
 fail_alloc=1;assert(storage(oldcap+1)==NULL);assert(stored==old&&capacity==oldcap);
 free(stored);stored=NULL;capacity=0;
 run(5632);assert(stored==NULL); /* OOM must drain FIFO without touching NULL. */
 fail_alloc=0;run(5632);assert(stored);free(stored);
 puts("PASS: bounded aligned SDIO DMA, exact FIFO bytes, padded tail, OOM drain and recovery");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 for padded in (0,1):
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined',f'-DPADDED={padded}',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
