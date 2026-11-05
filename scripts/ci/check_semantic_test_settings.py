#!/usr/bin/env python3
"""Keep semantic regressions executable on Zond and pipeline exclusions reviewable."""

import re
import sys
from pathlib import Path


def check_settings(source):
    """Return policy violations in the settings section of a semantic fixture."""
    settings = source.partition('// ====')[2].partition('// ----')[0]
    values = dict(re.findall(r'^// (\w+):[ \t]*(.*?)[ \t]*$', settings, re.MULTILINE))
    errors = []
    pipeline = values.get('compileViaYul', 'also')
    if pipeline not in ('also', 'true', 'false'):
        errors.append('invalid compileViaYul setting')
    reason = values.get('compileViaYulReason', '')
    if pipeline != 'also' and not reason:
        errors.append('single-pipeline tests require compileViaYulReason')
    if pipeline == 'also' and reason:
        errors.append('remove compileViaYulReason when both pipelines run')
    if 'QRVMVersion' in values:
        # Zond is currently the only supported QRVM revision.
        if values['QRVMVersion'] not in ('any', '>=zond', '<=zond', '=zond'):
            errors.append('QRVMVersion excludes the supported Zond target')
    return errors


def main():
    root = Path(__file__).resolve().parents[2] / 'test/libhyperion/semanticTests'
    failures = []
    for path in sorted(root.rglob('*.hyp')):
        for error in check_settings(path.read_text(encoding='utf8')):
            failures.append(f'{path.relative_to(root)}: {error}')
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print('Semantic test settings: both pipelines or explained exclusions; all fixtures enabled on Zond.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
