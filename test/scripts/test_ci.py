"""Exercise release metadata, platform handling and CI workers in isolated repos."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BASH = shutil.which('bash')
if os.name == 'nt' and shutil.which('git'):
    # Use Git for Windows' wrapper to initialize its runtime environment.
    git_exec_path = subprocess.check_output(['git', '--exec-path'], text=True).strip()
    git_bash = Path(git_exec_path).parents[2] / 'bin/bash.exe'
    BASH = str(git_bash) if git_bash.is_file() else None


@unittest.skipUnless(BASH and shutil.which('git'), 'Requires Bash and Git')
class CITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'scripts/ci').mkdir(parents=True)
        for name in ('prepare_metadata.sh', 'hyptest.sh', 'hyptest_all.sh', 'build_qrvmone.sh', 'build.sh'):
            shutil.copyfile(ROOT / 'scripts/ci' / name, self.root / 'scripts/ci' / name)
        shutil.copyfile(ROOT / 'scripts/get_version.sh', self.root / 'scripts/get_version.sh')
        (self.root / 'CMakeLists.txt').write_text('set(PROJECT_VERSION "0.2.0")\n')
        self.env = dict(os.environ)
        for name in ('GITHUB_REF_TYPE', 'GITHUB_REF_NAME', 'FORCE_RELEASE', 'GITHUB_SHA',
                     'BOOST_TEST_ARGS', 'CMAKE_OPTIONS', 'COVERAGE'):
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
        return subprocess.run([BASH, 'scripts/ci/prepare_metadata.sh'], cwd=self.root,
                              env={**self.env, **env}, capture_output=True, text=True)

    def test_pr_uses_checked_out_commit_and_commit_date(self):
        result = self.metadata(GITHUB_REF_TYPE='branch', GITHUB_REF_NAME='123/merge',
                               GITHUB_SHA='0' * 40)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'commit_hash.txt').read_text().strip(), self.git('rev-parse', 'HEAD'))
        self.assertEqual((self.root / 'prerelease.txt').read_text().strip(), 'ci.2026.1.2')

    def test_release_tag_and_prerelease(self):
        for tag, prerelease in [('v0.2.0', ''), ('v0.2.0-rc.1', 'rc.1')]:
            with self.subTest(tag=tag):
                result = self.metadata(GITHUB_REF_TYPE='tag', GITHUB_REF_NAME=tag)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
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
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((self.root / 'prerelease.txt').read_text().strip(), prerelease)

    def test_local_checkout_of_release_tag(self):
        self.git('tag', 'v0.2.0')
        result = self.metadata(GITHUB_ACTIONS='false')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'prerelease.txt').read_text(), '')

    def test_wrong_version_or_malformed_tag_cannot_produce_release_metadata(self):
        for tag in ('v0.1.0', 'v0.2.00', 'v0.2.0-', 'v0.2.0-rc/1', '0.2.0'):
            with self.subTest(tag=tag):
                result = self.metadata(GITHUB_REF_TYPE='tag', GITHUB_REF_NAME=tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'commit_hash.txt').exists())

    def test_qrvm_build_selects_platform_bootstrap_and_library(self):
        build = self.root / 'deps/qrvmone/build'
        build.mkdir(parents=True)
        for library in ('qrvmone.dll', 'libqrvmone.so', 'libqrvmone.dylib'):
            (build / library).touch()
        github_env = self.root / 'github_env'
        for ostype, runner_os, library in [
            ('msys', '', 'qrvmone.dll'),
            ('cygwin', '', 'qrvmone.dll'),
            ('mingw64', '', 'qrvmone.dll'),
            ('unknown', 'Windows', 'qrvmone.dll'),
            ('linux-gnu', 'Linux', 'libqrvmone.so'),
            ('darwin24', 'macOS', 'libqrvmone.dylib'),
        ]:
            with self.subTest(ostype=ostype, runner_os=runner_os):
                github_env.write_text('')
                (self.root / 'cmake_arguments').write_text('')
                result = subprocess.run(
                    [BASH, '-c', '''
OSTYPE=$1
cmake() { printf '%s\\n' "$@" >> cmake_arguments; }
cygpath() {
    case "$1" in
        -m) printf '%s\\n' 'C:/CI fixture/scripts/ci/cmake' ;;
        -w) printf '%s\\n' 'C:\\CI fixture\\qrvmone.dll' ;;
        *) return 1 ;;
    esac
}
source "$0"
''', 'scripts/ci/build_qrvmone.sh', ostype],
                    cwd=self.root, env={**self.env, 'RUNNER_OS': runner_os,
                                        'GITHUB_ENV': github_env.as_posix()},
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                arguments = (self.root / 'cmake_arguments').read_text().splitlines()
                exported = github_env.read_text().strip()
                if library == 'qrvmone.dll':
                    self.assertIn('-DCMAKE_MODULE_PATH=C:/CI fixture/scripts/ci/cmake', arguments)
                    self.assertEqual(exported, r'QRL_QRVMONE=C:\CI fixture\qrvmone.dll')
                else:
                    self.assertFalse(any(arg.startswith('-DCMAKE_MODULE_PATH=') for arg in arguments))
                    self.assertTrue(exported.endswith('/' + library), exported)

    def test_compiler_build_with_and_without_optional_arguments(self):
        for options in ('', '-DUSE_Z3=OFF -DUSE_CVC4=OFF'):
            with self.subTest(options=options):
                (self.root / 'cmake_arguments').write_text('')
                result = subprocess.run(
                    [BASH, '-c', '''
cmake() { printf '%s\\n' "$@" >> cmake_arguments; }
source "$0"
''', 'scripts/ci/build.sh'],
                    cwd=self.root, env={**self.env, 'CMAKE_OPTIONS': options, 'BUILD_JOBS': '4',
                                        'HYPERION_BUILD_DIR': 'build directory'},
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                arguments = (self.root / 'cmake_arguments').read_text().splitlines()
                self.assertEqual(arguments, ['-S', '.', '-B', 'build directory', '-DCMAKE_BUILD_TYPE=Release',
                                            *options.split(), '--build', 'build directory', '--parallel', '4'])

    def test_windows_test_workers_do_not_set_posix_stack_limits(self):
        for ostype, runner_os, windows in [
            ('msys', '', True),
            ('cygwin', '', True),
            ('mingw64', '', True),
            ('unknown', 'Windows', True),
            ('linux-gnu', 'Linux', False),
            ('darwin24', 'macOS', False),
        ]:
            with self.subTest(ostype=ostype, runner_os=runner_os):
                result = subprocess.run(
                    [BASH, '-c', '''
OSTYPE=$1
ulimit() { echo stack-limit; }
hyptest_stub() { echo worker-ran; }
source "$0"
''', 'scripts/ci/hyptest.sh', ostype],
                    cwd=self.root, env={**self.env, 'RUNNER_OS': runner_os,
                                        'HYPTEST_BINARY': 'hyptest_stub', 'TEST_JOBS': '1',
                                        'CI_NODE_TOTAL': '1', 'CI_NODE_INDEX': '0'},
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('worker-ran', result.stdout)
                self.assertEqual('stack-limit' in result.stdout, not windows)

    def test_all_compiler_configurations_run_and_preserve_failures(self):
        with (self.root / 'scripts/ci/hyptest.sh').open('w', encoding='utf8', newline='\n') as mock:
            mock.write('''printf '%s\\n' "$OPTIMIZE" >> passes
[[ $OPTIMIZE != $FAIL_OPTIMIZE ]]
''')
        for fail_optimize in ('0', '1', 'none'):
            with self.subTest(fail_optimize=fail_optimize):
                (self.root / 'passes').write_text('')
                result = subprocess.run(
                    [BASH, 'scripts/ci/hyptest_all.sh'], cwd=self.root,
                    env={**self.env, 'FAIL_OPTIMIZE': fail_optimize},
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0 if fail_optimize == 'none' else 1, result.stdout + result.stderr)
                self.assertEqual((self.root / 'passes').read_text().splitlines(), ['0', '1'])

    @unittest.skipUnless(os.name == 'posix', 'Requires POSIX shell process semantics')
    def test_shards_partition_presets(self):
        shards = []
        for index in range(3):
            result = subprocess.run(
                [BASH, '-c', 'source "$1"; ci_select_steps_multiarg a b c d e',
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
            [BASH, 'scripts/ci/hyptest.sh'], cwd=self.root,
            env={**self.env, 'HYPTEST_BINARY': str(compiler), 'TEST_JOBS': '2',
                 'CI_NODE_TOTAL': '2', 'CI_NODE_INDEX': '1'},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.root / 'finished').read_text().strip(), '3')

    @unittest.skipUnless(os.name == 'posix', 'Requires POSIX shell process semantics')
    def test_invalid_shard_fails_before_running_compiler(self):
        result = subprocess.run(
            [BASH, 'scripts/ci/hyptest.sh'], cwd=self.root,
            env={**self.env, 'TEST_JOBS': '2', 'CI_NODE_TOTAL': '2', 'CI_NODE_INDEX': '2'},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid test worker or shard', result.stderr)
