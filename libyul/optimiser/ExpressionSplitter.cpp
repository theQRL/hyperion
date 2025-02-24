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
 * Optimiser component that turns complex expressions into multiple variable
 * declarations.
 */

#include <libyul/optimiser/ExpressionSplitter.h>

#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/optimiser/TypeInfo.h>

#include <libyul/AST.h>
#include <libyul/Dialect.h>

#include <libhyputil/CommonData.h>

using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;
using namespace hyperion::langutil;

void ExpressionSplitter::run(OptimiserStepContext& _context, Block& _ast)
{
	TypeInfo typeInfo(_context.dialect, _ast);
	ExpressionSplitter{_context.dialect, _context.dispenser, typeInfo}(_ast);
}

void ExpressionSplitter::operator()(FunctionCall& _funCall)
{
	BuiltinFunction const* builtin = m_dialect.builtin(_funCall.functionName.name);

	for (size_t i = _funCall.arguments.size(); i > 0; i--)
		if (!builtin || !builtin->literalArgument(i - 1))
			outlineExpression(_funCall.arguments[i - 1]);
}

void ExpressionSplitter::operator()(If& _if)
{
	outlineExpression(*_if.condition);
	(*this)(_if.body);
}

void ExpressionSplitter::operator()(Switch& _switch)
{
	outlineExpression(*_switch.expression);
	for (auto& _case: _switch.cases)
		// Do not visit the case expression, nothing to split there.
		(*this)(_case.body);
}

void ExpressionSplitter::operator()(ForLoop& _loop)
{
	(*this)(_loop.pre);
	// Do not visit the condition because we cannot split expressions there.
	(*this)(_loop.post);
	(*this)(_loop.body);
}

void ExpressionSplitter::operator()(Block& _block)
{
	std::vector<Statement> saved;
	swap(saved, m_statementsToPrefix);

	std::function<std::optional<std::vector<Statement>>(Statement&)> f =
			[&](Statement& _statement) -> std::optional<std::vector<Statement>> {
		m_statementsToPrefix.clear();
		visit(_statement);
		if (m_statementsToPrefix.empty())
			return {};
		m_statementsToPrefix.emplace_back(std::move(_statement));
		return std::move(m_statementsToPrefix);
	};
	iterateReplacing(_block.statements, f);

	swap(saved, m_statementsToPrefix);
}

void ExpressionSplitter::outlineExpression(Expression& _expr)
{
	if (std::holds_alternative<Identifier>(_expr))
		return;

	visit(_expr);

	std::shared_ptr<DebugData const> debugData = debugDataOf(_expr);
	YulString var = m_nameDispenser.newName({});
	YulString type = m_typeInfo.typeOf(_expr);
	m_statementsToPrefix.emplace_back(VariableDeclaration{
		debugData,
		{{TypedName{debugData, var, type}}},
		std::make_unique<Expression>(std::move(_expr))
	});
	_expr = Identifier{debugData, var};
	m_typeInfo.setVariableType(var, type);
}

