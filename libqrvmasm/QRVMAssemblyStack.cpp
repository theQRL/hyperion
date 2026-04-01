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

#include <libqrvmasm/QRVMAssemblyStack.h>

#include <libhyputil/JSON.h>
#include <liblangutil/Exceptions.h>
#include <libhyperion/codegen/CompilerContext.h>

#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

#include <tuple>

using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::frontend;

namespace hyperion::qrvmasm
{

void QRVMAssemblyStack::parseAndAnalyze(std::string const& _sourceName, std::string const& _source)
{
	hypAssert(!m_qrvmAssembly);
	m_name = _sourceName;
	Json::Value assemblyJson;
	solRequire(jsonParseStrict(_source, assemblyJson), AssemblyImportException, "Could not parse JSON file.");
	std::tie(m_qrvmAssembly, m_sourceList) = qrvmasm::Assembly::fromJSON(assemblyJson);
	solRequire(m_qrvmAssembly != nullptr, AssemblyImportException, "Could not create qrvm assembly object.");
}

void QRVMAssemblyStack::assemble()
{
	hypAssert(m_qrvmAssembly);
	hypAssert(m_qrvmAssembly->isCreation());
	hypAssert(!m_qrvmRuntimeAssembly);

	m_object = m_qrvmAssembly->assemble();
	m_sourceMapping = AssemblyItem::computeSourceMapping(m_qrvmAssembly->items(), sourceIndices());
	if (m_qrvmAssembly->numSubs() > 0)
	{
		m_qrvmRuntimeAssembly = std::make_shared<qrvmasm::Assembly>(m_qrvmAssembly->sub(0));
		hypAssert(m_qrvmRuntimeAssembly && !m_qrvmRuntimeAssembly->isCreation());
		m_runtimeSourceMapping = AssemblyItem::computeSourceMapping(m_qrvmRuntimeAssembly->items(), sourceIndices());
		m_runtimeObject = m_qrvmRuntimeAssembly->assemble();
	}
}

LinkerObject const& QRVMAssemblyStack::object(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_object;
}

LinkerObject const& QRVMAssemblyStack::runtimeObject(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_runtimeObject;
}

std::map<std::string, unsigned> QRVMAssemblyStack::sourceIndices() const
{
	hypAssert(m_qrvmAssembly);
	return m_sourceList
		| ranges::views::enumerate
		| ranges::views::transform([](auto const& _source) { return std::make_pair(_source.second, _source.first); })
		| ranges::to<std::map<std::string, unsigned>>;
}

std::string const* QRVMAssemblyStack::sourceMapping(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return &m_sourceMapping;
}

std::string const* QRVMAssemblyStack::runtimeSourceMapping(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return &m_runtimeSourceMapping;
}

Json::Value QRVMAssemblyStack::assemblyJSON(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	hypAssert(m_qrvmAssembly);
	return m_qrvmAssembly->assemblyJSON(sourceIndices());
}

std::string QRVMAssemblyStack::assemblyString(std::string const& _contractName, StringMap const& _sourceCodes) const
{
	hypAssert(_contractName == m_name);
	hypAssert(m_qrvmAssembly);
	return m_qrvmAssembly->assemblyString(m_debugInfoSelection, _sourceCodes);
}

std::string const QRVMAssemblyStack::filesystemFriendlyName(std::string const& _contractName) const
{
	hypAssert(_contractName == m_name);
	return m_name;
}

std::vector<std::string> QRVMAssemblyStack::sourceNames() const
{
	return m_sourceList;
}

} // namespace hyperion::qrvmasm
