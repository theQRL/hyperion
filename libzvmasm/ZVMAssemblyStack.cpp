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

#include <libzvmasm/ZVMAssemblyStack.h>

#include <libhyputil/JSON.h>
#include <liblangutil/Exceptions.h>
#include <libhyperion/codegen/CompilerContext.h>

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

#include <tuple>

using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::frontend;

namespace hyperion::zvmasm
{

void ZVMAssemblyStack::parseAndAnalyze(std::string const& _sourceName, std::string const& _source)
{
	hypAssert(!m_zvmAssembly);
	m_name = _sourceName;
	Json::Value assemblyJson;
	solRequire(jsonParseStrict(_source, assemblyJson), AssemblyImportException, "Could not parse JSON file.");
	std::tie(m_zvmAssembly, m_sourceList) = zvmasm::Assembly::fromJSON(assemblyJson);
	solRequire(m_zvmAssembly != nullptr, AssemblyImportException, "Could not create zvm assembly object.");
}

void ZVMAssemblyStack::assemble()
{
	hypAssert(m_zvmAssembly);
	hypAssert(m_zvmAssembly->isCreation());
	hypAssert(!m_zvmRuntimeAssembly);

	m_object = m_zvmAssembly->assemble();
	m_sourceMapping = AssemblyItem::computeSourceMapping(m_zvmAssembly->items(), sourceIndices());
	if (m_zvmAssembly->numSubs() > 0)
	{
		m_zvmRuntimeAssembly = std::make_shared<zvmasm::Assembly>(m_zvmAssembly->sub(0));
		hypAssert(m_zvmRuntimeAssembly && !m_zvmRuntimeAssembly->isCreation());
		m_runtimeSourceMapping = AssemblyItem::computeSourceMapping(m_zvmRuntimeAssembly->items(), sourceIndices());
		m_runtimeObject = m_zvmRuntimeAssembly->assemble();
	}
}

LinkerObject const& ZVMAssemblyStack::object(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_object;
}

LinkerObject const& ZVMAssemblyStack::runtimeObject(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_runtimeObject;
}

std::map<std::string, unsigned> ZVMAssemblyStack::sourceIndices() const
{
	hypAssert(m_zvmAssembly);
	return m_sourceList
		| ranges::views::enumerate
		| ranges::views::transform([](auto const& _source) { return std::make_pair(_source.second, _source.first); })
		| ranges::to<std::map<std::string, unsigned>>;
}

std::string const* ZVMAssemblyStack::sourceMapping(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return &m_sourceMapping;
}

std::string const* ZVMAssemblyStack::runtimeSourceMapping(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return &m_runtimeSourceMapping;
}

Json::Value ZVMAssemblyStack::assemblyJSON(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	hypAssert(m_zvmAssembly);
	return m_zvmAssembly->assemblyJSON(sourceIndices());
}

std::string ZVMAssemblyStack::assemblyString(std::string const& _contractName, StringMap const& _sourceCodes) const
{
	hypAssert(_contractName == m_name);
	hypAssert(m_zvmAssembly);
	return m_zvmAssembly->assemblyString(m_debugInfoSelection, _sourceCodes);
}

std::string const ZVMAssemblyStack::filesystemFriendlyName(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_name;
}

std::vector<std::string> ZVMAssemblyStack::sourceNames() const
{
	return m_sourceList;
}

} // namespace hyperion::zvmasm
