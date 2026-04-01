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

#include <libyul/backends/qrvm/QRVMDialect.h>

#include <libqrvmasm/Instruction.h>
#include <libqrvmasm/SemanticInformation.h>
#include <liblangutil/Exceptions.h>
#include <libhyputil/StringUtils.h>
#include <libyul/AST.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/AsmParser.h>
#include <libyul/Exceptions.h>
#include <libyul/Object.h>
#include <libyul/Utilities.h>
#include <libyul/backends/qrvm/AbstractAssembly.h>

#include <range/v3/view/reverse.hpp>
#include <range/v3/view/tail.hpp>

#include <regex>

using namespace std::string_literals;
using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;

namespace
{

std::pair<YulString, BuiltinFunctionForQRVM> createQRVMFunction(
	std::string const& _name,
	qrvmasm::Instruction _instruction
)
{
	qrvmasm::InstructionInfo info = qrvmasm::instructionInfo(_instruction);
	BuiltinFunctionForQRVM f;
	f.name = YulString{_name};
	f.parameters.resize(static_cast<size_t>(info.args));
	f.returns.resize(static_cast<size_t>(info.ret));
	f.sideEffects = QRVMDialect::sideEffectsOfInstruction(_instruction);
	if (qrvmasm::SemanticInformation::terminatesControlFlow(_instruction))
	{
		f.controlFlowSideEffects.canContinue = false;
		if (qrvmasm::SemanticInformation::reverts(_instruction))
		{
			f.controlFlowSideEffects.canTerminate = false;
			f.controlFlowSideEffects.canRevert = true;
		}
		else
		{
			f.controlFlowSideEffects.canTerminate = true;
			f.controlFlowSideEffects.canRevert = false;
		}
	}
	f.isMSize = _instruction == qrvmasm::Instruction::MSIZE;
	f.literalArguments.clear();
	f.instruction = _instruction;
	f.generateCode = [_instruction](
		FunctionCall const&,
		AbstractAssembly& _assembly,
		BuiltinContext&
	) {
		_assembly.appendInstruction(_instruction);
	};

	YulString name = f.name;
	return {name, std::move(f)};
}

std::pair<YulString, BuiltinFunctionForQRVM> createFunction(
	std::string _name,
	size_t _params,
	size_t _returns,
	SideEffects _sideEffects,
	std::vector<std::optional<LiteralKind>> _literalArguments,
	std::function<void(FunctionCall const&, AbstractAssembly&, BuiltinContext&)> _generateCode
)
{
	yulAssert(_literalArguments.size() == _params || _literalArguments.empty(), "");

	YulString name{std::move(_name)};
	BuiltinFunctionForQRVM f;
	f.name = name;
	f.parameters.resize(_params);
	f.returns.resize(_returns);
	f.sideEffects = std::move(_sideEffects);
	f.literalArguments = std::move(_literalArguments);
	f.isMSize = false;
	f.instruction = {};
	f.generateCode = std::move(_generateCode);
	return {name, f};
}

std::set<YulString> createReservedIdentifiers()
{
	std::set<YulString> reserved;
	for (auto const& instr: qrvmasm::c_instructions)
	{
		std::string name = toLower(instr.first);
		reserved.emplace(name);
	}
	reserved += std::vector<YulString>{
		"linkersymbol"_yulstring,
		"datasize"_yulstring,
		"dataoffset"_yulstring,
		"datacopy"_yulstring,
		"setimmutable"_yulstring,
		"loadimmutable"_yulstring,
	};
	return reserved;
}

std::map<YulString, BuiltinFunctionForQRVM> createBuiltins(bool _objectAccess)
{
	std::map<YulString, BuiltinFunctionForQRVM> builtins;
	for (auto const& instr: qrvmasm::c_instructions)
	{
		std::string name = toLower(instr.first);
		auto const opcode = instr.second;

		if (
			!qrvmasm::isDupInstruction(opcode) &&
			!qrvmasm::isSwapInstruction(opcode) &&
			!qrvmasm::isPushInstruction(opcode) &&
			opcode != qrvmasm::Instruction::JUMP &&
			opcode != qrvmasm::Instruction::JUMPI &&
			opcode != qrvmasm::Instruction::JUMPDEST
		)
			builtins.emplace(createQRVMFunction(name, opcode));
	}

	if (_objectAccess)
	{
		builtins.emplace(createFunction("linkersymbol", 1, 1, SideEffects{}, {LiteralKind::String}, [](
			FunctionCall const& _call,
			AbstractAssembly& _assembly,
			BuiltinContext&
		) {
			yulAssert(_call.arguments.size() == 1, "");
			Expression const& arg = _call.arguments.front();
			_assembly.appendLinkerSymbol(std::get<Literal>(arg).value.str());
		}));

		builtins.emplace(createFunction(
			"memoryguard",
			1,
			1,
			SideEffects{},
			{LiteralKind::Number},
			[](
				FunctionCall const& _call,
				AbstractAssembly& _assembly,
				BuiltinContext&
			) {
				yulAssert(_call.arguments.size() == 1, "");
				Literal const* literal = std::get_if<Literal>(&_call.arguments.front());
				yulAssert(literal, "");
				_assembly.appendConstant(valueOfLiteral(*literal));
			})
		);

		builtins.emplace(createFunction("datasize", 1, 1, SideEffects{}, {LiteralKind::String}, [](
			FunctionCall const& _call,
			AbstractAssembly& _assembly,
			BuiltinContext& _context
		) {
			yulAssert(_context.currentObject, "No object available.");
			yulAssert(_call.arguments.size() == 1, "");
			Expression const& arg = _call.arguments.front();
			YulString dataName = std::get<Literal>(arg).value;
			if (_context.currentObject->name == dataName)
				_assembly.appendAssemblySize();
			else
			{
			std::vector<size_t> subIdPath =
					_context.subIDs.count(dataName) == 0 ?
						_context.currentObject->pathToSubObject(dataName) :
						std::vector<size_t>{_context.subIDs.at(dataName)};
				yulAssert(!subIdPath.empty(), "Could not find assembly object <" + dataName.str() + ">.");
				_assembly.appendDataSize(subIdPath);
			}
		}));
		builtins.emplace(createFunction("dataoffset", 1, 1, SideEffects{}, {LiteralKind::String}, [](
			FunctionCall const& _call,
			AbstractAssembly& _assembly,
			BuiltinContext& _context
		) {
			yulAssert(_context.currentObject, "No object available.");
			yulAssert(_call.arguments.size() == 1, "");
			Expression const& arg = _call.arguments.front();
			YulString dataName = std::get<Literal>(arg).value;
			if (_context.currentObject->name == dataName)
				_assembly.appendConstant(0);
			else
			{
			std::vector<size_t> subIdPath =
					_context.subIDs.count(dataName) == 0 ?
						_context.currentObject->pathToSubObject(dataName) :
						std::vector<size_t>{_context.subIDs.at(dataName)};
				yulAssert(!subIdPath.empty(), "Could not find assembly object <" + dataName.str() + ">.");
				_assembly.appendDataOffset(subIdPath);
			}
		}));
		builtins.emplace(createFunction(
			"datacopy",
			3,
			0,
			SideEffects{false, true, false, false, true, SideEffects::None, SideEffects::None, SideEffects::Write},
			{},
			[](
				FunctionCall const&,
				AbstractAssembly& _assembly,
				BuiltinContext&
			) {
				_assembly.appendInstruction(qrvmasm::Instruction::CODECOPY);
			}
		));
		builtins.emplace(createFunction(
			"setimmutable",
			3,
			0,
			SideEffects{false, false, false, false, true, SideEffects::None, SideEffects::None, SideEffects::Write},
			{std::nullopt, LiteralKind::String, std::nullopt},
			[](
				FunctionCall const& _call,
				AbstractAssembly& _assembly,
				BuiltinContext&
			) {
				yulAssert(_call.arguments.size() == 3, "");
				YulString identifier = std::get<Literal>(_call.arguments[1]).value;
				_assembly.appendImmutableAssignment(identifier.str());
			}
		));
		builtins.emplace(createFunction(
			"loadimmutable",
			1,
			1,
			SideEffects{},
			{LiteralKind::String},
			[](
				FunctionCall const& _call,
				AbstractAssembly& _assembly,
				BuiltinContext&
			) {
				yulAssert(_call.arguments.size() == 1, "");
				_assembly.appendImmutable(std::get<Literal>(_call.arguments.front()).value.str());
			}
		));
	}
	return builtins;
}

std::regex const& verbatimPattern()
{
	std::regex static const pattern{"verbatim_([1-9]?[0-9])i_([1-9]?[0-9])o"};
	return pattern;
}

}


