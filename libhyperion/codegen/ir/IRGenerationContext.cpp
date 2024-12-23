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
 * Class that contains contextual information during IR generation.
 */

#include <libhyperion/codegen/ir/IRGenerationContext.h>

#include <libhyperion/codegen/YulUtilFunctions.h>
#include <libhyperion/codegen/ABIFunctions.h>
#include <libhyperion/codegen/CompilerUtils.h>
#include <libhyperion/ast/AST.h>
#include <libhyperion/ast/TypeProvider.h>

#include <libhyputil/Whiskers.h>
#include <libhyputil/StringUtils.h>

#include <range/v3/view/map.hpp>
#include <range/v3/algorithm/find.hpp>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::frontend;

std::string IRGenerationContext::enqueueFunctionForCodeGeneration(FunctionDefinition const& _function)
{
	std::string name = IRNames::function(_function);

	if (!m_functions.contains(name))
		m_functionGenerationQueue.push_back(&_function);

	return name;
}

FunctionDefinition const* IRGenerationContext::dequeueFunctionForCodeGeneration()
{
	hypAssert(!m_functionGenerationQueue.empty(), "");

	FunctionDefinition const* result = m_functionGenerationQueue.front();
	m_functionGenerationQueue.pop_front();
	return result;
}

ContractDefinition const& IRGenerationContext::mostDerivedContract() const
{
	hypAssert(m_mostDerivedContract, "Most derived contract requested but not set.");
	return *m_mostDerivedContract;
}

IRVariable const& IRGenerationContext::addLocalVariable(VariableDeclaration const& _varDecl)
{
	auto const& [it, didInsert] = m_localVariables.emplace(
		std::make_pair(&_varDecl, IRVariable{_varDecl})
	);
	hypAssert(didInsert, "Local variable added multiple times.");
	return it->second;
}

IRVariable const& IRGenerationContext::localVariable(VariableDeclaration const& _varDecl)
{
	hypAssert(
		m_localVariables.count(&_varDecl),
		"Unknown variable: " + _varDecl.name()
	);
	return m_localVariables.at(&_varDecl);
}

void IRGenerationContext::resetLocalVariables()
{
	m_localVariables.clear();
}

void IRGenerationContext::registerImmutableVariable(VariableDeclaration const& _variable)
{
	hypAssert(_variable.immutable(), "Attempted to register a non-immutable variable as immutable.");
	hypUnimplementedAssert(
		_variable.annotation().type->isValueType(),
		"Only immutable variables of value type are supported."
	);
	hypAssert(m_reservedMemory.has_value(), "Reserved memory has already been reset.");
	m_immutableVariables[&_variable] = CompilerUtils::generalPurposeMemoryStart + *m_reservedMemory;
	hypAssert(_variable.annotation().type->memoryHeadSize() == 32, "Memory writes might overlap.");
	*m_reservedMemory += _variable.annotation().type->memoryHeadSize();
}

size_t IRGenerationContext::immutableMemoryOffset(VariableDeclaration const& _variable) const
{
	hypAssert(
		m_immutableVariables.count(&_variable),
		"Unknown immutable variable: " + _variable.name()
	);
	return m_immutableVariables.at(&_variable);
}

size_t IRGenerationContext::reservedMemory()
{
	hypAssert(m_reservedMemory.has_value(), "Reserved memory was used before.");
	size_t reservedMemory = *m_reservedMemory;
	m_reservedMemory = std::nullopt;
	return reservedMemory;
}

void IRGenerationContext::addStateVariable(
	VariableDeclaration const& _declaration,
	u256 _storageOffset,
	unsigned _byteOffset
)
{
	m_stateVariables[&_declaration] = std::make_pair(std::move(_storageOffset), _byteOffset);
}

std::string IRGenerationContext::newYulVariable()
{
	return "_" + std::to_string(++m_varCounter);
}

void IRGenerationContext::initializeInternalDispatch(InternalDispatchMap _internalDispatch)
{
	hypAssert(internalDispatchClean(), "");

	for (DispatchQueue const& functions: _internalDispatch | ranges::views::values)
		for (auto function: functions)
			enqueueFunctionForCodeGeneration(*function);

	m_internalDispatchMap = std::move(_internalDispatch);
}

InternalDispatchMap IRGenerationContext::consumeInternalDispatchMap()
{
	InternalDispatchMap internalDispatch = std::move(m_internalDispatchMap);
	m_internalDispatchMap.clear();
	return internalDispatch;
}

void IRGenerationContext::addToInternalDispatch(FunctionDefinition const& _function)
{
	FunctionType const* functionType = TypeProvider::function(_function, FunctionType::Kind::Internal);
	hypAssert(functionType);

	YulArity arity = YulArity::fromType(*functionType);
	DispatchQueue& dispatchQueue = m_internalDispatchMap[arity];
	if (ranges::find(dispatchQueue, &_function) == ranges::end(dispatchQueue))
	{
		dispatchQueue.push_back(&_function);
		enqueueFunctionForCodeGeneration(_function);
	}
}


void IRGenerationContext::internalFunctionCalledThroughDispatch(YulArity const& _arity)
{
	m_internalDispatchMap.try_emplace(_arity);
}

YulUtilFunctions IRGenerationContext::utils()
{
	return YulUtilFunctions(m_zvmVersion, m_revertStrings, m_functions);
}

ABIFunctions IRGenerationContext::abiFunctions()
{
	return ABIFunctions(m_zvmVersion, m_revertStrings, m_functions);
}
