import unittest

from ci.check_semantic_test_settings import check_settings


class SemanticTestSettingsTest(unittest.TestCase):
    def test_default_runs_both(self):
        self.assertEqual(check_settings('contract C {}\n// ----\n// f() -> 1'), [])

    def test_single_pipeline_requires_reason(self):
        for pipeline in ('true', 'false'):
            fixture = f'// ====\n// compileViaYul: {pipeline}\n'
            self.assertTrue(check_settings(fixture + '// ----'))
            self.assertTrue(check_settings(fixture + '// compileViaYulReason:  \n// QRVMVersion: =zond\n// ----'))
            self.assertEqual(check_settings(fixture + '// compileViaYulReason: Raw assembly representation.\n// ----'), [])

    def test_unreachable_version_is_rejected(self):
        for restriction in ('>zond', '<zond', '!zond', 'future', 'zond'):
            self.assertTrue(check_settings(f'// ====\n// QRVMVersion: {restriction}\n// ----'))
        for restriction in ('>=zond', '<=zond', '=zond', 'any'):
            self.assertEqual(check_settings(f'// ====\n// QRVMVersion: {restriction}\n// ----'), [])

    def test_source_and_expectations_are_not_settings(self):
        self.assertEqual(check_settings('// compileViaYul: false\n// ====\n// ----\n// QRVMVersion: >zond'), [])
