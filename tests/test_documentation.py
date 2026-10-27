#!/usr/bin/env python3
"""Check local Markdown links against the checked-out repository."""
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '*.md'], cwd=ROOT).decode().split('\0')
failures = []
count = 0
for name in filter(None, tracked):
    source = ROOT / name
    if not source.exists():
        continue
    text = re.sub(r'```.*?```', '', source.read_text(), flags=re.S)
    for target in re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', text):
        target = target.strip().split(' "', 1)[0].strip('<>')
        url = urlsplit(target)
        if url.scheme or url.netloc or not url.path:
            continue
        path = unquote(url.path)
        resolved = (ROOT / path.lstrip('/')) if path.startswith('/') else (source.parent / path)
        count += 1
        if not resolved.exists():
            failures.append(f'{name}: {target}')
if failures:
    raise AssertionError('Broken local documentation links:\n' + '\n'.join(failures))

# Minden dokumentum érhető el a README-ből: új leírás ne maradjon hivatkozás nélkül.
readme = (ROOT / 'README.md').read_text()
linked = set(re.findall(r'docs/[A-Za-z0-9._-]+\.md', readme))
missing = sorted(f'docs/{path.name}' for path in sorted((ROOT / 'docs').glob('*.md'))
                 if f'docs/{path.name}' not in linked)
if missing:
    raise AssertionError('Documents missing from README.md:\n' + '\n'.join(missing))
print(f'PASS: {count} local Markdown links resolve in {sum(bool(n) for n in tracked)} tracked documents')
