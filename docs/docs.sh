#!/usr/bin/env bash

#------------------------------------------------------------------------------
# Bash script to build the Hyperion Sphinx documentation locally.
#
# The documentation for hyperion is hosted at:
#
#     https://docs.soliditylang.org
#
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
# (c) 2016 hyperion contributors.
#------------------------------------------------------------------------------

set -euo pipefail

script_dir="$(dirname "$0")"

cd "${script_dir}"
pip3 install -r requirements.txt --upgrade --upgrade-strategy eager
sphinx-build -nW -b html -d _build/doctrees . _build/html
