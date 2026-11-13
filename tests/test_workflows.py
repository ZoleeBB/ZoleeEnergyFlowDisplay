#!/usr/bin/env python3
"""Check the workflow graph, artifact handoff and trust boundaries as contracts."""
from pathlib import Path
import struct
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflows():
    # A GitHub YAML 1.2-t értelmez; BaseLoader nem alakítja az `on` kulcsot
    # a PyYAML 1.1-es szabályával True értékké. A skalárok itt szövegek.
    return {p.name: yaml.load(p.read_text(), Loader=yaml.BaseLoader)
            for p in (ROOT / '.github/workflows').glob('*.yml')}


class WorkflowContractTests(unittest.TestCase):
    def setUp(self):
        self.flows = workflows()
        self.shared = self.flows['ci-shared.yml']

    def test_one_entrypoint_per_event_and_no_branch_push_duplicate(self):
        self.assertEqual(set(self.flows), {'ci-checks.yml', 'ci-shared.yml', 'release-main.yml', 'release-manual.yml'})
        self.assertEqual(set(self.flows['ci-checks.yml']['on']), {'pull_request', 'workflow_dispatch'})
        self.assertEqual(self.flows['ci-checks.yml']['on']['pull_request']['branches'], ['main'])
        self.assertEqual(self.flows['release-main.yml']['on'], {'push': {'branches': ['main']}})
        self.assertEqual(self.flows['release-manual.yml']['on'], {'release': {'types': ['published']}})
        self.assertEqual(set(self.shared['on']), {'workflow_call'})
        self.assertFalse(any('pull_request_target' in f['on'] for f in self.flows.values()))

    def test_latest_pr_cancels_but_main_merges_are_queued(self):
        pr = self.flows['ci-checks.yml']['concurrency']
        main = self.flows['release-main.yml']['concurrency']
        self.assertEqual(pr['cancel-in-progress'], 'true')
        self.assertNotIn('queue', pr)
        self.assertEqual(main['cancel-in-progress'], 'false')
        self.assertEqual(main['queue'], 'max')
        self.assertNotEqual(pr['group'], main['group'])

    def test_every_caller_pins_source_and_matches_callee_permissions(self):
        for name, flow in self.flows.items():
            for job in flow['jobs'].values():
                if job.get('uses') == './.github/workflows/ci-shared.yml':
                    self.assertEqual(job['with']['source_sha'], '${{ github.sha }}', name)
                    for permission, level in self.shared['permissions'].items():
                        self.assertEqual(flow['permissions'][permission], level, (name, permission))
        for job in self.shared['jobs'].values():
            for step in job.get('steps', []):
                if step.get('uses') == 'actions/checkout@v4' and not step.get('with', {}).get('repository'):
                    self.assertEqual(step['with']['ref'], '${{ inputs.source_sha }}')

    def test_four_build_outputs_and_exact_release_handoff(self):
        uploads = [(job, step) for job in self.shared['jobs'].values()
                   for step in job.get('steps', []) if step.get('uses') == 'actions/upload-artifact@v4']
        # Csak a négy buildkimenet tölthető fel; a dokumentációs előnézetek
        # verziókezelt képek, nem lejáró diagnosztikai csomagok.
        names = [s['with']['name'] for _, s in uploads]
        self.assertEqual(len(names), 4)
        self.assertEqual(set(names), {
            'zolee-firmware-${{ inputs.source_sha }}',
            'zolee-templates-${{ inputs.source_sha }}',
            'zolee-simulator-master-windows-${{ inputs.source_sha }}',
            'zolee-simulator-slave-4b-windows-${{ inputs.source_sha }}',
        })
        for _, step in uploads:
            self.assertEqual(step['with']['if-no-files-found'], 'error')
        firmware = next(s for _, s in uploads if s['with']['name'].startswith('zolee-firmware-'))
        self.assertEqual(firmware['with']['overwrite'], 'false')
        slave = self.shared['jobs']['simulator-slave']
        self.assertEqual(slave['strategy']['matrix']['os'], ['ubuntu-latest', 'windows-latest'])
        upload = next(s for s in slave['steps'] if s.get('uses') == 'actions/upload-artifact@v4')
        self.assertEqual(upload['if'], "matrix.os == 'windows-latest'")
        for name in ['release-main.yml', 'release-manual.yml']:
            downloads = [s for j in self.flows[name]['jobs'].values()
                         for s in j.get('steps', []) if s.get('uses') == 'actions/download-artifact@v4']
            self.assertEqual([s['with']['name'] for s in downloads], ['zolee-firmware-${{ github.sha }}'])

    def test_lighting_render_and_memory_checks_remain_without_upload(self):
        steps = self.shared['jobs']['ui-preview']['steps']
        self.assertFalse(any(s.get('uses') == 'actions/upload-artifact@v4' for s in steps))
        render = next(s for s in steps if s.get('name') == 'Render actual LVGL screens')
        lines = {line.strip() for line in render['run'].splitlines()}
        self.assertIn('ZOLEE_LIGHTING_ONLY=1 ../.preview-build/ui_preview', lines)
        self.assertIn('ZOLEE_LIGHTING_ONLY=1 ZOLEE_PREVIEW_RGB565=1 ../.preview-build/ui_preview', lines)
        self.assertEqual(render['env']['ASAN_OPTIONS'], 'detect_leaks=1:halt_on_error=1')
        self.assertTrue(any('-DZOLEE_PREVIEW_ASAN=ON' in s.get('run', '') for s in steps))

    def test_lighting_documentation_uses_complete_static_full_resolution_gallery(self):
        folder = ROOT / 'docs/images/lighting'
        names = {
            'lighting-cards-day', 'lighting-circuits-day', 'lighting-color-day',
            'lighting-color-night', 'lighting-white-day',
        }
        self.assertEqual({p.stem for p in folder.glob('*.png')}, names)
        guide = (ROOT / 'docs/lighting-controller.md').read_text(encoding='utf-8')
        readme = (ROOT / 'README.md').read_text(encoding='utf-8')
        for name in names:
            with self.subTest(image=name):
                header = (folder / (name + '.png')).read_bytes()[:33]
                self.assertEqual(header[:8], bytes.fromhex('89504e470d0a1a0a'))
                self.assertEqual(header[12:16], b'IHDR')
                self.assertEqual(struct.unpack('>II', header[16:24]), (1280, 800))
                self.assertIn('(images/lighting/' + name + '.png)', guide)
        for name in ('lighting-cards-day', 'lighting-color-day'):
            self.assertIn('(docs/images/lighting/' + name + '.png)', readme)
        self.assertTrue((folder / 'README.md').is_file())

    def test_validation_gates_the_expensive_firmware_and_publication(self):
        jobs = self.shared['jobs']
        validation = set(jobs) - {'prepare', 'build'}
        self.assertEqual(set(jobs['prepare']['needs']), validation)
        self.assertEqual(jobs['build']['needs'], 'prepare')
        self.assertEqual(jobs['build']['if'], "needs.prepare.outputs.build == 'true'")
        publish = self.flows['release-main.yml']['jobs']['publish']
        self.assertEqual(publish['needs'], 'verify')
        self.assertIn("github.event_name == 'push'", publish['if'])
        self.assertIn("github.ref == 'refs/heads/main'", publish['if'])
        self.assertIn("needs.verify.outputs.publish == 'true'", publish['if'])
        for name, flow in self.flows.items():
            for job_id, job in flow['jobs'].items():
                if job.get('permissions', {}).get('contents') == 'write':
                    self.assertIn(name, ['release-main.yml', 'release-manual.yml'])
                    self.assertEqual(job_id, 'publish')

    def test_browser_installation_has_room_for_slow_system_packages(self):
        job = self.shared['jobs']['web-editor']
        steps = {step.get('name'): step for step in job['steps']}
        runtime = steps['Install pinned browser test runtime']
        dependencies = steps['Install Chromium system dependencies']
        browser = steps['Install Chromium headless shell']
        install_budget = sum(int(step['timeout-minutes'])
                             for step in (runtime, dependencies, browser))
        self.assertGreaterEqual(int(job['timeout-minutes']), install_budget + 3)
        self.assertIn('playwright@1.62.1', runtime['run'])
        self.assertIn('install-deps chromium', dependencies['run'])
        self.assertIn('install --only-shell chromium', browser['run'])
        scripts = [step.get('run', '') for step in job['steps']]
        self.assertIn('node tests/test_slave_presence_browser.js', scripts)
        self.assertIn('node tests/test_support_browser.js', scripts)
        self.assertFalse(any('continue-on-error' in step for step in job['steps']))


if __name__ == '__main__':
    unittest.main()
