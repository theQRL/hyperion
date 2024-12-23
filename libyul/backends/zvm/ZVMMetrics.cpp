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
* Module providing metrics for the ZVM optimizer.
*/

#include <libyul/backends/zvm/ZVMMetrics.h>

#include <libyul/AST.h>
#include <libyul/Exceptions.h>
#include <libyul/Utilities.h>
#include <libyul/backends/zvm/ZVMDialect.h>

#include <libzvmasm/Instruction.h>
#include <libzvmasm/GasMeter.h>

#include <libhyputil/CommonData.h>

using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;

bigint GasMeter::costs(Expression const& _expression) const
{
	return combineCosts(GasMeterVisitor::costs(_expression, m_dialect, m_isCreation));
}

bigint GasMeter::instructionCosts(zvmasm::Instruction _instruction) const
{
	return combineCosts(GasMeterVisitor::instructionCosts(_instruction, m_dialect, m_isCreation));
}

bigint GasMeter::combineCosts(std::pair<bigint, bigint> _costs) const
{
	return _costs.first * m_runs + _costs.second;
}


std::pair<bigint, bigint> GasMeterVisitor::costs(
	Expression const& _expression,
	ZVMDialect const& _dialect,
	bool _isCreation
)
{
	GasMeterVisitor gmv(_dialect, _isCreation);
	gmv.visit(_expression);
	return {gmv.m_runGas, gmv.m_dataGas};
}

std::pair<bigint, bigint> GasMeterVisitor::instructionCosts(
	zvmasm::Instruction _instruction,
	ZVMDialect const& _dialect,
	bool _isCreation
)
{
	GasMeterVisitor gmv(_dialect, _isCreation);
	gmv.instructionCostsInternal(_instruction);
	return {gmv.m_runGas, gmv.m_dataGas};
}

void GasMeterVisitor::operator()(FunctionCall const& _funCall)
{
	ASTWalker::operator()(_funCall);
	if (BuiltinFunctionForZVM const* f = m_dialect.builtin(_funCall.functionName.name))
		if (f->instruction)
		{
			instructionCostsInternal(*f->instruction);
			return;
		}
	yulAssert(false, "Functions not implemented.");
}

void GasMeterVisitor::operator()(Literal const& _lit)
{
	m_runGas += zvmasm::GasMeter::runGas(zvmasm::Instruction::PUSH1);
	m_dataGas +=
		singleByteDataGas() +
		zvmasm::GasMeter::dataGas(
			toCompactBigEndian(valueOfLiteral(_lit), 1),
			m_isCreation
		);
}

void GasMeterVisitor::operator()(Identifier const&)
{
	m_runGas += zvmasm::GasMeter::runGas(zvmasm::Instruction::DUP1);
	m_dataGas += singleByteDataGas();
}

bigint GasMeterVisitor::singleByteDataGas() const
{
	if (m_isCreation)
		return zvmasm::GasCosts::txDataNonZeroGas;
	else
		return zvmasm::GasCosts::createDataGas;
}

void GasMeterVisitor::instructionCostsInternal(zvmasm::Instruction _instruction)
{
	if (_instruction == zvmasm::Instruction::EXP)
		m_runGas += zvmasm::GasCosts::expGas + zvmasm::GasCosts::expByteGas;
	else if (_instruction == zvmasm::Instruction::KECCAK256)
		// Assumes that Keccak-256 is computed on a single word (rounded up).
		m_runGas += zvmasm::GasCosts::keccak256Gas + zvmasm::GasCosts::keccak256WordGas;
	else
		m_runGas += zvmasm::GasMeter::runGas(_instruction);
	m_dataGas += singleByteDataGas();
}
