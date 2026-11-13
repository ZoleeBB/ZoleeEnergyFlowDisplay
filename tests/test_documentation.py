#!/usr/bin/env python3
"""Ellenőrzi a helyi hivatkozásokat és a teljes dokumentáció elérhetőségét."""
from html import unescape
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
# Az új navigáció és az áthelyezett README-fejezetek horgonyait is védjük.
# A régi műszaki naplók változatlan tartalmát nem írjuk át e rendezés miatt.
MOVED = {'docs/project-overview.md', 'docs/first-start.md',
         'docs/developer-checks.md', 'docs/documentation-structure.md'}


def prose(text):
    """A kódpélda nem élő hivatkozás; a ``` és ~~~ kerítés is támogatott."""
    lines = []
    fence = None
    for line in text.splitlines():
        mark = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if mark:
            token = mark.group(1)
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            continue
        if fence is None:
            lines.append(line)
    return '\n'.join(lines)


def targets(text):
    body = prose(text)
    result = re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', body)
    result += re.findall(r'\b(?:href|src)\s*=\s*[\"\']([^\"\']+)[\"\']', body)
    # A nevesített Markdown-hivatkozások definíciója is kap ellenőrzést.
    result += re.findall(r'^\s{0,3}\[[^\]\n]+\]:\s*(\S+)', body, flags=re.M)
    return [unescape(item.strip().split(' "', 1)[0].strip('<>')) for item in result]


def anchors(text):
    body = prose(text)
    result = set(re.findall(r'\b(?:id|name)=[\"\']([^\"\']+)[\"\']', body))
    used = set()
    for heading in re.findall(r'^\s{0,3}#{1,6}\s+(.+?)\s*#*$', body, flags=re.M):
        heading = re.sub(r'<[^>]*>', '', unescape(heading))
        heading = re.sub(r'!?\[([^\]]+)\]\([^)]+\)', r'\1', heading)
        slug = re.sub(r'[^\w\- ]', '', heading.lower()).replace(' ', '-')
        name, suffix = slug, 0
        while name in used:
            suffix += 1
            name = f'{slug}-{suffix}'
        used.add(name)
        result.add(name)
    return result


def validate(root, names):
    """Git és offline USB-dokumentáció esetén is használható ellenőrzés."""
    root = Path(root).resolve()
    documents = {name: (root / name).read_text(encoding='utf-8')
                 for name in sorted(set(names))
                 if name.endswith('.md') and (root / name).is_file()}
    graph = {name: set() for name in documents}
    failures, count, anchor_count = [], 0, 0
    for name, text in documents.items():
        source = root / name
        strict = name == 'README.md' or name.endswith('/README.md') or name in MOVED
        for target in targets(text):
            url = urlsplit(target)
            if url.scheme or url.netloc:
                continue
            path = unquote(url.path)
            destination = ((root / path.lstrip('/')) if path.startswith('/') else
                           (source.parent / path)) if path else source
            destination = destination.resolve()
            if not destination.is_relative_to(root):
                failures.append(f'{name}: repository escape: {target}')
                continue
            count += 1
            if not destination.exists():
                failures.append(f'{name}: missing local target: {target}')
                continue
            if destination.is_dir() and (destination / 'README.md').is_file():
                destination /= 'README.md'
            relative = destination.relative_to(root).as_posix()
            if relative in graph:
                graph[name].add(relative)
                if strict and url.fragment:
                    anchor_count += 1
                    if unquote(url.fragment) not in anchors(documents[relative]):
                        failures.append(f'{name}: missing heading: {target}')
    # Nem egyetlen lapos README-listát követelünk: az indexeken át is
    # elérhető minden leírás, a mélyebb könyvtárak Markdown-oldalai is.
    visited, pending = set(), ['README.md']
    while pending:
        name = pending.pop()
        if name not in visited:
            visited.add(name)
            pending.extend(graph.get(name, set()) - visited)
    missing = sorted(name for name in documents if name.startswith('docs/') and name not in visited)
    failures.extend('Unreachable documentation: ' + name for name in missing)
    if failures:
        raise AssertionError('\n'.join(failures))
    return len(documents), count, anchor_count, len([n for n in visited if n.startswith('docs/')])


class DocumentationGraphTests(unittest.TestCase):
    def check(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            return validate(root, files)

    def test_nested_indexes_and_cycles(self):
        result = self.check({'README.md': '[Docs](docs/README.md)',
            'docs/README.md': '[Group](group/README.md)',
            'docs/group/README.md': '[Leaf](leaf.md) [Back](../README.md)',
            'docs/group/leaf.md': '# Leaf'})
        self.assertEqual(result[3], 3)

    def test_unlinked_nested_page_fails(self):
        with self.assertRaisesRegex(AssertionError, 'Unreachable'):
            self.check({'README.md': '# Root', 'docs/nested/forgotten.md': '# Lost'})

    def test_broken_file_link_fails(self):
        with self.assertRaisesRegex(AssertionError, 'missing local target'):
            self.check({'README.md': '[Missing](docs/absent.md)'})

    def test_html_image_link_fails(self):
        with self.assertRaisesRegex(AssertionError, 'missing local target'):
            self.check({'README.md': '<img src="missing.png">'})

    def test_unicode_and_duplicate_heading_anchors(self):
        self.check({'README.md': '[A](docs/guide.md#éjszakai-mód-1)',
                    'docs/guide.md': '# Éjszakai mód\n\n## Éjszakai mód\n'})
        with self.assertRaisesRegex(AssertionError, 'missing heading'):
            self.check({'README.md': '[A](docs/guide.md#not-there)', 'docs/guide.md': '# Guide'})

    def test_fenced_examples_are_not_links(self):
        self.check({'README.md': '```md\n[example](missing.md)\n```\n~~~\n![x](missing.png)\n~~~'})

    def test_directory_link_opens_readme(self):
        self.check({'README.md': '[Docs](docs/)', 'docs/README.md': '# Docs'})

    def test_reference_link_is_followed(self):
        self.check({'README.md': '[Docs][manual]\n\n[manual]: docs/guide.md',
                    'docs/guide.md': '# Guide'})

    def test_repository_escape_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'repository escape'):
            self.check({'README.md': '[outside](../../outside.md)'})


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(DocumentationGraphTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    tracked = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '*.md'],
        cwd=ROOT).decode().split('\0')
    docs, links, headings, reached = validate(ROOT, filter(None, tracked))
    print(f'PASS: {links} local links, {headings} navigation anchors, '
          f'all {reached} docs/ pages reachable; {docs} Markdown files checked')
