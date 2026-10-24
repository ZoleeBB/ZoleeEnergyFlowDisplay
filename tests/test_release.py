#!/usr/bin/env python3
"""Exercise packaging and publication without creating a real GitHub release."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/release'))
from version import validate_tag
from package import package
from flash_usb import command, ESPTOOL_VERSION, DEFAULT_BAUD
import flash_usb
from publish import publish


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root/'build'
        self.out = self.root/'dist'
        self.build.mkdir()
        self.tag = 'v1.2.3-rc.1'
        self.sha = 'a'*40
        data = bytearray(1024)
        data[0] = 0xE9
        struct.pack_into('<I', data, 32, 0xABCD5432)
        data[48:48+len(self.tag)] = self.tag.encode()
        self.app = bytes(data)
        # Deliberately non-default addresses catch guessed or fixed offsets.
        self.flash = {'0x3000':'bootloader/bootloader.bin', '0xa000':'partition_table/partition-table.bin',
                      '0x17000':'ota_data_initial.bin', '0x30000':'app.bin'}
        for name in self.flash.values():
            path = self.build/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(self.app if name=='app.bin' else b'flash-image')
        self.manifest = {'flash_files':self.flash, 'write_flash_args':['--flash_mode','dio','--flash_size','32MB','--flash_freq','80m'],
                         'flash_settings':{'flash_mode':'dio','flash_size':'32MB','flash_freq':'80m'},
                         'extra_esptool_args':{'chip':'esp32p4','before':'default_reset','after':'hard_reset','stub':False}}
        (self.build/'flasher_args.json').write_text(json.dumps(self.manifest))
        (self.build/'project_description.json').write_text(json.dumps({'target':'esp32p4','project_version':self.tag,'app_bin':'app.bin','git_revision':'v5.5.4'}))

    def pack(self):
        package(self.build,self.out,self.tag,self.sha)

    def test_version(self):
        for tag in ['v1.0.0','v0.0.0-ci','v1.2.3-rc.1']:
            self.assertEqual(validate_tag(tag),tag)
        for tag in ['1.0.0','v01.0.0','v1.0.0-01','v1.0.0-'+('a'*25),'v1.0.0; echo bad','v1.0.0-á']:
            with self.assertRaises(ValueError):validate_tag(tag)

    def test_portable_complete_and_identical(self):
        self.pack()
        ota = next(self.out.glob('*-ota.bin'))
        usb = next(self.out.glob('*-usb.zip'))
        first = usb.read_bytes()
        self.pack()
        self.assertEqual(first,usb.read_bytes())
        self.assertEqual(ota.read_bytes(),self.app)
        unpack = self.root/'folder with spaces'
        with zipfile.ZipFile(usb) as archive:archive.extractall(unpack)
        manifest = json.loads((unpack/'flasher_args.json').read_text())
        self.assertEqual(manifest['flash_files'],self.flash)
        self.assertEqual((unpack/'app.bin').read_bytes(),ota.read_bytes())
        for name in ('docs/templates.md','docs/template-authoring.md','assets/template-manifest.json','assets/loxone-logo.png','assets/loxone-wordmark.svg',
                     'docs/previews/template-icons.png','docs/previews/live-home-flow.gif',
                     'examples/config.http.sample.json','examples/loxone-energy-http-post.picoc'):
            self.assertTrue((unpack/name).is_file(),name)
        self.assertEqual(len(list((unpack/'sd-card/templates').glob('*/template.json'))),len(list((ROOT/'templates').glob('*/template.json')))+1)
        home=json.loads((unpack/'sd-card/templates/loxone-home/template.json').read_text())
        self.assertEqual(len(home['icons']),64)
        self.assertEqual(home['kind'],'loxone-home-resources')
        self.assertTrue((unpack/'sd-card/templates/loxone-home/background-dark.png').is_file())
        for descriptor in (ROOT/'templates').glob('*/template.json'):
            for mode in ('light','dark'):
                relative = f'sd-card/templates/{descriptor.parent.name}/preview-{mode}.png'
                self.assertEqual((unpack/relative).read_bytes(),
                                 (descriptor.parent/f'preview-{mode}.png').read_bytes())
        # Documentation and example links must resolve in the offline installer.
        import re
        for guide in [unpack/'README.md', unpack/'TELEPITES.md', *(unpack/'docs').glob('*.md')]:
            for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', guide.read_text(encoding='utf-8')):
                if '://' not in target and not target.startswith('#'):
                    self.assertTrue((guide.parent/target.split('#',1)[0]).exists(), (guide.name,target))
        cmd = command(unpack,'COM5',115200)
        self.assertIn('--no-stub',cmd)
        for address,name in self.flash.items():
            self.assertEqual(cmd[cmd.index(address)+1],str(unpack/name))
        subprocess.run([sys.executable,str(unpack/'flash.py'),'--port','COM5','--dry-run'],check=True,stdout=subprocess.DEVNULL)
        (unpack/'ota_data_initial.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):command(unpack,'COM5',115200)

    def test_usb_v32_modes_and_tool_version(self):
        self.manifest['extra_esptool_args']['stub'] = True
        (self.build/'flasher_args.json').write_text(json.dumps(self.manifest))
        self.pack()
        unpack = self.root/'usb'
        with zipfile.ZipFile(next(self.out.glob('*-usb.zip'))) as z: z.extractall(unpack)
        self.assertEqual((unpack/'requirements-usb.txt').read_text(), f'esptool=={ESPTOOL_VERSION}\n')
        self.assertEqual(json.loads((unpack/'build-info.json').read_text())['esptool'], ESPTOOL_VERSION)
        normal = command(unpack, 'COM3', DEFAULT_BAUD)
        self.assertNotIn('--no-stub', normal)
        self.assertEqual(normal[normal.index('--baud')+1], '115200')
        fallback = command(unpack, 'COM3', DEFAULT_BAUD, no_stub=True, manual_boot=True)
        self.assertIn('--no-stub', fallback)
        self.assertEqual(fallback[fallback.index('--before')+1], 'no-reset')
        self.assertEqual(fallback[fallback.index('--after')+1], 'no-reset')
        for offset, name in self.flash.items():
            self.assertEqual(normal[normal.index(offset)+1], fallback[fallback.index(offset)+1])
        with patch.object(sys, 'argv', ['flash.py', '--port', 'COM3']), patch.object(flash_usb, 'command', return_value=normal), patch.object(flash_usb.importlib.metadata, 'version', return_value='5.1.0'), patch.object(flash_usb.subprocess, 'run') as run:
            with self.assertRaises(SystemExit) as caught: flash_usb.main()
            self.assertIn(ESPTOOL_VERSION, str(caught.exception))
            run.assert_not_called()
        with patch.object(sys, 'argv', ['flash.py', '--port', 'COM3']), patch.object(flash_usb, 'command', return_value=normal), patch.object(flash_usb.importlib.metadata, 'version', return_value=ESPTOOL_VERSION), patch.object(flash_usb.subprocess, 'run') as run:
            run.return_value.returncode = 0
            flash_usb.main()
            run.assert_called_once_with(normal, check=False)
        manifest = json.loads((unpack/'flasher_args.json').read_text())
        manifest['flash_files']['0x1000000'] = 'app.bin'
        (unpack/'flasher_args.json').write_text(json.dumps(manifest))
        hashes = json.loads((unpack/'flash-sha256.json').read_text())
        hashes['flasher_args.json'] = hashlib.sha256((unpack/'flasher_args.json').read_bytes()).hexdigest()
        (unpack/'flash-sha256.json').write_text(json.dumps(hashes))
        with self.assertRaisesRegex(ValueError, '16 MiB'): command(unpack, 'COM3', DEFAULT_BAUD, no_stub=True)
        self.assertIn('0x1000000', command(unpack, 'COM3', DEFAULT_BAUD))

    def test_slave_usb_and_combined_checksums(self):
        self.pack()
        image = bytearray(self.app)
        image[80:80+len(b'zolee_slave_4b')] = b'zolee_slave_4b'
        (self.build/'app.bin').write_bytes(image)
        description = json.loads((self.build/'project_description.json').read_text())
        description['project_name'] = 'zolee_slave_4b'
        (self.build/'project_description.json').write_text(json.dumps(description))
        package(self.build, self.out, self.tag, self.sha, 'slave-4b')
        usb = self.out/f'ZoleeEnergyFlowDisplay-slave-4b-{self.tag}-usb.zip'
        unpack = self.root/'slave usb'
        with zipfile.ZipFile(usb) as archive:
            archive.extractall(unpack)
        self.assertEqual((unpack/'app.bin').read_bytes(), bytes(image))
        self.assertIn('4B', (unpack/'TELEPITES.md').read_text())
        self.assertIn('zolee_slave_4b', (unpack/'firmware/slave-waveshare-4b/README.md').read_text())
        command(unpack, 'COM5', DEFAULT_BAUD, no_stub=True, manual_boot=True)
        checks = dict(line.split('  ', 1)[::-1] for line in (self.out/'SHA256SUMS.txt').read_text().splitlines())
        self.assertEqual(len(checks), 6)
        for name, expected in checks.items():
            self.assertEqual(hashlib.sha256((self.out/name).read_bytes()).hexdigest(), expected)
        assets = []
        def api(url, data=None, content_type=None):
            if data is not None:
                from urllib.parse import parse_qs, urlsplit
                name = parse_qs(urlsplit(url).query)['name'][0]
                result = {'name': name, 'size': len(data), 'state': 'uploaded',
                          'digest': 'sha256:'+hashlib.sha256(data).hexdigest()}
                assets.append(result)
                return result
            if '/commits/' in url:
                return {'sha': self.sha}
            if '/assets?' in url:
                return assets
            return {'tag_name': self.tag, 'draft': False, 'immutable': False,
                    'upload_url': 'https://uploads.github.com/assets{?name}'}
        with patch('publish.api', api):
            publish(self.out, 'owner/repo', self.tag, self.sha, 1)
            self.assertEqual(len(assets), 7)
            publish(self.out, 'owner/repo', self.tag, self.sha, 1)
            self.assertEqual(len(assets), 7)
            info_path = self.out/'slave-4b-build-info.json'
            info = json.loads(info_path.read_text())
            info['commit'] = 'b'*40
            info_path.write_text(json.dumps(info))
            with self.assertRaises(ValueError):
                publish(self.out, 'owner/repo', self.tag, self.sha, 1)
            self.assertEqual(len(assets), 7)
        image[80] = ord('X')
        (self.build/'app.bin').write_bytes(image)
        with self.assertRaises(ValueError):
            package(self.build, self.out, self.tag, self.sha, 'slave-4b')

    def test_bad_build(self):
        (self.build/'ota_data_initial.bin').unlink()
        with self.assertRaises(ValueError):self.pack()
        (self.build/'ota_data_initial.bin').write_bytes(b'ok')
        bad = bytearray(self.app);bad[48]=ord('x');(self.build/'app.bin').write_bytes(bad)
        with self.assertRaises(ValueError):self.pack()

    def test_publish_idempotence_and_protection(self):
        self.pack()
        assets = []
        immutable = False
        posted = []
        def api(url,data=None,content_type=None):
            if data is not None:
                from urllib.parse import parse_qs,urlsplit
                name = parse_qs(urlsplit(url).query)['name'][0]
                result={'name':name,'size':len(data),'state':'uploaded','digest':'sha256:'+hashlib.sha256(data).hexdigest()}
                posted.append(name);assets.append(result);return result
            if '/commits/' in url:return {'sha':self.sha}
            if '/assets?' in url:return assets
            return {'tag_name':self.tag,'draft':False,'immutable':immutable,'upload_url':'https://uploads.github.com/assets{?name}'}
        with patch('publish.api',api):
            publish(self.out,'owner/repo',self.tag,self.sha,1)
            self.assertEqual(len(posted),4)
            immutable=True
            publish(self.out,'owner/repo',self.tag,self.sha,1)
            self.assertEqual(len(posted),4)
            assets[0]['digest']='sha256:wrong'
            with self.assertRaises(ValueError):publish(self.out,'owner/repo',self.tag,self.sha,1)
            assets.clear()
            with self.assertRaises(ValueError):publish(self.out,'owner/repo',self.tag,self.sha,1)
            self.assertEqual(len(posted),4)


if __name__=='__main__':unittest.main()
