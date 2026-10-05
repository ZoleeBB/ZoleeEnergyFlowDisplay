#!/usr/bin/env python3
"""Version selection, merged-only guard and complete draft-to-release lifecycle."""
import hashlib
import json
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.parse import parse_qs, urlsplit
import test_release
from auto_release import next_tag, prepare, finish, identity


class AutomaticReleaseTests(unittest.TestCase):
    setUp = test_release.ReleaseTests.setUp
    pack = test_release.ReleaseTests.pack

    def test_next_version(self):
        self.assertEqual(next_tag([],[]),'v0.0.1')
        self.assertEqual(next_tag([{'name':'Prod'}],[{'tag_name':'Prod','name':'0.0.1'}]),'v0.0.2')
        self.assertEqual(next_tag([{'name':'v1.2.9'},{'name':'v1.2.10'},{'name':'v0.0.0-ci'},
                                  {'name':'v2.0.0-rc.1'}],[]),'v1.2.11')
        self.assertEqual(next_tag([{'name':'v1.9.99'},{'name':'v2.0.0'}],[]),'v2.0.1')
        self.assertEqual(next_tag([], [{'tag_name':'v1.0.8','name':'Draft','draft':True}]),'v1.0.9')

    def mock_api(self):
        self.releases=[];self.tags={};self.assets=[];self.events=[];self.merged=True
        self.branch='main';self.fail_upload=False;self.tag_collision=False
        def call(url,data=None,content_type=None,method=None):
            parsed=urlsplit(url);path=parsed.path
            payload=json.loads(data) if data and content_type!='application/octet-stream' else None
            if path.endswith('/pulls/3'):
                return {'merged':self.merged,'base':{'ref':self.branch,'repo':{'full_name':'owner/repo'}},'merge_commit_sha':self.sha}
            if path.endswith('/tags') and not data:return [{'name':tag} for tag in self.tags]
            if path.endswith('/releases'):
                if data:
                    self.events.append('create-draft')
                    release={**payload,'id':1,'upload_url':'https://uploads.github.com/assets{?name}'}
                    self.releases.append(release);return release.copy()
                return [r.copy() for r in self.releases]
            if path.endswith('/git/refs'):
                self.events.append('create-tag');tag=payload['ref'].removeprefix('refs/tags/')
                if self.tag_collision:self.tags[tag]='b'*40
                if tag in self.tags:raise HTTPError(url,422,'exists',{},None)
                self.tags[tag]=payload['sha'];return {}
            if '/commits/' in path:return {'sha':self.tags[path.rsplit('/',1)[1]]}
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
            self.assertEqual(self.events,['create-draft','create-tag']+['upload']*4+['publish'])
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
            self.assertEqual(prepare('owner/repo',3,self.sha),{'tag':self.tag,'needed':'true'})
            self.fail_upload=False
            finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertFalse(self.releases[0]['draft']);self.assertEqual(len(self.releases),1)

    def test_existing_tag_never_moved(self):
        self.pack();call=self.mock_api();self.tag_collision=True
        with patch('auto_release.api',call),patch('publish.api',call):
            with self.assertRaises(ValueError):finish(self.out,'owner/repo',3,self.sha,self.tag)
            self.assertEqual(self.tags[self.tag],'b'*40)
            self.assertNotIn('upload',self.events);self.assertNotIn('publish',self.events)


if __name__=='__main__':unittest.main()