QRVMDialect::QRVMDialect(langutil::QRVMVersion _qrvmVersion, bool _objectAccess):
	m_objectAccess(_objectAccess),
	m_qrvmVersion(_qrvmVersion),
	m_functions(createBuiltins(_objectAccess)),
	m_reserved(createReservedIdentifiers())
{
}

BuiltinFunctionForQRVM const* QRVMDialect::builtin(YulString _name) const
{
	if (m_objectAccess)
	{
		std::smatch match;
		if (regex_match(_name.str(), match, verbatimPattern()))
			return verbatimFunction(stoul(match[1]), stoul(match[2]));
	}
	auto it = m_functions.find(_name);
	if (it != m_functions.end())
		return &it->second;
	else
		return nullptr;
}

bool QRVMDialect::reservedIdentifier(YulString _name) const
{
	if (m_objectAccess)
		if (_name.str().substr(0, "verbatim"s.size()) == "verbatim")
			return true;
	return m_reserved.count(_name) != 0;
}

QRVMDialect const& QRVMDialect::strictAssemblyForQRVM(langutil::QRVMVersion _version)
{
	static std::map<langutil::QRVMVersion, std::unique_ptr<QRVMDialect const>> dialects;
	static YulStringRepository::ResetCallback callback{[&] { dialects.clear(); }};
	if (!dialects[_version])
		dialects[_version] = std::make_unique<QRVMDialect>(_version, false);
	return *dialects[_version];
}

