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

#include <libhyperion/formal/VariableUsage.h>

#include <libhyperion/formal/BMC.h>
#include <libhyperion/formal/SMTEncoder.h>

#include <range/v3/view.hpp>

#include <algorithm>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::frontend;
using namespace hyperion::frontend::smt;

std::set<VariableDeclaration const*> VariableUsage::touchedVariables(ASTNode const& _node, std::vector<CallableDeclaration const*> const& _outerCallstack)
{
	m_touchedVariables.clear();
	m_callStack.clear();
	m_callStack += _outerCallstack;
	if (!m_callStack.empty())
		m_lastCall = m_callStack.back();
	_node.accept(*this);
	return m_touchedVariables;
}

void VariableUsage::endVisit(Identifier const& _identifier)
{
	if (_identifier.annotation().willBeWrittenTo)
		checkIdentifier(_identifier);
}

void VariableUsage::endVisit(IndexAccess const& _indexAccess)
{
	if (_indexAccess.annotation().willBeWrittenTo)
	{
		/// identifier.annotation().willBeWrittenTo == false, that's why we
		/// need to check that before.
		auto identifier = dynamic_cast<Identifier const*>(SMTEncoder::leftmostBase(_indexAccess));
		if (identifier)
			checkIdentifier(*identifier);
	}
}

void VariableUsage::endVisit(FunctionCall const& _funCall)
{
	auto scopeContract = currentScopeContract();
	if (m_inlineFunctionCalls(_funCall, scopeContract, m_currentContract))
		if (auto funDef = SMTEncoder::functionCallToDefinition(_funCall, scopeContract, m_currentContract))
			if (find(m_callStack.begin(), m_callStack.end(), funDef) == m_callStack.end())
				funDef->accept(*this);
}

bool VariableUsage::visit(FunctionDefinition const& _function)
{
	m_callStack.push_back(&_function);
	return true;
}

void VariableUsage::endVisit(FunctionDefinition const&)
{
	hypAssert(!m_callStack.empty(), "");
	m_callStack.pop_back();
}

void VariableUsage::endVisit(ModifierInvocation const& _modifierInv)
{
	auto const& modifierDef = dynamic_cast<ModifierDefinition const*>(_modifierInv.name().annotation().referencedDeclaration);
	if (modifierDef)
		modifierDef->accept(*this);
}

void VariableUsage::endVisit(PlaceholderStatement const&)
{
	hypAssert(!m_callStack.empty(), "");
	FunctionDefinition const* funDef = nullptr;
	for (auto it = m_callStack.rbegin(); it != m_callStack.rend() && !funDef; ++it)
		funDef = dynamic_cast<FunctionDefinition const*>(*it);
	hypAssert(funDef, "");
	if (funDef->isImplemented())
		funDef->body().accept(*this);
}

void VariableUsage::checkIdentifier(Identifier const& _identifier)
{
	Declaration const* declaration = _identifier.annotation().referencedDeclaration;
	hypAssert(declaration, "");
	if (VariableDeclaration const* varDecl = dynamic_cast<VariableDeclaration const*>(declaration))
	{
		if (!varDecl->isLocalVariable() || (m_lastCall && varDecl->functionOrModifierDefinition() == m_lastCall))
			m_touchedVariables.insert(varDecl);
	}
}

ContractDefinition const* VariableUsage::currentScopeContract()
{
	for (auto&& f: m_callStack | ranges::views::reverse)
		if (auto fun = dynamic_cast<FunctionDefinition const*>(f))
			return fun->annotation().contract;
	return nullptr;
}
