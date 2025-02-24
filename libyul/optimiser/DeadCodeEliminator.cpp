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
 * Optimisation stage that removes unreachable code.
 */

#include <libyul/optimiser/DeadCodeEliminator.h>
#include <libyul/optimiser/Semantics.h>
#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/ControlFlowSideEffectsCollector.h>
#include <libyul/AST.h>

#include <libzvmasm/SemanticInformation.h>

#include <algorithm>
#include <limits>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::yul;

void DeadCodeEliminator::run(OptimiserStepContext& _context, Block& _ast)
{
	ControlFlowSideEffectsCollector sideEffects(_context.dialect, _ast);
	DeadCodeEliminator{
		_context.dialect,
		sideEffects.functionSideEffectsNamed()
	}(_ast);
}

void DeadCodeEliminator::operator()(ForLoop& _for)
{
	yulAssert(_for.pre.statements.empty(), "DeadCodeEliminator needs ForLoopInitRewriter as a prerequisite.");
	ASTModifier::operator()(_for);
}

void DeadCodeEliminator::operator()(Block& _block)
{
	TerminationFinder::ControlFlow controlFlowChange;
	size_t index;
	std::tie(controlFlowChange, index) = TerminationFinder{m_dialect, &m_functionSideEffects}.firstUnconditionalControlFlowChange(_block.statements);

	// Erase everything after the terminating statement that is not a function definition.
	if (controlFlowChange != TerminationFinder::ControlFlow::FlowOut && index != std::numeric_limits<size_t>::max())
		_block.statements.erase(
			remove_if(
				_block.statements.begin() + static_cast<ptrdiff_t>(index) + 1,
				_block.statements.end(),
				[] (Statement const& _s) { return !std::holds_alternative<yul::FunctionDefinition>(_s); }
			),
			_block.statements.end()
		);

	ASTModifier::operator()(_block);
}
