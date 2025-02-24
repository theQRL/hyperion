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

#include <libhyperion/ast/ASTVisitor.h>

#include <vector>
#include <set>

namespace hyperion::frontend::smt
{

/**
 * This class computes information about which variables are modified in a certain subtree.
 */
class VariableUsage: private ASTConstVisitor
{
public:
	/// @param _outerCallstack the current callstack in the callers context.
	std::set<VariableDeclaration const*> touchedVariables(ASTNode const& _node, std::vector<CallableDeclaration const*> const& _outerCallstack);

	/// Sets whether to inline function calls.
	void setFunctionInlining(std::function<bool(FunctionCall const&, ContractDefinition const*, ContractDefinition const*)> _inlineFunctionCalls) { m_inlineFunctionCalls = _inlineFunctionCalls; }

	void setCurrentContract(ContractDefinition const& _contract) { m_currentContract = &_contract; }

private:
	void endVisit(Identifier const& _node) override;
	void endVisit(IndexAccess const& _node) override;
	void endVisit(FunctionCall const& _node) override;
	bool visit(FunctionDefinition const& _node) override;
	void endVisit(FunctionDefinition const& _node) override;
	void endVisit(ModifierInvocation const& _node) override;
	void endVisit(PlaceholderStatement const& _node) override;

	/// Checks whether an identifier should be added to touchedVariables.
	void checkIdentifier(Identifier const& _identifier);

	ContractDefinition const* currentScopeContract();

	std::set<VariableDeclaration const*> m_touchedVariables;
	std::vector<CallableDeclaration const*> m_callStack;
	CallableDeclaration const* m_lastCall = nullptr;
	ContractDefinition const* m_currentContract = nullptr;

	std::function<bool(FunctionCall const&, ContractDefinition const*, ContractDefinition const*)> m_inlineFunctionCalls = [](FunctionCall const&, ContractDefinition const*, ContractDefinition const*) { return false; };
};

}
