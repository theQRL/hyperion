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
 * Some useful snippets for the optimiser.
 */

#include <libyul/optimiser/OptimizerUtilities.h>

#include <libyul/backends/qrvm/QRVMDialect.h>

#include <libyul/Dialect.h>
#include <libyul/AST.h>

#include <liblangutil/Token.h>
#include <libhyputil/CommonData.h>

#include <range/v3/action/remove_if.hpp>

using namespace hyperion;
using namespace hyperion::langutil;
using namespace hyperion::util;
using namespace hyperion::yul;

void yul::removeEmptyBlocks(Block& _block)
{
	auto isEmptyBlock = [](Statement const& _st) -> bool {
		return std::holds_alternative<Block>(_st) && std::get<Block>(_st).statements.empty();
	};
	ranges::actions::remove_if(_block.statements, isEmptyBlock);
}

bool yul::isRestrictedIdentifier(Dialect const& _dialect, YulString const& _identifier)
{
	return _identifier.empty() || TokenTraits::isYulKeyword(_identifier.str()) || _dialect.reservedIdentifier(_identifier);
}

std::optional<qrvmasm::Instruction> yul::toQRVMInstruction(Dialect const& _dialect, YulString const& _name)
{
	if (auto const* dialect = dynamic_cast<QRVMDialect const*>(&_dialect))
		if (BuiltinFunctionForQRVM const* builtin = dialect->builtin(_name))
			return builtin->instruction;
	return std::nullopt;
}

langutil::QRVMVersion const yul::qrvmVersionFromDialect(Dialect const& _dialect)
{
	if (auto const* dialect = dynamic_cast<QRVMDialect const*>(&_dialect))
		return dialect->qrvmVersion();
	return langutil::QRVMVersion();
}

void StatementRemover::operator()(Block& _block)
{
	util::iterateReplacing(
		_block.statements,
		[&](Statement& _statement) -> std::optional<std::vector<Statement>>
		{
			if (m_toRemove.count(&_statement))
				return {std::vector<Statement>{}};
			else
				return std::nullopt;
		}
	);
	ASTModifier::operator()(_block);
}
