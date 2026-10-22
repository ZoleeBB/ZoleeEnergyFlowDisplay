#!/usr/bin/env python3
"""Támogatási URL-ek, generált HTML és a valódi C-kiszolgálás ellenőrzése."""
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_support import build_header, render_card, validate_links


class CardParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.text = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == 'a':
            self.links.append(attributes)
        # Az oldal megnyitásakor egyetlen külső kapcsolat sem indulhat.
        assert tag not in ('script', 'iframe', 'img', 'form', 'link')

    def handle_data(self, data):
        self.text.append(data)


enabled = {'revolut_me_url': 'https://revolut.me/supporttest',
           'ko_fi_url': 'https://ko-fi.com/supporttest'}
empty = {key: '' for key in enabled}
for language in ('hu', 'en'):
    parser = CardParser()
    parser.feed(render_card(enabled, language))
    assert [link['href'] for link in parser.links] == list(enabled.values())
    for link in parser.links:
        assert link['target'] == '_blank'
        assert set(link['rel'].split()) == {'noopener', 'noreferrer'}
        assert link['referrerpolicy'] == 'no-referrer'
        assert link['aria-label']
    assert '{{' not in render_card(enabled, language)
    assert render_card(empty, language) == ''
    for key in enabled:
        one = CardParser()
        one.feed(render_card({**empty, key: enabled[key]}, language))
        assert len(one.links) == 1 and one.links[0]['href'] == enabled[key]

for key, good in enabled.items():
    for bad in (
        good.replace('https:', 'http:'), good + '/5', good + '?amount=5',
        good + '#payment', good.replace('/supporttest', '.evil.test/supporttest'),
        good.replace('https://', 'https://user@'), 'javascript:alert(1)',
        good + '\"><script>alert(1)</script>', None, 42,
    ):
        try:
            validate_links({**enabled, key: bad})
        except ValueError:
            pass
        else:
            raise AssertionError(f'Unsafe or non-generic support link accepted: {bad!r}')
for bad in ({}, {**empty, 'typo': ''}, [], None):
    try:
        validate_links(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid support configuration accepted')

subprocess.run([sys.executable, 'tools/prepare_web.py', '--check'], cwd=ROOT, check=True)
for configured, config in ((0, empty), (1, enabled)):
    with tempfile.TemporaryDirectory(prefix='zolee-support-') as temporary:
        directory = Path(temporary)
        # Ugyanazt a kézzel írt kiszolgálót fordítjuk valós, generált linkekkel is.
        shutil.copyfile(ROOT / 'main/app_support_web.h', directory / 'app_support_web.h')
        (directory / 'app_support_links.h').write_text(build_header(config), encoding='utf-8')
        binary = str(directory / 'test_support_web')
        subprocess.run([
            os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=all',
            f'-DSUPPORT_ENABLED={configured}', '-I' + temporary, '-Itests/stubs',
            'tests/test_support_web.c', '-o', binary,
        ], cwd=ROOT, check=True)
        subprocess.run([binary], check=True)

print('PASS: provider allowlist, generic HTTPS links, safe external navigation, no background requests')
