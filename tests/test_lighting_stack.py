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
    def run_report(self, size=1536, qualifier='static', omitted=None,
                   model_only=True, omitted_ui=None, image_size=1536,
                   omitted_service=None, command_size=1536):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lines = [f'main/app_loxone_lighting_model.inc:20:1:{name}.part.0\t{size}\t{qualifier}'
                     for name in sorted(stack.REQUIRED) if name != omitted]
            (root / 'app_loxone_model.c.su').write_text('\n'.join(lines))
            if not model_only:
                services = ('app_loxone_light_open', 'app_loxone_light_set', 'app_loxone_irc_summary',
                            'app_loxone_shading_summary', 'app_loxone_shading_command',
                            'app_loxone_shading_position')
                lines = [f'main/app_loxone_lighting_service.inc:20:1:{name}\t64\tstatic'
                         for name in services if name != omitted_service]
                lines.append(f'main/app_loxone.c:20:1:home_command_build_locked\t{command_size}\tstatic')
                (root / 'app_loxone.c.su').write_text('\n'.join(lines))
                sources = (('app_loxone_lighting_ui.inc', 'light_card_build'),
                           ('app_loxone_irc_ui.inc', 'irc_card_update'),
                           ('app_loxone_shading_ui.inc', 'shading_card_update'))
                lines = [f'main/{source}:20:1:{name}\t64\tstatic'
                         for source, name in sources if source != omitted_ui]
                lines.append('main/app_loxone_home_ui.inc:20:1:'
                             f'home_irc_icon_prepare.constprop.0\t{image_size}\tstatic')
                (root / 'app_ui.c.su').write_text('\n'.join(lines))
            with contextlib.redirect_stdout(io.StringIO()):
                stack.inspect(root, model_only=model_only)

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

    def test_irc_frame_prefixes_are_guarded(self):
        for name in ('lx_irc_block_create', 'irc_format', 'app_loxone_irc_summary',
                     'home_irc_icon_prepare'):
            self.assertTrue(stack.lighting_name(name + '.constprop.0'))

    def test_missing_irc_function_fails(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó.*lx_irc_block_event'):
            self.run_report(omitted='lx_irc_block_event')

    def test_shading_frame_prefixes_are_guarded(self):
        for name in ('lx_shading_block_create', 'shading_card_update', 'app_loxone_shading_summary',
                     'home_shading_icon_prepare', 'lx_home_shading_position_build',
                     'home_command_build_locked', 'home_command_request'):
            self.assertTrue(stack.lighting_name(name + '.constprop.0'))

    def test_missing_shading_function_fails(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó.*lx_shading_block_event'):
            self.run_report(omitted='lx_shading_block_event')

    def test_full_report_accepts_all_ui_sources(self):
        self.run_report(model_only=False)

    def test_full_report_requires_irc_ui_source(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó tényleges IRC UI-veremadat'):
            self.run_report(model_only=False, omitted_ui='app_loxone_irc_ui.inc')

    def test_full_report_requires_lighting_ui_source(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó tényleges világítási UI-veremadat'):
            self.run_report(model_only=False, omitted_ui='app_loxone_lighting_ui.inc')

    def test_full_report_requires_shading_ui_source(self):
        with self.assertRaisesRegex(ValueError, 'Hiányzó tényleges árnyékolási UI-veremadat'):
            self.run_report(model_only=False, omitted_ui='app_loxone_shading_ui.inc')

    def test_full_report_requires_shading_services(self):
        for name in ('app_loxone_shading_summary', 'app_loxone_shading_command',
                     'app_loxone_shading_position'):
            with self.assertRaisesRegex(ValueError, 'Hiányzó.*' + name):
                self.run_report(model_only=False, omitted_service=name)

    def test_command_helper_over_limit_fails(self):
        with self.assertRaisesRegex(ValueError, 'home_command_build_locked.*1537 B > 1536 B'):
            self.run_report(model_only=False, command_size=1537)

    def test_irc_image_helper_over_limit_fails(self):
        with self.assertRaisesRegex(ValueError, 'home_irc_icon_prepare.*1537 B > 1536 B'):
            self.run_report(model_only=False, image_size=1537)

    def test_windows_source_path(self):
        record = stack.parse_record('C:/source/model.inc:12:3:lx_light_queue\t112\tstatic')
        self.assertEqual(record[0], 'C:/source/model.inc')
        self.assertEqual(record[2], 112)

    def test_invalid_record_fails(self):
        with self.assertRaises(ValueError):
            stack.parse_record('unexpected compiler output')


if __name__ == '__main__':
    unittest.main()
