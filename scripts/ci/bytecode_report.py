#!/usr/bin/env python3
"""Compare actual code generation, metadata and linking across native/WASM builds.

Compiles test/ci/bytecode.hyp and every semantic test in each pipeline the semantic tests run in,
without and with the optimizer, and records a digest of each contract's metadata and bytecode.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SEMANTIC_TESTS = ROOT / 'test/libhyperion/semanticTests'


def read_test(path):
    """Split a test into its sources and settings as test/TestCaseReader.cpp does."""
    sources, settings = {}, {}
    name, content = '', ''
    in_sources = True
    lines = path.read_bytes().decode('utf8').split('\n')
    if lines[-1] == '':
        lines.pop()
    for line in lines:
        if line.startswith('// ----'):
            break
        if line.startswith('// ===='):
            in_sources = False
        elif in_sources:
            if line.startswith('==== Source:') and line.endswith('===='):
                if name or content:
                    sources[name] = content
                name, content = line[len('==== Source:'):-len('====')].strip(), ''
            elif line.startswith('==== ExternalSource:') and line.endswith('===='):
                spec = line[len('==== ExternalSource:'):-len('====')].strip()
                source_name, _, target = spec.partition('=')
                target = target.strip() or source_name
                sources[source_name.strip()] = (path.parent / target).read_bytes().decode('utf8')
            else:
                content += line + '\n'
        elif line.startswith('// '):
            key, _, value = line[len('// '):].partition(':')
            settings[key.strip()] = value.strip()
    sources[name] = content
    return sources, settings


def corpus():
    """Yield (test, sources, settings) for the fixture and every test hyptest runs."""
    fixture = ROOT / 'test/ci/bytecode.hyp'
    yield 'test/ci/bytecode.hyp', {'bytecode.hyp': fixture.read_bytes().decode('utf8')}, {}
    for path in sorted(SEMANTIC_TESTS.rglob('*.hyp')):
        relative = path.relative_to(SEMANTIC_TESTS)
        # test/Common.cpp skips directories and files starting with an underscore.
        if not any(part.startswith('_') for part in relative.parts):
            yield (path.relative_to(ROOT).as_posix(), *read_test(path))


def jobs():
    """Yield (configuration, test, standard JSON input) in the pipelines each test runs in."""
    for test, sources, settings in corpus():
        # QRVMVersion gates only whether hyptest executes a test; every test is compiled here.
        pipelines = {'true': [True], 'false': [False]}.get(settings.get('compileViaYul'), [False, True])
        for via_ir in pipelines:
            for optimize in (False, True):
                compiler_settings = {
                    'viaIR': via_ir,
                    'optimizer': {'enabled': optimize},
                    'outputSelection': {'*': {'*': ['metadata', 'qrvm.bytecode', 'qrvm.deployedBytecode']}},
                }
                if 'revertStrings' in settings:
                    compiler_settings['debug'] = {'revertStrings': settings['revertStrings']}
                compiler_input = {
                    'language': 'Hyperion',
                    'sources': {name: {'content': content} for name, content in sources.items()},
                    'settings': compiler_settings,
                }
                yield f'ir={via_ir},optimize={optimize}', test, json.dumps(compiler_input)


def run(command, stdin, timeout):
    result = subprocess.run(command, input=stdin, encoding='utf8', capture_output=True, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f'{" ".join(command)} exited with {result.returncode}:\n{result.stderr}')
    return result.stdout


def compile_native(compiler, inputs, workers, arch=None):
    # arch(1) runs one slice of a universal macOS binary, such as x86_64 under Rosetta 2.
    command = (['arch', f'-{arch}'] if arch else []) + [str(compiler), '--standard-json']
    def compile_one(compiler_input):
        return json.loads(run(command, compiler_input, 180))
    with ThreadPoolExecutor(workers) as pool:
        return list(pool.map(compile_one, inputs))


def compile_hypcjs(compiler, inputs, workers):
    # Each Node process loads the compiler once and compiles a share of the inputs.
    def compile_chunk(chunk):
        command = ['node', str(ROOT / 'scripts/ci/compile_hypcjs.js'), str(compiler)]
        output = run(command, '\n'.join(chunk) + '\n', 3600)
        # Split on \n only: JSON.stringify leaves U+2028, U+2029 and U+0085 unescaped.
        return [json.loads(line) for line in output.split('\n') if line]
    with ThreadPoolExecutor(workers) as pool:
        chunks = list(pool.map(compile_chunk, [inputs[i::workers] for i in range(workers)]))
    return [chunks[i % workers][i // workers] for i in range(len(inputs))]


def digest(value):
    if not isinstance(value, str):
        value = json.dumps(value, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(value.encode('utf8')).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--hypcjs', action='store_true')
    parser.add_argument('--arch', help='slice of a universal macOS compiler to run, such as x86_64')
    args = parser.parse_args()
    if args.hypcjs and args.arch:
        parser.error('--arch applies only to a native compiler')
    configurations, tests, inputs = zip(*jobs())
    workers = os.cpu_count() or 2
    if args.hypcjs:
        outputs = compile_hypcjs(args.compiler.resolve(), inputs, workers)
    else:
        outputs = compile_native(args.compiler.resolve(), inputs, workers, args.arch)
    reports = {}
    for configuration, test, output in zip(configurations, tests, outputs):
        errors = [error for error in output.get('errors', []) if error['severity'] == 'error']
        if errors:
            raise RuntimeError(f'{test} ({configuration}):\n' + json.dumps(errors, indent=2))
        contracts = {
            source: {
                name: {
                    'bytecode': digest(contract['qrvm']['bytecode']),
                    'deployedBytecode': digest(contract['qrvm']['deployedBytecode']),
                    'metadata': digest(contract['metadata']),
                }
                for name, contract in source_contracts.items()
            }
            for source, source_contracts in output.get('contracts', {}).items()
        }
        if not any(contract['qrvm']['bytecode']['object']
                   for source_contracts in output.get('contracts', {}).values()
                   for contract in source_contracts.values()):
            raise RuntimeError(f'No bytecode generated for {test} ({configuration})')
        reports.setdefault(configuration, {})[test] = contracts
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Reports are compared byte for byte, including the Windows build.
    with args.output.open('w', encoding='utf8', newline='\n') as report:
        report.write(json.dumps(reports, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
