#!/usr/bin/env python3
"""A veremellenőr ne fogadjon el hiányzó vagy túl nagy fordítói adatot."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lighting_stack', ROOT / 'tools/check_lighting_stack.py')
stack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stack)


class StackReportTests(unittest.TestCase):
    def run_report(self, size=1536, qualifier='static', omitted=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lines = [f'main/app_loxone_lighting_model.inc:20:1:{name}.part.0\t{size}\t{qualifier}'
                     for name in sorted(stack.REQUIRED) if name != omitted]
            (root / 'app_loxone_model.c.su').write_text('\n'.join(lines))
            with contextlib.redirect_stdout(io.StringIO()):
                stack.inspect(root, model_only=True)

    def test_limit_and_optimized_symbol(self):
        self.run_report()

    def test_one_byte_over_limit_fails(self):
        with self.assertRaisesRegex(ValueError, '1537 B > 1536 B'):
            self.run_report(size=1537)

    def test_bounded_dynamic_stack_is_a_maximum(self):
        self.run_report(qualifier='dynamic,bounded')
        with self.assertRaisesRegex(ValueError, '1537 B > 1536 B'):
            self.run_report(size=1537, qualifier='dynamic,bounded')

    def test_unbounded_dynamic_stack_fails(self):
        with self.assertRaisesRegex(ValueError, 'nem korlátos'):
            self.run_report(qualifier='dynamic')

    def test_unknown_qualifier_fails(self):
        with self.assertRaisesRegex(ValueError, 'ismeretlen'):
            self.run_report(qualifier='unknown')

    def test_missing_function_fails(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó'):
            self.run_report(omitted='lx_light_block_event')

    def test_missing_reports_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'Hiányzó'):
                with contextlib.redirect_stdout(io.StringIO()):
                    stack.inspect(Path(directory))

    def test_windows_source_path(self):
        record = stack.parse_record('C:/source/model.inc:12:3:lx_light_queue\t112\tstatic')
        self.assertEqual(record[0], 'C:/source/model.inc')
        self.assertEqual(record[2], 112)

    def test_invalid_record_fails(self):
        with self.assertRaises(ValueError):
            stack.parse_record('unexpected compiler output')


if __name__ == '__main__':
    unittest.main()
