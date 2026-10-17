#!/usr/bin/env python3
"""Version selection, merged-only guard and complete draft-to-release lifecycle."""
import hashlib
import json
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.parse import parse_qs, urlsplit
import test_release
from auto_release import next_tag, tag_for_merged_pr, prepare, finish, identity


class AutomaticReleaseTests(unittest.TestCase):
    def setUp(self):
        test_release.ReleaseTests.setUp(self)
        self.tag='v1.2.3'
        data=bytearray(self.app);data[48:80]=self.tag.encode().ljust(32,b'\0')
        self.app=bytes(data);(self.build/'app.bin').write_bytes(self.app)
        path=self.build/'project_description.json';description=json.loads(path.read_text())
        description['project_version']=self.tag;path.write_text(json.dumps(description))

    pack = test_release.ReleaseTests.pack

    def test_next_version(self):
        self.assertEqual(next_tag([],[]),'v0.0.1')
        self.assertEqual(next_tag([{'name':'Prod'}],[{'tag_name':'Prod','name':'0.0.1'}]),'v0.0.2')
        self.assertEqual(next_tag([{'name':'v1.2.9'},{'name':'v1.2.10'},{'name':'v0.0.0-ci'},
                                  {'name':'v2.0.0-rc.1'}],[]),'v1.2.11')
        self.assertEqual(next_tag([{'name':'v1.9.99'},{'name':'v2.0.0'}],[]),'v2.0.1')
        self.assertEqual(next_tag([], [{'tag_name':'v1.0.8','name':'Draft','draft':True}]),'v1.0.9')

    def test_loxone_major_release(self):
        self.assertEqual(tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',12,[{'name':'v1.4.9'}],[]),'v2.0.0')
        self.assertEqual(tag_for_merged_pr('other/repo',12,[{'name':'v1.4.9'}],[]),'v1.4.10')
        self.assertEqual(tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',43,[{'name':'v2.0.0'}],[]),'v2.0.1')
        with self.assertRaises(ValueError):tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',12,[{'name':'v2.0.0'}],[])

    def test_major_release_only_for_project_pr9(self):
        existing=[{'name':'v0.0.6'}]
        self.assertEqual(tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',9,existing,[]),'v1.0.0')
        self.assertEqual(tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',10,existing,[]),'v0.0.7')
        self.assertEqual(tag_for_merged_pr('owner/repo',9,existing,[]),'v0.0.7')
        self.assertEqual(tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',10,[{'name':'v1.0.0'}],[]),'v1.0.1')
        with self.assertRaises(ValueError):
            tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',9,[{'name':'v1.0.0'}],[])
        with self.assertRaises(ValueError):
            tag_for_merged_pr('ZoleeBB/ZoleeEnergyFlowDisplay',9,[],[{'tag_name':'v1.1.0','name':'new'}])

    def mock_api(self):
        self.releases=[];self.tags={};self.assets=[];self.events=[];self.merged=True
        self.branch='main';self.fail_upload=False;self.tag_collision=False
        self.annotations={};self.hide_drafts=False;self.fail_draft=False
        def call(url,data=None,content_type=None,method=None):
            parsed=urlsplit(url);path=parsed.path
            payload=json.loads(data) if data and content_type!='application/octet-stream' else None
            if path.endswith('/pulls/3'):
                return {'merged':self.merged,'base':{'ref':self.branch,'repo':{'full_name':'owner/repo'}},'merge_commit_sha':self.sha}
            if path.endswith('/tags') and not data and '/git/' not in path:
                return [{'name':tag,'commit':{'sha':self.annotations.get(sha,{}).get('object',{}).get('sha',sha)}} for tag,sha in self.tags.items()]
            if path.endswith('/git/tags') and data:
                oid=str(len(self.annotations)+1).zfill(40)
                self.annotations[oid]={'message':payload['message'],'object':{'type':'commit','sha':payload['object']}}
                return {'sha':oid}
            if '/git/tags/' in path:return self.annotations[path.rsplit('/',1)[1]]
            if '/git/ref/tags/' in path:
                tag=path.rsplit('/',1)[1]
                if tag not in self.tags:raise HTTPError(url,404,'not found',{},None)
                oid=self.tags[tag]
                return {'object':{'type':'tag' if oid in self.annotations else 'commit','sha':oid}}
            if path.endswith('/releases'):
                if data:
                    self.events.append('create-draft')
                    if self.fail_draft:raise HTTPError(url,503,'draft failure',{},None)
                    release={**payload,'id':1,'upload_url':'https://uploads.github.com/assets{?name}'}
                    self.releases.append(release);return release.copy()
                return [r.copy() for r in self.releases if not (self.hide_drafts and r['draft'])]
            if path.endswith('/git/refs'):
                self.events.append('create-tag');tag=payload['ref'].removeprefix('refs/tags/')
                if self.tag_collision:self.tags[tag]='b'*40
                if tag in self.tags:raise HTTPError(url,422,'exists',{},None)
                self.tags[tag]=payload['sha'];return {}
            if '/commits/' in path:
                oid=self.tags[path.rsplit('/',1)[1]]
                return {'sha':self.annotations.get(oid,{}).get('object',{}).get('sha',oid)}
            if path.endswith('/releases/1/assets'):return self.assets
            if path.endswith('/releases/1'):
                if method=='PATCH':
                    self.events.append('publish');self.releases[0].update(payload)
                return self.releases[0].copy()
            if parsed.netloc=='uploads.github.com':
                self.events.append('upload')
                if self.fail_upload:raise HTTPError(url,503,'temporary upload failure',{},None)
                result={'name':parse_qs(parsed.query)['name'][0],'size':len(data),'state':'uploaded',
                        'digest':'sha256:'+hashlib.sha256(data).hexdigest()}
                self.assets.append(result);return result
            raise AssertionError(url)
        return call

    def test_only_verified_merges(self):
        call=self.mock_api()
        with patch('auto_release.api',call):
            self.merged=False
            with self.assertRaises(ValueError):prepare('owner/repo',3,self.sha)
            self.merged=True;self.branch='develop'
            with self.assertRaises(ValueError):prepare('owner/repo',3,self.sha)
            self.branch='main'
            with self.assertRaises(ValueError):prepare('owner/repo',3,'b'*40)
            self.assertEqual(prepare('owner/repo',3,self.sha),{'tag':'v0.0.1','needed':'true'})
        self.assertEqual(self.events,[])  # Version selection does not create a release or tag.

    def test_upload_before_publication_and_rerun(self):
        self.pack();call=self.mock_api()
        with patch('auto_release.api',call),patch('publish.api',call):
            finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertEqual(self.events,['create-tag','create-draft']+['upload']*4+['publish'])
            self.assertFalse(self.releases[0]['draft'])
            self.assertEqual(prepare('owner/repo',3,self.sha),{'tag':self.tag,'needed':'false'})
            self.assertEqual(len(self.releases),1)
            finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertEqual(self.events.count('upload'),4)
            self.assertEqual(self.events.count('publish'),1)

    def test_failed_upload_stays_draft_and_resumes(self):
        self.pack();call=self.mock_api();self.fail_upload=True
        with patch('auto_release.api',call),patch('publish.api',call):
            with self.assertRaises(HTTPError):finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertTrue(self.releases[0]['draft']);self.assertNotIn('publish',self.events)
            self.hide_drafts=True
            self.assertEqual(prepare('owner/repo',3,self.sha),{'tag':self.tag,'needed':'true'})
            self.hide_drafts=False
            self.fail_upload=False
            finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertFalse(self.releases[0]['draft']);self.assertEqual(len(self.releases),1)

    def test_reservation_recovers_even_before_draft_creation(self):
        self.pack();call=self.mock_api();self.fail_draft=True
        with patch('auto_release.api',call),patch('publish.api',call):
            with self.assertRaises(HTTPError):finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertEqual(self.releases,[])
            # A later merge can reserve a newer version; retry must still use its own.
            self.tags['v1.2.4']='b'*40
            self.assertEqual(prepare('owner/repo',3,self.sha),{'tag':self.tag,'needed':'true'})
            self.fail_draft=False
            finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertFalse(self.releases[0]['draft'])

    def test_existing_tag_never_moved(self):
        self.pack();call=self.mock_api();self.tag_collision=True
        with patch('auto_release.api',call),patch('publish.api',call):
            with self.assertRaises(ValueError):finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertEqual(self.tags[self.tag],'b'*40)
            self.assertNotIn('upload',self.events);self.assertNotIn('publish',self.events)


if __name__=='__main__':unittest.main()
