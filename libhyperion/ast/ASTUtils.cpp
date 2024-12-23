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

#include <libhyperion/ast/AST.h>
#include <libhyperion/ast/ASTUtils.h>
#include <libhyperion/ast/ASTVisitor.h>

#include <libhyputil/Algorithms.h>

namespace hyperion::frontend
{

ASTNode const* locateInnermostASTNode(int _offsetInFile, SourceUnit const& _sourceUnit)
{
	ASTNode const* innermostMatch = nullptr;
	auto locator = SimpleASTVisitor(
		[&](ASTNode const& _node) -> bool
		{
			// In the AST parent location always covers the whole child location.
			// The parent is visited first so to get the innermost node we simply
			// take the last one that still contains the offset.

			if (!_node.location().containsOffset(_offsetInFile))
				return false;

			innermostMatch = &_node;
			return true;
		},
		[](ASTNode const&) {}
	);
	_sourceUnit.accept(locator);
	return innermostMatch;
}

bool isConstantVariableRecursive(VariableDeclaration const& _varDecl)
{
	hypAssert(_varDecl.isConstant(), "Constant variable expected");

	auto visitor = [](VariableDeclaration const& _variable, util::CycleDetector<VariableDeclaration>& _cycleDetector, size_t _depth)
	{
		hypAssert(_depth < 256, "Recursion depth limit reached");
		if (!_variable.value())
			// This should result in an error later on.
			return;

		if (auto referencedVarDecl = dynamic_cast<VariableDeclaration const*>(
			ASTNode::referencedDeclaration(*_variable.value()))
		)
			if (referencedVarDecl->isConstant())
				_cycleDetector.run(*referencedVarDecl);
	};

	return util::CycleDetector<VariableDeclaration>(visitor).run(_varDecl) != nullptr;
}

VariableDeclaration const* rootConstVariableDeclaration(VariableDeclaration const& _varDecl)
{
	hypAssert(_varDecl.isConstant(), "Constant variable expected");
	hypAssert(!isConstantVariableRecursive(_varDecl), "Recursive declaration");

	VariableDeclaration const* rootDecl = &_varDecl;
	Identifier const* identifier;
	while ((identifier = dynamic_cast<Identifier const*>(rootDecl->value().get())))
	{
		auto referencedVarDecl = dynamic_cast<VariableDeclaration const*>(identifier->annotation().referencedDeclaration);
		if (!referencedVarDecl || !referencedVarDecl->isConstant())
			return nullptr;
		rootDecl = referencedVarDecl;
	}
	return rootDecl;
}

Expression const* resolveOuterUnaryTuples(Expression const* _expr)
{
	while (auto const* tupleExpression = dynamic_cast<TupleExpression const*>(_expr))
		if (tupleExpression->components().size() == 1)
			_expr = tupleExpression->components().front().get();
		else
			break;
	return _expr;
}

}
