#!/usr/bin/env python3
"""A fordítói kapu utasítsa el a bootkeret bent maradását és a hiányos mérést."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_audio_stack as stack


class AudioStackGate(unittest.TestCase):
    def fixture(self, root, main_frame=16, boot=True, qualifier='static'):
        functions = {
            'app_main.c': [('app_main', main_frame)] + ([('app_boot', 640)] if boot else []),
            'app_loxone_audio_runtime.c': [('app_loxone_audio_run', 192)],
            'app_loxone_audio_auth.c': [('lx_audio_auth_create', 736)],
            'app_loxone_audio_media.c': [('app_loxone_audio_media_request', 128), ('media_worker_step', 192)],
            'app_loxone_audio_image.c': [('lx_audio_image_decode', 64)],
            'app_ota_auto.c': [('ota_worker', 64)],
        }
        for unit, entries in functions.items():
            (root / (unit + '.su')).write_text(''.join(
                f'/source/{unit}:10:1:{name}\t{size}\t{qualifier}\n'
                for name, size in entries), encoding='utf-8')

    def inspect(self, root, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            stack.inspect(root, **kwargs)

    def test_complete_bounded_compiler_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, qualifier='dynamic,bounded')
            self.inspect(root)

    def test_boot_frame_cannot_remain_under_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, main_frame=640)
            with self.assertRaisesRegex(ValueError, 'app_main: 640 B > 256 B'):
                self.inspect(root)
            self.fixture(root, boot=False)
            with self.assertRaisesRegex(ValueError, 'Hiányzó fordítói veremadat: app_boot'):
                self.inspect(root)

    def test_incomplete_or_unbounded_report_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, qualifier='dynamic')
            with self.assertRaisesRegex(ValueError, 'nem korlátos veremkeret'):
                self.inspect(root)
            self.fixture(root)
            (root / 'app_loxone_audio_runtime.c.su').unlink()
            with self.assertRaisesRegex(ValueError, 'Hiányzó fordítási egység'):
                self.inspect(root)

    def test_slave_ota_has_no_required_audio_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'app_ota_auto.c.su').write_text(
                '/source/app_ota_auto.c:10:1:ota_worker\t64\tstatic\n', encoding='utf-8')
            self.inspect(root, ota_only=True)

    def test_media_callback_must_have_compiler_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'app_loxone_audio_media.c.su').write_text(
                '/source/app_loxone_audio_media.c:10:1:app_loxone_audio_media_request\t128\tstatic\n',
                encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Hiányzó fordítói veremadat: media_worker_step'):
                self.inspect(root)


if __name__ == '__main__':
    unittest.main()
