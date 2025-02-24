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

#include <libhyperion/analysis/ControlFlowGraph.h>

#include <libhyperion/analysis/ControlFlowBuilder.h>

using namespace hyperion::langutil;
using namespace hyperion::frontend;

bool CFG::constructFlow(ASTNode const& _astRoot)
{
	_astRoot.accept(*this);
	return !Error::containsErrors(m_errorReporter.errors());
}


bool CFG::visit(FunctionDefinition const& _function)
{
	if (_function.isImplemented() && _function.isFree())
		m_functionControlFlow[{nullptr, &_function}] = ControlFlowBuilder::createFunctionFlow(
			m_nodeContainer,
			_function,
			nullptr /* _contract */
		);
	return false;
}

bool CFG::visit(ContractDefinition const& _contract)
{
	for (ContractDefinition const* contract: _contract.annotation().linearizedBaseContracts)
		for (FunctionDefinition const* function: contract->definedFunctions())
			if (function->isImplemented())
				m_functionControlFlow[{&_contract, function}] =
					ControlFlowBuilder::createFunctionFlow(m_nodeContainer, *function, &_contract);

	return true;
}

FunctionFlow const& CFG::functionFlow(FunctionDefinition const& _function, ContractDefinition const* _contract) const
{
	return *m_functionControlFlow.at({_contract, &_function});
}

CFGNode* CFG::NodeContainer::newNode()
{
	m_nodes.emplace_back(std::make_unique<CFGNode>());
	return m_nodes.back().get();
}
