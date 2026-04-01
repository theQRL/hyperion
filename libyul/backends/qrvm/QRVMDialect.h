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
 * Yul dialects for QRVM.
 */

#pragma once

#include <libyul/Dialect.h>

#include <libyul/backends/qrvm/AbstractAssembly.h>
#include <libyul/ASTForward.h>
#include <liblangutil/QRVMVersion.h>

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

struct BuiltinFunctionForQRVM: public BuiltinFunction
{
	std::optional<qrvmasm::Instruction> instruction;
	/// Function to generate code for the given function call and append it to the abstract
	/// assembly. Expects all non-literal arguments of the call to be on stack in reverse order
	/// (i.e. right-most argument pushed first).
	/// Expects the caller to set the source location.
	std::function<void(FunctionCall const&, AbstractAssembly&, BuiltinContext&)> generateCode;
};


/**
 * Yul dialect for QRVM as a backend.
 * The main difference is that the builtin functions take an AbstractAssembly for the
 * code generation.
 */
struct QRVMDialect: public Dialect
{
	/// Constructor, should only be used internally. Use the factory functions below.
	QRVMDialect(langutil::QRVMVersion _qrvmVersion, bool _objectAccess);

	/// @returns the builtin function of the given name or a nullptr if it is not a builtin function.
	BuiltinFunctionForQRVM const* builtin(YulString _name) const override;

	/// @returns true if the identifier is reserved. This includes the builtins too.
	bool reservedIdentifier(YulString _name) const override;

	BuiltinFunctionForQRVM const* discardFunction(YulString /*_type*/) const override { return builtin("pop"_yulstring); }
	BuiltinFunctionForQRVM const* equalityFunction(YulString /*_type*/) const override { return builtin("eq"_yulstring); }
	BuiltinFunctionForQRVM const* booleanNegationFunction() const override { return builtin("iszero"_yulstring); }
	BuiltinFunctionForQRVM const* memoryStoreFunction(YulString /*_type*/) const override { return builtin("mstore"_yulstring); }
	BuiltinFunctionForQRVM const* memoryLoadFunction(YulString /*_type*/) const override { return builtin("mload"_yulstring); }
	BuiltinFunctionForQRVM const* storageStoreFunction(YulString /*_type*/) const override { return builtin("sstore"_yulstring); }
	BuiltinFunctionForQRVM const* storageLoadFunction(YulString /*_type*/) const override { return builtin("sload"_yulstring); }
	YulString hashFunction(YulString /*_type*/) const override { return "keccak256"_yulstring; }

	static QRVMDialect const& strictAssemblyForQRVM(langutil::QRVMVersion _version);
	static QRVMDialect const& strictAssemblyForQRVMObjects(langutil::QRVMVersion _version);

	langutil::QRVMVersion qrvmVersion() const { return m_qrvmVersion; }

	bool providesObjectAccess() const { return m_objectAccess; }

	static SideEffects sideEffectsOfInstruction(qrvmasm::Instruction _instruction);

protected:
	BuiltinFunctionForQRVM const* verbatimFunction(size_t _arguments, size_t _returnVariables) const;

	bool const m_objectAccess;
	langutil::QRVMVersion const m_qrvmVersion;
	std::map<YulString, BuiltinFunctionForQRVM> m_functions;
	std::map<std::pair<size_t, size_t>, std::shared_ptr<BuiltinFunctionForQRVM const>> mutable m_verbatimFunctions;
	std::set<YulString> m_reserved;
};

/**
 * QRVM dialect with types u256 (default) and bool.
 * Difference to QRVMDialect:
 *  - All comparison functions return type bool
 *  - bitwise operations are called bitor, bitand, bitxor and bitnot
 *  - and, or, xor take bool and return bool
 *  - iszero is replaced by not, which takes bool and returns bool
 *  - there are conversion functions bool_to_u256 and u256_to_bool.
 *  - there is popbool
 */
struct QRVMDialectTyped: public QRVMDialect
{
	/// Constructor, should only be used internally. Use the factory function below.
	QRVMDialectTyped(langutil::QRVMVersion _qrvmVersion, bool _objectAccess);

	BuiltinFunctionForQRVM const* discardFunction(YulString _type) const override;
	BuiltinFunctionForQRVM const* equalityFunction(YulString _type) const override;
	BuiltinFunctionForQRVM const* booleanNegationFunction() const override { return builtin("not"_yulstring); }

	static QRVMDialectTyped const& instance(langutil::QRVMVersion _version);
};

}
