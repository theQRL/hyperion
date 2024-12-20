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

#pragma once

#include <libzvmasm/AbstractAssemblyStack.h>
#include <libzvmasm/Assembly.h>
#include <libzvmasm/LinkerObject.h>

#include <libhyputil/JSON.h>

#include <map>
#include <string>

namespace hyperion::zvmasm
{

class ZVMAssemblyStack: public AbstractAssemblyStack
{
public:
	explicit ZVMAssemblyStack(langutil::ZVMVersion _zvmVersion): m_zvmVersion(_zvmVersion) {}

	/// Runs parsing and analysis steps.
	/// Multiple calls overwrite the previous state.
	/// @throws AssemblyImportException, if JSON could not be validated.
	void parseAndAnalyze(std::string const& _sourceName, std::string const& _source);

	void assemble();

	std::string const& name() const { return m_name; }

	virtual LinkerObject const& object(std::string const& _contractName) const override;
	virtual LinkerObject const& runtimeObject(std::string const& _contractName) const override;

	std::shared_ptr<zvmasm::Assembly> const& zvmAssembly() const { return m_zvmAssembly; }
	std::shared_ptr<zvmasm::Assembly> const& zvmRuntimeAssembly() const { return m_zvmRuntimeAssembly; }

	virtual std::string const* sourceMapping(std::string const& _contractName) const override;
	virtual std::string const* runtimeSourceMapping(std::string const& _contractName) const override;

	virtual Json::Value assemblyJSON(std::string const& _contractName) const override;
	virtual std::string assemblyString(std::string const& _contractName, StringMap const& _sourceCodes) const override;

	virtual std::string const filesystemFriendlyName(std::string const& _contractName) const override;

	virtual std::vector<std::string> contractNames() const override { return {m_name}; }
	virtual std::vector<std::string> sourceNames() const override;
	std::map<std::string, unsigned> sourceIndices() const;

	virtual bool compilationSuccessful() const override { return m_zvmAssembly != nullptr; }

	void selectDebugInfo(langutil::DebugInfoSelection _debugInfoSelection)
	{
		m_debugInfoSelection = _debugInfoSelection;
	}

private:
	langutil::ZVMVersion m_zvmVersion;
	std::string m_name;
	std::shared_ptr<zvmasm::Assembly> m_zvmAssembly;
	std::shared_ptr<zvmasm::Assembly> m_zvmRuntimeAssembly;
	zvmasm::LinkerObject m_object; ///< Deployment object (includes the runtime sub-object).
	zvmasm::LinkerObject m_runtimeObject; ///< Runtime object.
	std::vector<std::string> m_sourceList;
	langutil::DebugInfoSelection m_debugInfoSelection = langutil::DebugInfoSelection::Default();
	std::string m_sourceMapping;
	std::string m_runtimeSourceMapping;
};

} // namespace hyperion::zvmasm
