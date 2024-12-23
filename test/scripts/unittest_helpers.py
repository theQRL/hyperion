from pathlib import Path
from typing import Union

LIBHYPERION_TEST_DIR = Path(__file__).parent.parent / 'libhyperion'
FIXTURE_DIR = Path(__file__).parent / 'fixtures'

def load_file(path: Union[Path, str]) -> str:
    # NOTE: newline='' disables newline conversion.
    # We want the file exactly as is because changing even a single byte in the source affects metadata.
    with open(path, 'r', encoding='utf-8', newline='') as f:
        return f.read()

def load_fixture(relative_path: Union[Path, str]) -> str:
    return load_file(FIXTURE_DIR / relative_path)

def load_libhyperion_test_case(relative_path: Union[Path, str]) -> str:
    return load_file(LIBHYPERION_TEST_DIR / relative_path)
