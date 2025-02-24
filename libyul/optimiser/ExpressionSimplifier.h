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
 * Optimiser component that uses the simplification rules to simplify expressions.
 */

#pragma once

#include <libyul/ASTForward.h>

#include <libyul/optimiser/DataFlowAnalyzer.h>

namespace hyperion::yul
{
struct Dialect;
struct OptimiserStepContext;

/**
 * Applies simplification rules to all expressions.
 * The component will work best if the code is in SSA form, but
 * this is not required for correctness. Using CommonSubexpressionEliminator
 * also helps this component track equivalent sub-expressions.
 *
 * It tracks the current values of variables using the DataFlowAnalyzer
 * and takes them into account for replacements.
 *
 * Prerequisite: Disambiguator, ForLoopInitRewriter.
 */
class ExpressionSimplifier: public DataFlowAnalyzer
{
public:
	static constexpr char const* name{"ExpressionSimplifier"};
	static void run(OptimiserStepContext&, Block& _ast);

	using ASTModifier::operator();
	void visit(Expression& _expression) override;

private:
	explicit ExpressionSimplifier(Dialect const& _dialect):
		DataFlowAnalyzer(_dialect, MemoryAndStorage::Ignore)
	{}
	bool knownToBeZero(Expression const& _expression) const;
};

}
