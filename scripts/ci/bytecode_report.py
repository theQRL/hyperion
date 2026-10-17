#!/usr/bin/env python3
"""Compare actual code generation, metadata and linking across native/WASM builds."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--hypcjs', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / 'test/ci/bytecode.hyp'
    command = (
        ['node', str(root / 'scripts/ci/compile_hypcjs.js'), str(args.compiler.resolve())]
        if args.hypcjs else [str(args.compiler.resolve()), '--standard-json']
    )
    reports = {}
    for via_ir in (False, True):
        for optimize in (False, True):
            compiler_input = {
                'language': 'Hyperion',
                'sources': {'bytecode.hyp': {'content': source.read_text(encoding='utf8')}},
                'settings': {
                    'viaIR': via_ir,
                    'optimizer': {'enabled': optimize},
                    'outputSelection': {'*': {'*': ['metadata', 'qrvm.bytecode', 'qrvm.deployedBytecode']}},
                },
            }
            result = subprocess.run(command, input=json.dumps(compiler_input), text=True,
                                    capture_output=True, check=True, timeout=180)
            output = json.loads(result.stdout)
            errors = [error for error in output.get('errors', []) if error['severity'] == 'error']
            if errors:
                raise RuntimeError(json.dumps(errors, indent=2))
            contracts = output['contracts']['bytecode.hyp']
            for contract in ('Arithmetic', 'BytecodeCompatibility'):
                if not contracts[contract]['qrvm']['bytecode']['object']:
                    raise RuntimeError(f'No bytecode generated for {contract}')
            reports[f'ir={via_ir},optimize={optimize}'] = contracts
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Reports are compared byte for byte, including the Windows build.
    with args.output.open('w', encoding='utf8', newline='\n') as report:
        report.write(json.dumps(reports, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
