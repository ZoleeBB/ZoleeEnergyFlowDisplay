#!/usr/bin/env python3
"""Validate a release tag before ESP-IDF can truncate its application version."""
import argparse
import re
from pathlib import Path


def validate_tag(tag):
    number = r'(?:0|[1-9][0-9]*)'
    identifier = r'(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
    pattern = rf'v{number}\.{number}\.{number}(?:-{identifier}(?:\.{identifier})*)?'
    if not tag.isascii() or len(tag) > 31 or not re.fullmatch(pattern, tag):
        raise ValueError('Tag must be vMAJOR.MINOR.PATCH[-prerelease], at most 31 ASCII characters')
    return tag


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--output', default='version.txt')
    args = parser.parse_args()
    Path(args.output).write_text(validate_tag(args.tag) + '\n', encoding='ascii')
