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
from flash_usb import command
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
        for name in ('templates.md','template-authoring.md','template-icon-prompts.json',
                     'previews/templates.png','previews/template-icons.png','previews/live-home-flow.gif'):
            self.assertTrue((unpack/name).is_file(),name)
        self.assertEqual(len(list((unpack/'sd-card/templates').glob('*/template.json'))),8)
        cmd = command(unpack,'COM5',115200)
        self.assertIn('--no-stub',cmd)
        for address,name in self.flash.items():
            self.assertEqual(cmd[cmd.index(address)+1],str(unpack/name))
        subprocess.run([sys.executable,str(unpack/'flash.py'),'--port','COM5','--dry-run'],check=True,stdout=subprocess.DEVNULL)
        (unpack/'ota_data_initial.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):command(unpack,'COM5',115200)

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