QRVMDialect const& QRVMDialect::strictAssemblyForQRVMObjects(langutil::QRVMVersion _version)
{
	static std::map<langutil::QRVMVersion, std::unique_ptr<QRVMDialect const>> dialects;
	static YulStringRepository::ResetCallback callback{[&] { dialects.clear(); }};
	if (!dialects[_version])
		dialects[_version] = std::make_unique<QRVMDialect>(_version, true);
	return *dialects[_version];
}

SideEffects QRVMDialect::sideEffectsOfInstruction(qrvmasm::Instruction _instruction)
{
	auto translate = [](qrvmasm::SemanticInformation::Effect _e) -> SideEffects::Effect
	{
		return static_cast<SideEffects::Effect>(_e);
	};

	return SideEffects{
		qrvmasm::SemanticInformation::movable(_instruction),
		qrvmasm::SemanticInformation::movableApartFromEffects(_instruction),
		qrvmasm::SemanticInformation::canBeRemoved(_instruction),
		qrvmasm::SemanticInformation::canBeRemovedIfNoMSize(_instruction),
		true, // cannotLoop
		translate(qrvmasm::SemanticInformation::otherState(_instruction)),
		translate(qrvmasm::SemanticInformation::storage(_instruction)),
		translate(qrvmasm::SemanticInformation::memory(_instruction)),
	};
}

BuiltinFunctionForQRVM const* QRVMDialect::verbatimFunction(size_t _arguments, size_t _returnVariables) const
{
	std::pair<size_t, size_t> key{_arguments, _returnVariables};
	std::shared_ptr<BuiltinFunctionForQRVM const>& function = m_verbatimFunctions[key];
	if (!function)
	{
		BuiltinFunctionForQRVM builtinFunction = createFunction(
			"verbatim_" + std::to_string(_arguments) + "i_" + std::to_string(_returnVariables) + "o",
			1 + _arguments,
			_returnVariables,
			SideEffects::worst(),
			std::vector<std::optional<LiteralKind>>{LiteralKind::String} + std::vector<std::optional<LiteralKind>>(_arguments),
			[=](
				FunctionCall const& _call,
				AbstractAssembly& _assembly,
				BuiltinContext&
			) {
				yulAssert(_call.arguments.size() == (1 + _arguments), "");
				Expression const& bytecode = _call.arguments.front();

				_assembly.appendVerbatim(
					asBytes(std::get<Literal>(bytecode).value.str()),
					_arguments,
					_returnVariables
				);
			}
		).second;
		builtinFunction.isMSize = true;
		function = std::make_shared<BuiltinFunctionForQRVM const>(std::move(builtinFunction));
	}
	return function.get();
}

