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
/**
 * Helper to compile Yul code using libzvmasm.
 */

#pragma once

#include <libyul/backends/zvm/AbstractAssembly.h>
#include <libyul/AsmAnalysis.h>
#include <liblangutil/ZVMVersion.h>

namespace hyperion::zvmasm
{
class Assembly;
}

namespace hyperion::yul
{
struct Block;
struct AsmAnalysisInfo;

class CodeGenerator
{
public:
	/// Performs code generation and appends generated to _assembly.
	static void assemble(
		Block const& _parsedData,
		AsmAnalysisInfo& _analysisInfo,
		zvmasm::Assembly& _assembly,
		langutil::ZVMVersion _zvmVersion,
		ExternalIdentifierAccess::CodeGenerator _identifierAccess = {},
		bool _useNamedLabelsForFunctions = false,
		bool _optimizeStackAllocation = false
	);
};
}
