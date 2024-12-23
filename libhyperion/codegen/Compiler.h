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
 * @author Christian <c@ethdev.com>
 * @date 2014
 * Hyperion AST to ZVM bytecode compiler.
 */

#pragma once

#include <libhyperion/codegen/CompilerContext.h>
#include <libhyperion/interface/OptimiserSettings.h>
#include <libhyperion/interface/DebugSettings.h>
#include <liblangutil/ZVMVersion.h>
#include <libzvmasm/Assembly.h>
#include <functional>
#include <ostream>

namespace hyperion::frontend
{

class Compiler
{
public:
	Compiler(langutil::ZVMVersion _zvmVersion, RevertStrings _revertStrings, OptimiserSettings _optimiserSettings):
		m_optimiserSettings(std::move(_optimiserSettings)),
		m_runtimeContext(_zvmVersion, _revertStrings),
		m_context(_zvmVersion, _revertStrings, &m_runtimeContext)
	{ }

	/// Compiles a contract.
	/// @arg _metadata contains the to be injected metadata CBOR
	void compileContract(
		ContractDefinition const& _contract,
		std::map<ContractDefinition const*, std::shared_ptr<Compiler const>> const& _otherCompilers,
		bytes const& _metadata
	);
	/// @returns Entire assembly.
	zvmasm::Assembly const& assembly() const { return m_context.assembly(); }
	/// @returns Runtime assembly.
	zvmasm::Assembly const& runtimeAssembly() const { return m_context.assembly().sub(m_runtimeSub); }
	/// @returns Entire assembly as a shared pointer to non-const.
	std::shared_ptr<zvmasm::Assembly> assemblyPtr() const { return m_context.assemblyPtr(); }
	/// @returns Runtime assembly as a shared pointer.
	std::shared_ptr<zvmasm::Assembly> runtimeAssemblyPtr() const;

	std::string generatedYulUtilityCode() const { return m_context.generatedYulUtilityCode(); }
	std::string runtimeGeneratedYulUtilityCode() const { return m_runtimeContext.generatedYulUtilityCode(); }

private:
	OptimiserSettings const m_optimiserSettings;
	CompilerContext m_runtimeContext;
	size_t m_runtimeSub = size_t(-1); ///< Identifier of the runtime sub-assembly, if present.
	CompilerContext m_context;
};

}
