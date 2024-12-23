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
import subprocess
from abc import ABCMeta
from abc import abstractmethod
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from shutil import rmtree
from tempfile import mkdtemp
from textwrap import dedent
from typing import List
from typing import Set

from test_helpers import download_project
from test_helpers import get_hypc_short_version
from test_helpers import parse_command_line
from test_helpers import parse_custom_presets
from test_helpers import parse_hypc_version
from test_helpers import replace_version_pragmas
from test_helpers import settings_from_preset
from test_helpers import SettingsPreset

CURRENT_ZVM_VERSION: str = "shanghai"

@dataclass
class TestConfig:
    name: str
    repo_url: str
    ref_type: str
    ref: str
    compile_only_presets: List[SettingsPreset] = field(default_factory=list)
    settings_presets: List[SettingsPreset] = field(default_factory=lambda: list(SettingsPreset))
    zvm_version: str = field(default=CURRENT_ZVM_VERSION)

    def selected_presets(self) -> Set[SettingsPreset]:
        return set(self.compile_only_presets + self.settings_presets)


class BaseRunner(metaclass=ABCMeta):
    config: TestConfig
    hypc_binary_type: str
    hypc_binary_path: Path
    presets: Set[SettingsPreset]

    def __init__(self, argv, config: TestConfig):
        args = parse_command_line(f"{config.name} external tests", argv)
        self.config = config
        self.hypc_binary_type = args.hypc_binary_type
        self.hypc_binary_path = args.hypc_binary_path
        self.presets = parse_custom_presets(args.selected_presets) if args.selected_presets else config.selected_presets()
        self.env = os.environ.copy()
        self.tmp_dir = mkdtemp(prefix=f"ext-test-{config.name}-")
        self.test_dir = Path(self.tmp_dir) / "ext"

    def setup_hypc(self) -> str:
        if self.hypc_binary_type == "hypcjs":
            # TODO: add support to hypc-js
            raise NotImplementedError()
        print("Setting up hypc...")
        hypc_version_output = subprocess.check_output(
            [self.hypc_binary_path, "--version"],
            shell=False,
            encoding="utf-8"
        ).split(":")[1]
        return parse_hypc_version(hypc_version_output)

    @staticmethod
    def enter_test_dir(fn):
        """Run a function inside the test directory"""

        previous_dir = os.getcwd()
        def f(self, *args, **kwargs):
            try:
                assert self.test_dir is not None
                os.chdir(self.test_dir)
                return fn(self, *args, **kwargs)
            finally:
                # Restore the previous directory after execute fn
                os.chdir(previous_dir)
        return f

    def setup_environment(self):
        """Configure the project build environment"""
        print("Configuring Runner building environment...")
        replace_version_pragmas(self.test_dir)

    @enter_test_dir
    def clean(self):
        """Clean temporary directories"""
        rmtree(self.tmp_dir)

    @enter_test_dir
    @abstractmethod
    def configure(self):
        raise NotImplementedError()

    @enter_test_dir
    @abstractmethod
    def compile(self, preset: SettingsPreset):
        raise NotImplementedError()

    @enter_test_dir
    @abstractmethod
    def run_test(self):
        raise NotImplementedError()

def run_test(runner: BaseRunner):
    print(f"Testing {runner.config.name}...\n===========================")
    print(f"Selected settings presets: {' '.join(p.value for p in runner.presets)}")

    # Configure hypc compiler
    hypc_version = runner.setup_hypc()
    print(f"Using compiler version {hypc_version}")

    # Download project
    download_project(runner.test_dir, runner.config.repo_url, runner.config.ref_type, runner.config.ref)

    # Configure run environment
    runner.setup_environment()

    # Configure TestRunner instance
    print(dedent(f"""\
        Configuring runner's profiles with:
        -------------------------------------
        Binary type: {runner.hypc_binary_type}
        Compiler path: {runner.hypc_binary_path}
        -------------------------------------
    """))
    runner.configure()
    for preset in runner.presets:
        print("Running compile function...")
        settings = settings_from_preset(preset, runner.config.zvm_version)
        print(dedent(f"""\
            -------------------------------------
            Settings preset: {preset.value}
            Settings: {settings}
            ZVM version: {runner.config.zvm_version}
            Compiler version: {get_hypc_short_version(hypc_version)}
            Compiler version (full): {hypc_version}
            -------------------------------------
        """))
        runner.compile(preset)
        # TODO: COMPILE_ONLY should be a command-line option
        if os.environ.get("COMPILE_ONLY") == "1" or preset in runner.config.compile_only_presets:
            print("Skipping test function...")
        else:
            print("Running test function...")
            runner.run_test()
        # TODO: store_benchmark_report
    runner.clean()
    print("Done.")
