"""Exercise release metadata and CI worker failure propagation in isolated repos."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('bash') and shutil.which('git'), 'Requires Bash and Git')
class CITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'scripts/ci').mkdir(parents=True)
        for name in ('prepare_metadata.sh', 'hyptest.sh'):
            shutil.copyfile(ROOT / 'scripts/ci' / name, self.root / 'scripts/ci' / name)
        shutil.copyfile(ROOT / 'scripts/get_version.sh', self.root / 'scripts/get_version.sh')
        (self.root / 'CMakeLists.txt').write_text('set(PROJECT_VERSION "0.2.0")\n')
        self.env = dict(os.environ)
        for name in ('GITHUB_REF_TYPE', 'GITHUB_REF_NAME', 'FORCE_RELEASE', 'GITHUB_SHA'):
            self.env.pop(name, None)
        self.env.update(GIT_AUTHOR_DATE='2026-01-02T03:04:05+00:00',
                        GIT_COMMITTER_DATE='2026-01-02T03:04:05+00:00')
        self.git('init', '--quiet')
        self.git('add', '.')
        self.git('-c', 'user.name=CI test', '-c', 'user.email=ci@example.invalid',
                 'commit', '--quiet', '-m', 'fixture')

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, env=self.env, check=True,
                              capture_output=True, text=True).stdout.strip()

    def metadata(self, **env):
        return subprocess.run(['bash', 'scripts/ci/prepare_metadata.sh'], cwd=self.root,
                              env={**self.env, **env}, capture_output=True, text=True)

    def test_pr_uses_checked_out_commit_and_commit_date(self):
        result = self.metadata(GITHUB_REF_TYPE='branch', GITHUB_REF_NAME='123/merge',
                               GITHUB_SHA='0' * 40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'commit_hash.txt').read_text().strip(), self.git('rev-parse', 'HEAD'))
        self.assertEqual((self.root / 'prerelease.txt').read_text().strip(), 'ci.2026.1.2')

    def test_release_tag_and_prerelease(self):
        for tag, prerelease in [('v0.2.0', ''), ('v0.2.0-rc.1', 'rc.1')]:
            with self.subTest(tag=tag):
                result = self.metadata(GITHUB_REF_TYPE='tag', GITHUB_REF_NAME=tag)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.root / 'prerelease.txt').read_text(), prerelease)

    def test_commit_date_is_utc_without_zero_padding(self):
        for date, prerelease in [
            ('2026-09-08T03:04:05+00:00', 'ci.2026.9.8'),
            ('2026-12-31T23:30:00-01:00', 'ci.2027.1.1'),
        ]:
            with self.subTest(date=date):
                self.env['GIT_COMMITTER_DATE'] = date
                self.git('-c', 'user.name=CI test', '-c', 'user.email=ci@example.invalid',
                         'commit', '--amend', '--no-edit', '--quiet')
                result = self.metadata(GITHUB_ACTIONS='true', TZ='Pacific/Honolulu')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.root / 'prerelease.txt').read_text().strip(), prerelease)

    def test_local_checkout_of_release_tag(self):
        self.git('tag', 'v0.2.0')
        result = self.metadata(GITHUB_ACTIONS='false')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'prerelease.txt').read_text(), '')

    def test_wrong_version_or_malformed_tag_cannot_produce_release_metadata(self):
        for tag in ('v0.1.0', 'v0.2.00', 'v0.2.0-', 'v0.2.0-rc/1', '0.2.0'):
            with self.subTest(tag=tag):
                result = self.metadata(GITHUB_REF_TYPE='tag', GITHUB_REF_NAME=tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'commit_hash.txt').exists())

    @unittest.skipUnless(os.name == 'posix', 'Requires POSIX shell process semantics')
    def test_shards_partition_presets(self):
        shards = []
        for index in range(3):
            result = subprocess.run(
                ['bash', '-c', 'source "$1"; ci_select_steps_multiarg a b c d e',
                 'bash', str(ROOT / 'scripts/common.sh')],
                env={**self.env, 'CI_NODE_TOTAL': '3', 'CI_NODE_INDEX': str(index)},
                capture_output=True, text=True, check=True,
            )
            shards.append(result.stdout.split())
        self.assertEqual(shards, [['a', 'd'], ['b', 'e'], ['c']])

    @unittest.skipUnless(os.name == 'posix', 'Requires POSIX shell process semantics')
    def test_failing_worker_is_reported_and_all_workers_finish(self):
        compiler = self.root / 'compiler.sh'
        compiler.write_text('''#!/usr/bin/env bash
while [[ $1 != --selected-batch ]]; do shift; done
batch=$2
if [[ $batch == 2 ]]; then exit 1; fi
sleep 0.1
printf '%s\\n' "$batch" >> finished
''')
        compiler.chmod(0o755)
        result = subprocess.run(
            ['bash', 'scripts/ci/hyptest.sh'], cwd=self.root,
            env={**self.env, 'HYPTEST_BINARY': str(compiler), 'TEST_JOBS': '2',
                 'CI_NODE_TOTAL': '2', 'CI_NODE_INDEX': '1'},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.root / 'finished').read_text().strip(), '3')

    @unittest.skipUnless(os.name == 'posix', 'Requires POSIX shell process semantics')
    def test_invalid_shard_fails_before_running_compiler(self):
        result = subprocess.run(
            ['bash', 'scripts/ci/hyptest.sh'], cwd=self.root,
            env={**self.env, 'TEST_JOBS': '2', 'CI_NODE_TOTAL': '2', 'CI_NODE_INDEX': '2'},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid test worker or shard', result.stderr)
