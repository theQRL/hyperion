#!/usr/bin/env python3

# ------------------------------------------------------------------------------
# This file is part of hyperion.
#
# hyperion is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# hyperion is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with hyperion.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2023 hyperion contributors.
# ------------------------------------------------------------------------------

import os
import re
import subprocess
import sys
from argparse import ArgumentParser
from enum import Enum
from pathlib import Path
from typing import List
from typing import Set

# Our scripts/ is not a proper Python package so we need to modify PYTHONPATH to import from it
# pragma pylint: disable=import-error,wrong-import-position
PROJECT_ROOT = Path(__file__).parents[2]
sys.path.insert(0, f"{PROJECT_ROOT}/scripts/common")

from git_helpers import git_commit_hash

HYPC_FULL_VERSION_REGEX = re.compile(r"^[a-zA-Z: ]*(.*)$")
HYPC_SHORT_VERSION_REGEX = re.compile(r"^([0-9.]+).*\+|\-$")


class SettingsPreset(Enum):
    LEGACY_NO_OPTIMIZE = 'legacy-no-optimize'
    IR_NO_OPTIMIZE = 'ir-no-optimize'
    LEGACY_OPTIMIZE_ZVM_ONLY = 'legacy-optimize-zvm-only'
    IR_OPTIMIZE_ZVM_ONLY = 'ir-optimize-zvm-only'
    LEGACY_OPTIMIZE_ZVM_YUL = 'legacy-optimize-zvm+yul'
    IR_OPTIMIZE_ZVM_YUL = 'ir-optimize-zvm+yul'


def compiler_settings(zvm_version: str, via_ir: bool = False, optimizer: bool = False, yul: bool = False) -> dict:
    return {
        "optimizer": {"enabled": optimizer, "details": {"yul": yul}},
        "zvmVersion": zvm_version,
        "viaIR": via_ir,
    }


def settings_from_preset(preset: SettingsPreset, zvm_version: str) -> dict:
    return {
        SettingsPreset.LEGACY_NO_OPTIMIZE:       compiler_settings(zvm_version),
        SettingsPreset.IR_NO_OPTIMIZE:           compiler_settings(zvm_version, via_ir=True),
        SettingsPreset.LEGACY_OPTIMIZE_ZVM_ONLY: compiler_settings(zvm_version, optimizer=True),
        SettingsPreset.IR_OPTIMIZE_ZVM_ONLY:     compiler_settings(zvm_version, via_ir=True, optimizer=True),
        SettingsPreset.LEGACY_OPTIMIZE_ZVM_YUL:  compiler_settings(zvm_version, optimizer=True, yul=True),
        SettingsPreset.IR_OPTIMIZE_ZVM_YUL:      compiler_settings(zvm_version, via_ir=True, optimizer=True, yul=True),
    }[preset]


def parse_custom_presets(presets: List[str]) -> Set[SettingsPreset]:
    return {SettingsPreset(p) for p in presets}

def parse_command_line(description: str, args: List[str]):
    arg_parser = ArgumentParser(description)
    arg_parser.add_argument(
        "hypc_binary_type",
        metavar="hypc-binary-type",
        type=str,
        default="native",
        choices=["native", "hypcjs"],
        help="""Hyperion compiler binary type""",
    )
    arg_parser.add_argument(
        "hypc_binary_path",
        metavar="hypc-binary-path",
        type=Path,
        help="""Path to hypc binary""",
    )
    arg_parser.add_argument(
        "selected_presets",
        metavar="selected-presets",
        help="""List of compiler settings presets""",
        nargs='*',
    )
    return arg_parser.parse_args(args)


def download_project(test_dir: Path, repo_url: str, ref_type: str = "branch", ref: str = "master"):
    assert ref_type in ("commit", "branch", "tag")

    print(f"Cloning {ref_type} {ref} of {repo_url}...")
    if ref_type == "commit":
        os.mkdir(test_dir)
        os.chdir(test_dir)
        subprocess.run(["git", "init"], check=True)
        subprocess.run(["git", "remote", "add", "origin", repo_url], check=True)
        subprocess.run(["git", "fetch", "--depth", "1", "origin", ref], check=True)
        subprocess.run(["git", "reset", "--hard", "FETCH_HEAD"], check=True)
    else:
        os.chdir(test_dir.parent)
        subprocess.run(["git", "clone", "--no-progress", "--depth", "1", repo_url, "-b", ref, test_dir.resolve()], check=True)
        if not test_dir.exists():
            raise RuntimeError("Failed to clone the project.")
        os.chdir(test_dir)

    if (test_dir / ".gitmodules").exists():
        subprocess.run(["git", "submodule", "update", "--init"], check=True)

    print(f"Current commit hash: {git_commit_hash()}")


def parse_hypc_version(hypc_version_string: str) -> str:
    hypc_version_match = re.search(HYPC_FULL_VERSION_REGEX, hypc_version_string)
    if hypc_version_match is None:
        raise RuntimeError(f"Hypc version could not be found in: {hypc_version_string}.")
    return hypc_version_match.group(1)


def get_hypc_short_version(hypc_full_version: str) -> str:
    hypc_short_version_match = re.search(HYPC_SHORT_VERSION_REGEX, hypc_full_version)
    if hypc_short_version_match is None:
        raise RuntimeError(f"Error extracting short version string from: {hypc_full_version}.")
    return hypc_short_version_match.group(1)


def store_benchmark_report(self):
    raise NotImplementedError()


def replace_version_pragmas(test_dir: Path):
    """
    Replace fixed-version pragmas (part of Consensys best practice).
    Include all directories to also cover node dependencies.
    """
    print("Replacing fixed-version pragmas...")
    for source in test_dir.glob("**/*.hyp"):
        content = source.read_text(encoding="utf-8")
        content = re.sub(r"pragma hyperion [^;]+;", r"pragma hyperion >=0.0;", content)
        with open(source, "w", encoding="utf-8") as f:
            f.write(content)
