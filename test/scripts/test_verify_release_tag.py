"""Exercise release tag verification against a local fake GitHub CLI."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_ci import BASH, ROOT


BUILT = 'a' * 40
OTHER = 'b' * 40
TAG_OBJECT = 'c' * 40

# Answers the four API reads the verifier makes. Each answer is what the real `gh api --jq`
# filter would print; the environment variables below choose it.
FAKE_GH = r'''
gh() {
    printf '%s\0' "$@" >> gh_calls
    printf '\n' >> gh_calls
    [[ $1 == api ]] || { echo "Unexpected gh command" >&2; return 2; }
    local stage
    case "$2" in
        "repos/$GH_REPO/git/ref/tags/$RELEASE_TAG") stage=ref ;;
        "repos/$GH_REPO/git/tags/$TAG_OBJECT") stage=tag ;;
        "repos/$GH_REPO") stage=repo ;;
        "repos/$GH_REPO/compare/main...$TAG_COMMIT?per_page=1") stage=compare ;;
        *) echo "Unexpected API path $2" >&2; return 2 ;;
    esac
    if [[ $stage == "$FAIL_STAGE" ]]; then
        echo "Simulated $stage failure" >&2
        return 1
    fi
    case "$stage" in
        ref) echo "$REF_TYPE $TAG_OBJECT" ;;
        tag) echo "$TAG_NAME $TARGET_TYPE $TAG_COMMIT $VERIFIED $REASON" ;;
        repo) echo main ;;
        compare) echo "$COMPARE_STATUS" ;;
    esac
}
source scripts/ci/verify_release_tag.sh
'''


@unittest.skipUnless(BASH, 'Requires Bash')
class ReleaseTagVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'scripts/ci').mkdir(parents=True)
        shutil.copyfile(ROOT / 'scripts/ci/verify_release_tag.sh', self.root / 'scripts/ci/verify_release_tag.sh')

    def verify(self, tag='v0.3.0', **overrides):
        # A signed, annotated tag on main pointing at the commit this run built, unless overridden.
        settings = {
            'GH_REPO': 'example/hyperion', 'RELEASE_TAG': tag, 'GITHUB_SHA': BUILT,
            'TAG_OBJECT': TAG_OBJECT, 'TAG_NAME': tag, 'TAG_COMMIT': BUILT, 'REF_TYPE': 'tag', 'TARGET_TYPE': 'commit',
            'VERIFIED': 'true', 'REASON': 'valid', 'COMPARE_STATUS': 'identical', 'FAIL_STAGE': '',
        }
        settings.update(overrides)
        # Braces matter: Bash reads $10 as $1 followed by "0".
        exports = ''.join(f'export {name}="${{{index + 1}}}"\n' for index, name in enumerate(settings))
        return subprocess.run(
            [BASH, '-c', exports + FAKE_GH, 'verify-test', *settings.values()],
            cwd=self.root, capture_output=True, text=True,
        )

    def api_paths(self):
        path = self.root / 'gh_calls'
        calls = [line.rstrip('\0').split('\0') for line in path.read_text().splitlines()] if path.exists() else []
        return [call[1] for call in calls]

    def assert_rejected(self, result, message):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('::error::', result.stderr)
        self.assertIn(message, result.stderr)

    def test_signed_annotated_tag_on_main_is_accepted(self):
        for status in ('identical', 'behind'):
            with self.subTest(compare_status=status):
                (self.root / 'gh_calls').unlink(missing_ok=True)
                result = self.verify(COMPARE_STATUS=status)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'points at {BUILT} on main', result.stdout)
                self.assertEqual(self.api_paths(), [
                    'repos/example/hyperion/git/ref/tags/v0.3.0',
                    f'repos/example/hyperion/git/tags/{TAG_OBJECT}',
                    'repos/example/hyperion',
                    f'repos/example/hyperion/compare/main...{BUILT}?per_page=1',
                ])

    def test_prerelease_tag_is_accepted(self):
        result = self.verify(tag='v0.3.0-rc.1')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_lightweight_tag_is_rejected(self):
        # The GitHub release page creates lightweight tags; v0.2.0 was one (CIPH-HYPERION-28).
        result = self.verify(REF_TYPE='commit')
        self.assert_rejected(result, 'is a lightweight tag')
        self.assertEqual(len(self.api_paths()), 1)

    def test_unverified_signature_is_rejected(self):
        for reason in ('unsigned', 'unknown_key', 'bad_email'):
            with self.subTest(reason=reason):
                result = self.verify(VERIFIED='false', REASON=reason)
                self.assert_rejected(result, f'does not verify the signature on v0.3.0 (reason: {reason})')

    def test_tag_object_signed_for_another_version_is_rejected(self):
        # A validly signed tag object for v0.2.0, pushed again as refs/tags/v0.3.0.
        result = self.verify(TAG_NAME='v0.2.0')
        self.assert_rejected(result, 'The tag object behind v0.3.0 is signed as v0.2.0')
        self.assertEqual(len(self.api_paths()), 2)

    def test_tag_of_a_non_commit_is_rejected(self):
        result = self.verify(TARGET_TYPE='tree')
        self.assert_rejected(result, 'points at a tree, not a commit')

    def test_tag_of_another_commit_than_the_build_is_rejected(self):
        result = self.verify(TAG_COMMIT=OTHER)
        self.assert_rejected(result, f'points at {OTHER}, but this run built {BUILT}')

    def test_commit_off_the_default_branch_is_rejected(self):
        for status in ('ahead', 'diverged'):
            with self.subTest(compare_status=status):
                result = self.verify(COMPARE_STATUS=status)
                self.assert_rejected(result, f'is not on main (compare status: {status})')

    def test_api_failures_fail_closed(self):
        later = {'ref': 1, 'tag': 2, 'repo': 3, 'compare': 4}
        for stage, calls in later.items():
            with self.subTest(stage=stage):
                (self.root / 'gh_calls').unlink(missing_ok=True)
                result = self.verify(FAIL_STAGE=stage)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f'Simulated {stage} failure', result.stderr)
                self.assertNotIn('is annotated, its signature is verified', result.stdout)
                self.assertEqual(len(self.api_paths()), calls, 'no API call may follow a failed one')

    def test_invalid_tag_fails_before_api_calls(self):
        for tag in ('v0.3', 'v0.3.0"invalid', '0.3.0', 'v0.3.0/x'):
            with self.subTest(tag=tag):
                result = self.verify(tag=tag)
                self.assert_rejected(result, 'Invalid release tag')
        self.assertFalse((self.root / 'gh_calls').exists())

    def test_missing_settings_fail_before_api_calls(self):
        for name in ('GH_REPO', 'RELEASE_TAG', 'GITHUB_SHA'):
            with self.subTest(missing=name):
                result = self.verify(**{name: ''})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing', result.stderr)
        self.assertFalse((self.root / 'gh_calls').exists())


if __name__ == '__main__':
    unittest.main()
