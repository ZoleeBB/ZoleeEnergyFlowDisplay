#!/usr/bin/env python3
"""Host-check the complete HTTP Pico C sample; does not emulate Loxone networking."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / 'examples/loxone-energy-http-post.picoc'
source = r'''
#include <stdio.h>
#include <assert.h>
static int iteration, sent, slept;
static float inputs[12] = {-3.8f,-1.2f,7.1f,82,2.1f,18.6f,12.3f,1,0,1,0,27};
static int next_iteration(void) { return ++iteration <= 4; }
static float getinput(int index) {
    assert(index>=0 && index<12);
    /* Invalid status must not be rounded or silently masked by the sender. */
    return index==9 && iteration>2 ? 2.5f : inputs[index];
}
static void setoutputtext(int index,const char *text) {
    assert(index==0); ++sent; puts(text);
}
static void sleep(int milliseconds) { assert(milliseconds==5000); ++slept; }
#define TRUE next_iteration()
static void run_sample(void) {
#include "SAMPLE_PATH"
}
int main(void) { run_sample(); assert(sent==4 && slept==4); return 0; }
'''.replace('SAMPLE_PATH', SAMPLE.as_posix())

with tempfile.TemporaryDirectory(prefix='zolee-http-picoc-') as directory:
    folder = Path(directory)
    (folder/'sample.c').write_text(source)
    subprocess.run([os.environ.get('CC','cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    str(folder/'sample.c'), '-o', str(folder/'sample')], check=True)
    lines = subprocess.check_output([str(folder/'sample')], text=True, timeout=10).splitlines()
    data = [json.loads(line) for line in lines]
    reference = json.loads((ROOT/'examples/mqtt-payload.sample.json').read_text())
    assert all(set(item)==set(reference) and len(item)==12 for item in data)
    assert data[0]==data[1] and lines[0]!=lines[1], 'constant inputs must produce a text change'
    assert lines[0].startswith('{"') and lines[1].startswith('{ "')
    assert data[0]['PowerGrid']<0 and data[0]['PowerBat']<0
    assert data[0]['Status']==1 and data[2]['Status']==2.5 and data[2]==data[3]
    assert all(len(line.encode())<4096 for line in lines)
    print('PASS: HTTP Pico C sample, 12 fields, 5-second cycle and constant-input text changes')
