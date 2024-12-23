/*
	This file is part of hyperion.

	hyperion is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	hyperion is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with hyperion.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <libyul/YulStack.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/ZVMVersion.h>

using namespace hyperion;
using namespace hyperion::langutil;
using namespace hyperion::util;
using namespace hyperion::yul;
using namespace std;

// Prototype as we can't use the FuzzerInterface.h header.
extern "C" int LLVMFuzzerTestOneInput(uint8_t const* _data, size_t _size);

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* _data, size_t _size)
{
	if (_size > 600)
		return 0;

	YulStringRepository::reset();

	string input(reinterpret_cast<char const*>(_data), _size);
	YulStack stack(
		langutil::ZVMVersion(),
		YulStack::Language::StrictAssembly,
		hyperion::frontend::OptimiserSettings::full(),
		DebugInfoSelection::All()
	);

	if (!stack.parseAndAnalyze("source", input))
		return 0;

	stack.optimize();
	return 0;
}
