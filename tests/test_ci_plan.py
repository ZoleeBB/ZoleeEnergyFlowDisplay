#!/usr/bin/env python3
"""Exercise event routing and exact merged-main provenance without publishing."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/release'))
from ci_plan import plan


class BuildPlanTests(unittest.TestCase):
    repository = 'owner/repo'
    sha = 'a' * 40

    def candidate(self, number=20, **changes):
        result = {'number': number, 'merged_at': '2026-10-21T16:00:00Z',
                  'merge_commit_sha': self.sha,
                  'base': {'ref': 'main', 'repo': {'full_name': self.repository}}}
        result.update(changes)
        return result

    def test_pr_and_manual_checks_never_resolve_or_publish(self):
        with patch('ci_plan.listing') as listing, patch('ci_plan.prepare') as prepare:
            for event, ref in [('pull_request', 'refs/pull/20/merge'),
                               ('workflow_dispatch', 'refs/heads/main')]:
                result = plan(self.repository, self.sha, event, ref)
                self.assertEqual(result, {'tag': f'v0.0.0-ci.g{self.sha[:7]}', 'pr': '0',
                                          'publish': 'false', 'build': 'true'})
            manual = plan(self.repository, self.sha, 'release', 'refs/tags/v3.0.0', 'v3.0.0')
            self.assertEqual(manual['tag'], 'v3.0.0')
            self.assertEqual(manual['publish'], 'false')
            listing.assert_not_called()
            prepare.assert_not_called()

    def test_automatic_cannot_be_enabled_for_untrusted_events(self):
        for event, ref, tag in [('pull_request', 'refs/heads/main', ''),
                                ('workflow_dispatch', 'refs/heads/main', ''),
                                ('push', 'refs/heads/feature/test', ''),
                                ('push', 'refs/heads/main', 'v3.0.0')]:
            with self.assertRaises(ValueError):
                plan(self.repository, self.sha, event, ref, tag, automatic=True)

    def test_direct_push_and_unrelated_prs_only_build_ci(self):
        irrelevant = [self.candidate(merged_at=None),
                      self.candidate(merge_commit_sha='b' * 40),
                      self.candidate(base={'ref': 'develop', 'repo': {'full_name': self.repository}}),
                      self.candidate(base={'ref': 'main', 'repo': {'full_name': 'fork/repo'}})]
        for candidates in [[], irrelevant]:
            with patch('ci_plan.listing', return_value=candidates), patch('ci_plan.prepare') as prepare:
                result = plan(self.repository, self.sha, 'push', 'refs/heads/main', automatic=True)
                self.assertEqual(result['publish'], 'false')
                self.assertEqual(result['build'], 'true')
                prepare.assert_not_called()

    def test_exact_merge_uses_existing_verified_release_selection(self):
        with patch('ci_plan.listing', return_value=[self.candidate()]) as listing:
            with patch('ci_plan.prepare', return_value={'tag': 'v3.0.0', 'needed': 'true'}) as prepare:
                result = plan(self.repository, self.sha, 'push', 'refs/heads/main', automatic=True)
                self.assertEqual(result, {'tag': 'v3.0.0', 'pr': '20',
                                          'publish': 'true', 'build': 'true'})
                prepare.assert_called_once_with(self.repository, 20, self.sha)
            listing.assert_called_once_with('https://api.github.com/repos/owner/repo',
                                            'commits/' + self.sha + '/pulls')

    def test_already_published_merge_does_not_rebuild_or_republish(self):
        with patch('ci_plan.listing', return_value=[self.candidate()]):
            with patch('ci_plan.prepare', return_value={'tag': 'v3.0.0', 'needed': 'false'}):
                result = plan(self.repository, self.sha, 'push', 'refs/heads/main', automatic=True)
                self.assertEqual(result['tag'], 'v3.0.0')
                self.assertEqual(result['build'], 'false')
                self.assertEqual(result['publish'], 'false')

    def test_ambiguous_identity_and_changed_merge_fail_closed(self):
        with patch('ci_plan.listing', return_value=[self.candidate(), self.candidate(21)]):
            with self.assertRaisesRegex(ValueError, 'Multiple merged'):
                plan(self.repository, self.sha, 'push', 'refs/heads/main', automatic=True)
        with patch('ci_plan.listing', return_value=[self.candidate()]):
            with patch('ci_plan.prepare', side_effect=ValueError('merge changed')):
                with self.assertRaisesRegex(ValueError, 'merge changed'):
                    plan(self.repository, self.sha, 'push', 'refs/heads/main', automatic=True)

    def test_invalid_inputs(self):
        for repository, sha, tag in [('owner/repo/extra', self.sha, ''),
                                     (self.repository, '../main', ''),
                                     (self.repository, self.sha, 'v3.0.0; invalid')]:
            with self.assertRaises(ValueError):
                plan(repository, sha, 'workflow_dispatch', 'refs/heads/main', tag)


if __name__ == '__main__':
    unittest.main()