QRVMDialectTyped::QRVMDialectTyped(langutil::QRVMVersion _qrvmVersion, bool _objectAccess):
	QRVMDialect(_qrvmVersion, _objectAccess)
{
	defaultType = "u256"_yulstring;
	boolType = "bool"_yulstring;
	types = {defaultType, boolType};

	// Set all types to ``defaultType``
	for (auto& fun: m_functions)
	{
		for (auto& p: fun.second.parameters)
			p = defaultType;
		for (auto& r: fun.second.returns)
			r = defaultType;
	}

	m_functions["lt"_yulstring].returns = {"bool"_yulstring};
	m_functions["gt"_yulstring].returns = {"bool"_yulstring};
	m_functions["slt"_yulstring].returns = {"bool"_yulstring};
	m_functions["sgt"_yulstring].returns = {"bool"_yulstring};
	m_functions["eq"_yulstring].returns = {"bool"_yulstring};

	// "not" and "bitnot" replace "iszero" and "not"
	m_functions["bitnot"_yulstring] = m_functions["not"_yulstring];
	m_functions["bitnot"_yulstring].name = "bitnot"_yulstring;
	m_functions["not"_yulstring] = m_functions["iszero"_yulstring];
	m_functions["not"_yulstring].name = "not"_yulstring;
	m_functions["not"_yulstring].returns = {"bool"_yulstring};
	m_functions["not"_yulstring].parameters = {"bool"_yulstring};
	m_functions.erase("iszero"_yulstring);

	m_functions["bitand"_yulstring] = m_functions["and"_yulstring];
	m_functions["bitand"_yulstring].name = "bitand"_yulstring;
	m_functions["bitor"_yulstring] = m_functions["or"_yulstring];
	m_functions["bitor"_yulstring].name = "bitor"_yulstring;
	m_functions["bitxor"_yulstring] = m_functions["xor"_yulstring];
	m_functions["bitxor"_yulstring].name = "bitxor"_yulstring;
	m_functions["and"_yulstring].parameters = {"bool"_yulstring, "bool"_yulstring};
	m_functions["and"_yulstring].returns = {"bool"_yulstring};
	m_functions["or"_yulstring].parameters = {"bool"_yulstring, "bool"_yulstring};
	m_functions["or"_yulstring].returns = {"bool"_yulstring};
	m_functions["xor"_yulstring].parameters = {"bool"_yulstring, "bool"_yulstring};
	m_functions["xor"_yulstring].returns = {"bool"_yulstring};
	m_functions["popbool"_yulstring] = m_functions["pop"_yulstring];
	m_functions["popbool"_yulstring].name = "popbool"_yulstring;
	m_functions["popbool"_yulstring].parameters = {"bool"_yulstring};
	m_functions.insert(createFunction("bool_to_u256", 1, 1, {}, {}, [](
		FunctionCall const&,
		AbstractAssembly&,
		BuiltinContext&
	) {}));
	m_functions["bool_to_u256"_yulstring].parameters = {"bool"_yulstring};
	m_functions["bool_to_u256"_yulstring].returns = {"u256"_yulstring};
	m_functions.insert(createFunction("u256_to_bool", 1, 1, {}, {}, [](
		FunctionCall const&,
		AbstractAssembly& _assembly,
		BuiltinContext&
	) {
		// TODO this should use a Panic.
		// A value larger than 1 causes an invalid instruction.
		_assembly.appendConstant(2);
		_assembly.appendInstruction(qrvmasm::Instruction::DUP2);
		_assembly.appendInstruction(qrvmasm::Instruction::LT);
		AbstractAssembly::LabelID inRange = _assembly.newLabelId();
		_assembly.appendJumpToIf(inRange);
		_assembly.appendInstruction(qrvmasm::Instruction::INVALID);
		_assembly.appendLabel(inRange);
	}));
	m_functions["u256_to_bool"_yulstring].parameters = {"u256"_yulstring};
	m_functions["u256_to_bool"_yulstring].returns = {"bool"_yulstring};
}

BuiltinFunctionForQRVM const* QRVMDialectTyped::discardFunction(YulString _type) const
{
	if (_type == "bool"_yulstring)
		return builtin("popbool"_yulstring);
	else
	{
		yulAssert(_type == defaultType, "");
		return builtin("pop"_yulstring);
	}
}

BuiltinFunctionForQRVM const* QRVMDialectTyped::equalityFunction(YulString _type) const
{
	if (_type == "bool"_yulstring)
		return nullptr;
	else
	{
		yulAssert(_type == defaultType, "");
		return builtin("eq"_yulstring);
	}
}

QRVMDialectTyped const& QRVMDialectTyped::instance(langutil::QRVMVersion _version)
{
	static std::map<langutil::QRVMVersion, std::unique_ptr<QRVMDialectTyped const>> dialects;
	static YulStringRepository::ResetCallback callback{[&] { dialects.clear(); }};
	if (!dialects[_version])
		dialects[_version] = std::make_unique<QRVMDialectTyped>(_version, true);
	return *dialects[_version];
}
