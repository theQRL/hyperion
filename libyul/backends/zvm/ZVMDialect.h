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
 * Yul dialects for ZVM.
 */

#pragma once

#include <libyul/Dialect.h>

#include <libyul/backends/zvm/AbstractAssembly.h>
#include <libyul/ASTForward.h>
#include <liblangutil/ZVMVersion.h>

#include <map>
#include <set>

namespace hyperion::yul
{

class YulString;
using Type = YulString;
struct FunctionCall;
struct Object;

/**
 * Context used during code generation.
 */
struct BuiltinContext
{
	Object const* currentObject = nullptr;
	/// Mapping from named objects to abstract assembly sub IDs.
	std::map<YulString, AbstractAssembly::SubID> subIDs;
};

struct BuiltinFunctionForZVM: public BuiltinFunction
{
	std::optional<zvmasm::Instruction> instruction;
	/// Function to generate code for the given function call and append it to the abstract
	/// assembly. Expects all non-literal arguments of the call to be on stack in reverse order
	/// (i.e. right-most argument pushed first).
	/// Expects the caller to set the source location.
	std::function<void(FunctionCall const&, AbstractAssembly&, BuiltinContext&)> generateCode;
};


/**
 * Yul dialect for ZVM as a backend.
 * The main difference is that the builtin functions take an AbstractAssembly for the
 * code generation.
 */
struct ZVMDialect: public Dialect
{
	/// Constructor, should only be used internally. Use the factory functions below.
	ZVMDialect(langutil::ZVMVersion _zvmVersion, bool _objectAccess);

	/// @returns the builtin function of the given name or a nullptr if it is not a builtin function.
	BuiltinFunctionForZVM const* builtin(YulString _name) const override;

	/// @returns true if the identifier is reserved. This includes the builtins too.
	bool reservedIdentifier(YulString _name) const override;

	BuiltinFunctionForZVM const* discardFunction(YulString /*_type*/) const override { return builtin("pop"_yulstring); }
	BuiltinFunctionForZVM const* equalityFunction(YulString /*_type*/) const override { return builtin("eq"_yulstring); }
	BuiltinFunctionForZVM const* booleanNegationFunction() const override { return builtin("iszero"_yulstring); }
	BuiltinFunctionForZVM const* memoryStoreFunction(YulString /*_type*/) const override { return builtin("mstore"_yulstring); }
	BuiltinFunctionForZVM const* memoryLoadFunction(YulString /*_type*/) const override { return builtin("mload"_yulstring); }
	BuiltinFunctionForZVM const* storageStoreFunction(YulString /*_type*/) const override { return builtin("sstore"_yulstring); }
	BuiltinFunctionForZVM const* storageLoadFunction(YulString /*_type*/) const override { return builtin("sload"_yulstring); }
	YulString hashFunction(YulString /*_type*/) const override { return "keccak256"_yulstring; }

	static ZVMDialect const& strictAssemblyForZVM(langutil::ZVMVersion _version);
	static ZVMDialect const& strictAssemblyForZVMObjects(langutil::ZVMVersion _version);

	langutil::ZVMVersion zvmVersion() const { return m_zvmVersion; }

	bool providesObjectAccess() const { return m_objectAccess; }

	static SideEffects sideEffectsOfInstruction(zvmasm::Instruction _instruction);

protected:
	BuiltinFunctionForZVM const* verbatimFunction(size_t _arguments, size_t _returnVariables) const;

	bool const m_objectAccess;
	langutil::ZVMVersion const m_zvmVersion;
	std::map<YulString, BuiltinFunctionForZVM> m_functions;
	std::map<std::pair<size_t, size_t>, std::shared_ptr<BuiltinFunctionForZVM const>> mutable m_verbatimFunctions;
	std::set<YulString> m_reserved;
};

/**
 * ZVM dialect with types u256 (default) and bool.
 * Difference to ZVMDialect:
 *  - All comparison functions return type bool
 *  - bitwise operations are called bitor, bitand, bitxor and bitnot
 *  - and, or, xor take bool and return bool
 *  - iszero is replaced by not, which takes bool and returns bool
 *  - there are conversion functions bool_to_u256 and u256_to_bool.
 *  - there is popbool
 */
struct ZVMDialectTyped: public ZVMDialect
{
	/// Constructor, should only be used internally. Use the factory function below.
	ZVMDialectTyped(langutil::ZVMVersion _zvmVersion, bool _objectAccess);

	BuiltinFunctionForZVM const* discardFunction(YulString _type) const override;
	BuiltinFunctionForZVM const* equalityFunction(YulString _type) const override;
	BuiltinFunctionForZVM const* booleanNegationFunction() const override { return builtin("not"_yulstring); }

	static ZVMDialectTyped const& instance(langutil::ZVMVersion _version);
};

}
