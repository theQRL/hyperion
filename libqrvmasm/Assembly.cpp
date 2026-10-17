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
/** @file Assembly.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libqrvmasm/Assembly.h>

#include <libqrvmasm/CommonSubexpressionEliminator.h>
#include <libqrvmasm/ControlFlowGraph.h>
#include <libqrvmasm/PeepholeOptimiser.h>
#include <libqrvmasm/Inliner.h>
#include <libqrvmasm/JumpdestRemover.h>
#include <libqrvmasm/BlockDeduplicator.h>
#include <libqrvmasm/ConstantOptimiser.h>
#include <libqrvmasm/GasMeter.h>

#include <liblangutil/CharStream.h>
#include <liblangutil/Exceptions.h>

#include <libhyputil/JSON.h>
#include <libhyputil/StringUtils.h>
#include <libhyputil/VMConstants.h>

#include <fmt/format.h>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/view/drop_exactly.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/map.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <iterator>
#include <cctype>
#include <optional>

using namespace hyperion;
using namespace hyperion::qrvmasm;
using namespace hyperion::langutil;
using namespace hyperion::util;

namespace
{

constexpr size_t c_maxAssemblyJSONNesting = 256;
constexpr size_t c_maxTrackedAddressByteStores = 4096;
constexpr size_t c_maxAssemblyDecimalIntegerDigits = 160;
constexpr size_t c_maxAssemblyHexIntegerDigits = VMWordBytes * 2;
constexpr size_t c_maxAssemblyDiagnosticValueBytes = 80;
constexpr size_t c_maxTrackedCurrentAddressOffsets = 16;
constexpr size_t c_maxEarlyValidationLocalJumpTargets = 0;
constexpr size_t c_localTagBits = std::numeric_limits<unsigned>::digits;
constexpr size_t c_lowStackBytePosition = VMWordBytes - 1;

using ByteRanges = CodeCopyTaintByteRanges;
using AddressByteMap = std::map<size_t, size_t>;
using AddressByteMaskKey = std::pair<size_t, size_t>;
using AddressByteMaskMap = std::map<AddressByteMaskKey, uint8_t>;

struct CodeCopyReadTaints
{
	bool mayReadForeignReference = false;
	bool mayReadLocalTag = false;
	bool mayReadCurrentAddress = false;
	bool mayReadComplementedCurrentAddress = false;
	std::set<std::pair<size_t, size_t>> foreignTags;
};

bool isHexDigits(std::string const& _value)
{
	if (_value.empty())
		return false;
	for (char c: _value)
		if (!std::isxdigit(static_cast<unsigned char>(c)))
			return false;
	return true;
}

bool isDecimalDigits(std::string const& _value)
{
	if (_value.empty())
		return false;
	for (char c: _value)
		if (!std::isdigit(static_cast<unsigned char>(c)))
			return false;
	return true;
}

std::string diagnosticValue(std::string const& _value)
{
	if (_value.size() <= c_maxAssemblyDiagnosticValueBytes)
		return _value;
	return
		_value.substr(0, c_maxAssemblyDiagnosticValueBytes) +
		"...(" + std::to_string(_value.size()) + " bytes)";
}

std::string assemblyDataKeyDescription(std::string const& _key)
{
	return "key '" + diagnosticValue(_key) + "' inside '.data'";
}

std::string significantDigits(std::string const& _value, char _zero, size_t _maxDigits, std::string const& _field)
{
	size_t const firstNonZero = _value.find_first_not_of(_zero);
	if (firstNonZero == std::string::npos)
		return std::string{_zero};

	size_t const digitCount = _value.size() - firstNonZero;
	solRequire(digitCount <= _maxDigits, AssemblyImportException, _field + " is out of bounds.");
	return _value.substr(firstNonZero);
}

bigint parseAssemblyInteger(std::string const& _value, std::string const& _field)
{
	solRequire(
		_value.size() <= c_maxAssemblyDecimalIntegerDigits,
		AssemblyImportException,
		_field + " is out of bounds."
	);
	if (!isDecimalDigits(_value))
		hypThrow(AssemblyImportException, _field + " is not a valid integer.");
	std::string const significantValue = significantDigits(
		_value,
		'0',
		c_maxAssemblyDecimalIntegerDigits,
		_field
	);
	try
	{
		return bigint(significantValue);
	}
	catch (std::exception const&)
	{
		hypThrow(AssemblyImportException, _field + " is not a valid integer.");
	}
	return bigint();
}

bigint parseAssemblyHexInteger(std::string const& _value, std::string const& _field, size_t _maxDigits)
{
	solRequire(
		_value.size() <= std::max(_maxDigits, c_maxAssemblyHexIntegerDigits),
		AssemblyImportException,
		_field + " is out of bounds."
	);
	if (!isHexDigits(_value))
		hypThrow(AssemblyImportException, _field + " is not a valid hexadecimal integer.");
	std::string const significantValue = significantDigits(_value, '0', _maxDigits, _field);
	try
	{
		return bigint("0x" + significantValue);
	}
	catch (std::exception const&)
	{
		hypThrow(AssemblyImportException, _field + " is not a valid hexadecimal integer.");
	}
	return bigint();
}

u512 parseAssemblyHexSizeT(std::string const& _value, std::string const& _field)
{
	bigint value = parseAssemblyHexInteger(_value, _field, sizeof(size_t) * 2);
	solRequire(
		value >= 0 && value <= std::numeric_limits<size_t>::max(),
		AssemblyImportException,
		_field + " is out of bounds."
	);
	return u512(value);
}

std::string formatAssemblyHexSizeT(size_t _value)
{
	std::stringstream hexStr;
	hexStr << std::hex << _value;
	return hexStr.str();
}

u512 parseAssemblyHexWord(std::string const& _value, std::string const& _field)
{
	bigint value = parseAssemblyHexInteger(_value, _field, VMWordBytes * 2);
	solRequire(
		value >= 0 && value < (bigint(1) << VMWordBits),
		AssemblyImportException,
		_field + " is out of bounds."
	);
	return u512(value);
}

u512 parseAssemblyHexDataReference(std::string const& _value, std::string const& _field)
{
	bigint value = parseAssemblyHexInteger(_value, _field, h256::size * 2);
	solRequire(
		value >= 0 && value < (bigint(1) << (h256::size * 8)),
		AssemblyImportException,
		_field + " is out of bounds."
	);
	return u512(value);
}

bytes parseAssemblyHexBytes(std::string const& _value, std::string const& _field, bool _allowEmpty)
{
	solRequire(
		(_allowEmpty || !_value.empty()) &&
		_value.size() % 2 == 0 &&
		(_value.empty() || isHexDigits(_value)),
		AssemblyImportException,
		_field + " is not a valid hexadecimal string."
	);
	return fromHex(_value, WhenError::Throw);
}

h256 parseAssemblyDataHash(std::string const& _value, std::string const& _field)
{
	solRequire(
		_value.size() == h256::size * 2 && isHexDigits(_value),
		AssemblyImportException,
		"The " + _field + " is not a 32-byte hexadecimal string."
	);
	return h256(fromHex(_value, WhenError::Throw));
}

size_t checkedAddSize(size_t _left, size_t _right)
{
	assertThrow(
		_right <= std::numeric_limits<size_t>::max() - _left,
		AssemblyException,
		"Assembly size overflow."
	);
	return _left + _right;
}

bool byteRangesOverlap(size_t _leftOffset, size_t _leftSize, size_t _rightOffset, size_t _rightSize)
{
	if (_leftSize == 0 || _rightSize == 0)
		return false;
	size_t const maxEnd = std::numeric_limits<size_t>::max();
	bool const leftEndOverflows = _leftSize > maxEnd - _leftOffset;
	bool const rightEndOverflows = _rightSize > maxEnd - _rightOffset;
	size_t const leftEnd = leftEndOverflows ? maxEnd : _leftOffset + _leftSize;
	size_t const rightEnd = rightEndOverflows ? maxEnd : _rightOffset + _rightSize;
	return
		(leftEndOverflows || _rightOffset < leftEnd) &&
		(rightEndOverflows || _leftOffset < rightEnd);
}

void writeBigEndianChecked(size_t _value, bytesRef _out, unsigned _width, std::string const& _message)
{
	assertThrow(numberEncodingSize(_value) <= _width, AssemblyException, _message);
	toBigEndian(_value, _out);
}

u256 checkedHashReference(AssemblyItem const& _item, char const* _kind)
{
	assertThrow(
		_item.data() < (u512(1) << (h256::size * 8)),
		AssemblyException,
		fmt::format("{} reference out of bounds.", _kind)
	);
	return u256(_item.data());
}

std::set<h256> referencedDataHashes(AssemblyItems const& _items)
{
	std::set<h256> hashes;
	for (AssemblyItem const& item: _items)
		if (item.type() == PushData)
			hashes.insert(h256(checkedHashReference(item, "Data")));
	return hashes;
}

size_t referencedDataSize(std::map<h256, bytes> const& _data, std::set<h256> const& _referencedHashes)
{
	size_t size = 0;
	for (auto const& [dataHash, dataBytes]: _data)
		if (_referencedHashes.count(dataHash))
			size = checkedAddSize(size, dataBytes.size());
	return size;
}

bool equalFunctionDebugData(
	LinkerObject::FunctionDebugData const& _left,
	LinkerObject::FunctionDebugData const& _right
)
{
	return
		std::tie(_left.bytecodeOffset, _left.instructionIndex, _left.sourceID, _left.params, _left.returns) ==
		std::tie(_right.bytecodeOffset, _right.instructionIndex, _right.sourceID, _right.params, _right.returns);
}

void insertFunctionDebugData(
	std::map<std::string, LinkerObject::FunctionDebugData>& _debugData,
	std::string const& _name,
	LinkerObject::FunctionDebugData const& _data
)
{
	auto [debugIt, inserted] = _debugData.emplace(_name, _data);
	if (inserted || equalFunctionDebugData(debugIt->second, _data))
		return;

	for (size_t suffix = 1; suffix != 0; ++suffix)
	{
		auto [suffixedIt, suffixedInserted] = _debugData.emplace(_name + "#" + std::to_string(suffix), _data);
		if (suffixedInserted || equalFunctionDebugData(suffixedIt->second, _data))
			return;
	}
	assertThrow(false, AssemblyException, "Too many duplicate function debug data entries.");
}

[[maybe_unused]] bool equalOptimiserSettings(Assembly::OptimiserSettings const& _left, Assembly::OptimiserSettings const& _right)
{
	return
		_left.runInliner == _right.runInliner &&
		_left.runJumpdestRemover == _right.runJumpdestRemover &&
		_left.runPeephole == _right.runPeephole &&
		_left.runDeduplicate == _right.runDeduplicate &&
		_left.runCSE == _right.runCSE &&
		_left.runConstantOptimiser == _right.runConstantOptimiser &&
		_left.qrvmVersion == _right.qrvmVersion &&
		_left.expectedExecutionsPerDeployment == _right.expectedExecutionsPerDeployment;
}

int stackDepositDelta(AssemblyItem const& _item)
{
	size_t arguments = _item.arguments();
	size_t returnValues = _item.returnValues();
	assertThrow(
		arguments <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
		returnValues <= static_cast<size_t>(std::numeric_limits<int>::max()),
		AssemblyException,
		"Stack effect too large."
	);
	return static_cast<int>(returnValues) - static_cast<int>(arguments);
}

int recomputeStackDeposit(AssemblyItems const& _items)
{
	long long deposit = 0;
	for (AssemblyItem const& item: _items)
	{
		deposit += stackDepositDelta(item);
		assertThrow(
			deposit >= std::numeric_limits<int>::min() &&
			deposit <= std::numeric_limits<int>::max(),
			AssemblyException,
			"Stack height overflow."
		);
	}
	return static_cast<int>(deposit);
}

void assertSerializableOperation(AssemblyItem const& _item)
{
	(void)_item;
}

void assertRepresentableSubAssemblyReference(AssemblyItem const& _item)
{
	assertThrow(
		_item.data() <= std::numeric_limits<size_t>::max(),
		AssemblyException,
		"Subassembly id out of bounds."
	);
}

void assertRepresentableTagReference(AssemblyItem const& _item)
{
	u512 const subAssembly = _item.data() >> 64;
	u512 const tagValue = _item.data() & u512(0xffffffffffffffffULL);
	assertThrow(
		subAssembly <= std::numeric_limits<size_t>::max(),
		AssemblyException,
		"Subassembly id out of bounds."
	);
	assertThrow(
		tagValue < std::numeric_limits<unsigned>::max(),
		AssemblyException,
		"Tag id out of bounds."
	);
}

void assertRepresentableTagDeclaration(AssemblyItem const& _item)
{
	assertThrow(_item.data() != 0, AssemblyException, "Invalid tag position.");
	assertThrow(
		_item.data() < std::numeric_limits<unsigned>::max(),
		AssemblyException,
		"Tag id out of bounds."
	);
}

bool validFunctionStackSlots(size_t _params, size_t _returns)
{
	return _params < Assembly::StackLimit && _returns < Assembly::StackLimit;
}

bool terminatesLinearControlFlow(Instruction _instruction)
{
	switch (_instruction)
	{
	case Instruction::JUMP:
	case Instruction::RETURN:
	case Instruction::STOP:
	case Instruction::INVALID:
	case Instruction::REVERT:
		return true;
	default:
		return false;
	}
}

struct VerbatimTaintInfo
{
	bool readsMemory = false;
	bool writesMemory = false;
	bool readsStorage = false;
	bool writesStorage = false;
};

VerbatimTaintInfo verbatimTaintInfo(bytes const& _data)
{
	VerbatimTaintInfo result;
	for (size_t offset = 0; offset < _data.size();)
	{
		Instruction instruction = static_cast<Instruction>(_data[offset++]);
		if (!isValidInstruction(instruction))
			return {true, true, true, true};

		switch (instruction)
		{
		case Instruction::MLOAD:
			result.readsMemory = true;
			break;
		case Instruction::MSTORE:
		case Instruction::MSTORE8:
			result.writesMemory = true;
			break;
		case Instruction::SLOAD:
			result.readsStorage = true;
			break;
		case Instruction::SSTORE:
			result.writesStorage = true;
			break;
		default:
			break;
		}

		InstructionInfo const& info = instructionInfo(instruction);
		if (info.additional < 0 || static_cast<size_t>(info.additional) > _data.size() - offset)
			return {true, true, true, true};
		offset += static_cast<size_t>(info.additional);
	}
	return result;
}

struct VerbatimStackEffect
{
	size_t arguments = 0;
	size_t returnValues = 0;
	size_t peakHeight = 0;
};

VerbatimStackEffect verbatimStackEffect(bytes const& _data, std::string const& _field)
{
	VerbatimStackEffect result;
	size_t height = 0;
	for (size_t offset = 0; offset < _data.size();)
	{
		Instruction instruction = static_cast<Instruction>(_data[offset++]);
		solRequire(isValidInstruction(instruction), AssemblyImportException, _field + " contains an invalid opcode.");
		solRequire(
			instruction != Instruction::JUMP &&
			instruction != Instruction::JUMPI &&
			instruction != Instruction::JUMPDEST &&
			!terminatesLinearControlFlow(instruction),
			AssemblyImportException,
			_field + " contains a control-flow opcode."
		);

		InstructionInfo const& info = instructionInfo(instruction);
		solRequire(
			info.additional >= 0 && info.args >= 0 && info.ret >= 0,
			AssemblyImportException,
			_field + " contains invalid instruction metadata."
		);
		size_t const additional = static_cast<size_t>(info.additional);
		size_t const instructionArguments = static_cast<size_t>(info.args);
		size_t const instructionReturnValues = static_cast<size_t>(info.ret);
		solRequire(
			additional <= _data.size() - offset,
			AssemblyImportException,
			_field + " contains truncated instruction data."
		);
		offset += additional;

		size_t const missingArguments = instructionArguments > height ? instructionArguments - height : 0;
		solRequire(
			missingArguments <= Assembly::StackLimit - result.arguments,
			AssemblyImportException,
			_field + " stack effect is too large."
		);
		result.arguments += missingArguments;
		height += missingArguments;
		result.peakHeight = std::max(result.peakHeight, height);
		height -= instructionArguments;
		solRequire(
			instructionReturnValues <= Assembly::StackLimit - height,
			AssemblyImportException,
			_field + " stack effect is too large."
		);
		height += instructionReturnValues;
		result.peakHeight = std::max(result.peakHeight, height);
	}
	result.returnValues = height;
	return result;
}

void assertVerbatimBytecodeIsStructured(AssemblyItem const& _item)
{
	try
	{
		(void)verbatimStackEffect(_item.verbatimData(), "VERBATIM bytecode");
	}
	catch (AssemblyImportException const&)
	{
		assertThrow(false, AssemblyException, "Invalid VERBATIM bytecode.");
	}
}

void assertNoHiddenData(AssemblyItem const& _item, char const* _name)
{
	assertThrow(
		_item.data() == 0,
		AssemblyException,
		std::string(_name) + " assembly item cannot carry hidden data."
	);
}

void assertValidJumpTypeMetadata(AssemblyItem const& _item)
{
	if (_item.getJumpType() == AssemblyItem::JumpType::Ordinary)
		return;

	assertThrow(
		_item.type() == Operation &&
		(_item.instruction() == Instruction::JUMP || _item.instruction() == Instruction::JUMPI),
		AssemblyException,
		"Jump type metadata is only valid on JUMP or JUMPI instructions."
	);
}

void assertSerializableAssemblyItem(AssemblyItem const& _item)
{
	assertValidJumpTypeMetadata(_item);
	switch (_item.type())
	{
	case Operation:
		assertSerializableOperation(_item);
		break;
	case Push:
		break;
	case PushProgramSize:
		assertNoHiddenData(_item, "PUSHSIZE");
		break;
	case PushDeployTimeAddress:
		assertNoHiddenData(_item, "PUSHDEPLOYADDRESS");
		break;
	case PushSub:
	case PushSubSize:
		assertRepresentableSubAssemblyReference(_item);
		break;
	case PushTag:
		assertRepresentableTagReference(_item);
		break;
	case Tag:
		assertRepresentableTagDeclaration(_item);
		break;
	case PushData:
		(void)checkedHashReference(_item, "Data");
		break;
	case PushLibraryAddress:
		(void)checkedHashReference(_item, "Library");
		break;
	case PushImmutable:
	case AssignImmutable:
		(void)checkedHashReference(_item, "Immutable");
		break;
	case VerbatimBytecode:
		assertVerbatimBytecodeIsStructured(_item);
		break;
	default:
		assertThrow(false, AssemblyException, "Invalid assembly item.");
	}
}

void assertRepresentableAssemblyItemReferences(AssemblyItem const& _item)
{
	switch (_item.type())
	{
	case PushSub:
	case PushSubSize:
		assertRepresentableSubAssemblyReference(_item);
		break;
	case PushTag:
		assertRepresentableTagReference(_item);
		break;
	case Tag:
		assertRepresentableTagDeclaration(_item);
		break;
	case PushData:
		(void)checkedHashReference(_item, "Data");
		break;
	case PushLibraryAddress:
		(void)checkedHashReference(_item, "Library");
		break;
	case PushImmutable:
	case AssignImmutable:
		(void)checkedHashReference(_item, "Immutable");
		break;
	default:
		break;
	}
}

std::string const& requiredIdentifier(
	std::map<h256, std::string> const& _identifiers,
	h256 const& _hash,
	char const* _kind
)
{
	(void)_kind;
	return _identifiers.at(_hash);
}

struct AbstractStackValue
{
	bool mayBeForeignTag = false;
	bool mayBeExternalJumpTarget = false;
	bool mustBeExternalJumpTarget = false;
	bool mayBeCurrentAddress = false;
	bool mayBeCurrentAddressDerived = false;
	bool mayBeComplementedCurrentAddress = false;
	bool mayBeComplementedCurrentAddressDerived = false;
	bool mayBeUnknownLocalTag = false;
	bool mayBeUnknownLeftShiftedLocalTag = false;
	bool mayBeUnknownCallerStackValue = false;
	std::optional<u512> currentAddressOffset;
	std::optional<u512> currentAddressXorMask;
	std::optional<u512> complementedCurrentAddressOffset;
	std::set<u512> possibleCurrentAddressOffsets;
	std::optional<size_t> currentAddressByteIndex;
	std::optional<size_t> complementedCurrentAddressByteIndex;
	AddressByteMap currentAddressStackBytes;
	AddressByteMap complementedCurrentAddressStackBytes;
	AddressByteMaskMap currentAddressMaskedStackBytes;
	AddressByteMaskMap complementedCurrentAddressMaskedStackBytes;
	std::set<size_t> zeroBytePositions;
	std::set<size_t> ffBytePositions;
	std::map<size_t, uint8_t> possibleOneBitMasks;
	std::optional<size_t> localTagLeftShift;
	std::set<std::pair<size_t, size_t>> foreignTags;
	std::set<size_t> localTags;
	std::set<size_t> leftShiftedLocalTags;
	std::optional<u512> literalValue;
	std::optional<size_t> duplicateSourceBelowTop;
	std::optional<size_t> isZeroOfValueBelow;

	bool operator==(AbstractStackValue const& _other) const
	{
		return
			mayBeForeignTag == _other.mayBeForeignTag &&
				mayBeExternalJumpTarget == _other.mayBeExternalJumpTarget &&
				mustBeExternalJumpTarget == _other.mustBeExternalJumpTarget &&
				mayBeCurrentAddress == _other.mayBeCurrentAddress &&
				mayBeCurrentAddressDerived == _other.mayBeCurrentAddressDerived &&
				mayBeComplementedCurrentAddress == _other.mayBeComplementedCurrentAddress &&
				mayBeComplementedCurrentAddressDerived == _other.mayBeComplementedCurrentAddressDerived &&
					mayBeUnknownLocalTag == _other.mayBeUnknownLocalTag &&
					mayBeUnknownLeftShiftedLocalTag == _other.mayBeUnknownLeftShiftedLocalTag &&
					mayBeUnknownCallerStackValue == _other.mayBeUnknownCallerStackValue &&
					currentAddressOffset == _other.currentAddressOffset &&
				currentAddressXorMask == _other.currentAddressXorMask &&
				complementedCurrentAddressOffset == _other.complementedCurrentAddressOffset &&
				possibleCurrentAddressOffsets == _other.possibleCurrentAddressOffsets &&
				currentAddressByteIndex == _other.currentAddressByteIndex &&
				complementedCurrentAddressByteIndex == _other.complementedCurrentAddressByteIndex &&
				currentAddressStackBytes == _other.currentAddressStackBytes &&
				complementedCurrentAddressStackBytes == _other.complementedCurrentAddressStackBytes &&
				currentAddressMaskedStackBytes == _other.currentAddressMaskedStackBytes &&
				complementedCurrentAddressMaskedStackBytes == _other.complementedCurrentAddressMaskedStackBytes &&
				zeroBytePositions == _other.zeroBytePositions &&
				ffBytePositions == _other.ffBytePositions &&
				possibleOneBitMasks == _other.possibleOneBitMasks &&
				localTagLeftShift == _other.localTagLeftShift &&
				foreignTags == _other.foreignTags &&
			localTags == _other.localTags &&
			leftShiftedLocalTags == _other.leftShiftedLocalTags &&
			literalValue == _other.literalValue &&
			duplicateSourceBelowTop == _other.duplicateSourceBelowTop &&
			isZeroOfValueBelow == _other.isZeroOfValueBelow;
	}

	bool merge(AbstractStackValue const& _other)
	{
		bool changed = false;
		auto markCurrentAddressDerived = [&]() {
			if (!mayBeCurrentAddressDerived)
			{
				mayBeCurrentAddressDerived = true;
				changed = true;
			}
		};
		auto markComplementedCurrentAddressDerived = [&]() {
			if (!mayBeComplementedCurrentAddressDerived)
			{
				mayBeComplementedCurrentAddressDerived = true;
				changed = true;
			}
		};
		if (_other.mayBeForeignTag && !mayBeForeignTag)
		{
			mayBeForeignTag = true;
			changed = true;
		}
		if (_other.mayBeExternalJumpTarget && !mayBeExternalJumpTarget)
		{
			mayBeExternalJumpTarget = true;
			changed = true;
		}
		if (!_other.mustBeExternalJumpTarget && mustBeExternalJumpTarget)
		{
			mustBeExternalJumpTarget = false;
			changed = true;
		}
		if (_other.mayBeCurrentAddress && !mayBeCurrentAddress)
		{
			mayBeCurrentAddress = true;
			changed = true;
		}
		if (_other.mayBeCurrentAddressDerived && !mayBeCurrentAddressDerived)
		{
			mayBeCurrentAddressDerived = true;
			changed = true;
		}
		if (_other.mayBeComplementedCurrentAddress && !mayBeComplementedCurrentAddress)
		{
			mayBeComplementedCurrentAddress = true;
			changed = true;
		}
		if (_other.mayBeComplementedCurrentAddressDerived && !mayBeComplementedCurrentAddressDerived)
		{
			mayBeComplementedCurrentAddressDerived = true;
			changed = true;
		}
		if (_other.mayBeUnknownLocalTag && !mayBeUnknownLocalTag)
		{
			mayBeUnknownLocalTag = true;
			changed = true;
		}
			if (_other.mayBeUnknownLeftShiftedLocalTag && !mayBeUnknownLeftShiftedLocalTag)
			{
				mayBeUnknownLeftShiftedLocalTag = true;
				changed = true;
			}
			if (_other.mayBeUnknownCallerStackValue && !mayBeUnknownCallerStackValue)
			{
				mayBeUnknownCallerStackValue = true;
				changed = true;
			}
		if (
			currentAddressOffset != _other.currentAddressOffset ||
			possibleCurrentAddressOffsets != _other.possibleCurrentAddressOffsets
		)
		{
			std::set<u512> mergedPossibleOffsets = possibleCurrentAddressOffsets;
			if (currentAddressOffset)
				mergedPossibleOffsets.insert(*currentAddressOffset);
			if (_other.currentAddressOffset)
				mergedPossibleOffsets.insert(*_other.currentAddressOffset);
			mergedPossibleOffsets.insert(
				_other.possibleCurrentAddressOffsets.begin(),
				_other.possibleCurrentAddressOffsets.end()
			);
			if (
				currentAddressOffset ||
				_other.currentAddressOffset ||
				!possibleCurrentAddressOffsets.empty() ||
				!_other.possibleCurrentAddressOffsets.empty()
			)
				markCurrentAddressDerived();
			currentAddressOffset.reset();
			if (mergedPossibleOffsets.size() <= c_maxTrackedCurrentAddressOffsets)
				possibleCurrentAddressOffsets = std::move(mergedPossibleOffsets);
			else
			{
				if (mergedPossibleOffsets.count(u512(0)))
					mayBeCurrentAddress = true;
				possibleCurrentAddressOffsets.clear();
			}
			changed = true;
		}
		if (currentAddressXorMask != _other.currentAddressXorMask)
		{
			if (currentAddressXorMask || _other.currentAddressXorMask)
			{
				markCurrentAddressDerived();
				markComplementedCurrentAddressDerived();
			}
			currentAddressXorMask.reset();
			changed = true;
		}
		if (complementedCurrentAddressOffset != _other.complementedCurrentAddressOffset)
		{
			if (complementedCurrentAddressOffset || _other.complementedCurrentAddressOffset)
				markComplementedCurrentAddressDerived();
			complementedCurrentAddressOffset.reset();
			changed = true;
		}
		if (currentAddressByteIndex != _other.currentAddressByteIndex)
		{
			currentAddressByteIndex.reset();
			changed = true;
		}
		if (complementedCurrentAddressByteIndex != _other.complementedCurrentAddressByteIndex)
		{
			complementedCurrentAddressByteIndex.reset();
			changed = true;
		}
		auto maskHasConflictingSource = [](AddressByteMaskMap const& _maskedBytes, size_t _position, size_t _byteIndex)
		{
			for (auto const& [key, mask]: _maskedBytes)
				if (key.first == _position && key.second != _byteIndex && mask != 0)
					return true;
			return false;
		};
		auto mergeAddressByteMask = [&](
			AddressByteMaskMap& _maskedBytes,
			AddressByteMap& _exactBytes,
			size_t _position,
			size_t _byteIndex,
			uint8_t _mask
		)
		{
			if (_mask == 0)
				return;
			uint8_t& targetMask = _maskedBytes[{_position, _byteIndex}];
			uint8_t const mergedMask = static_cast<uint8_t>(targetMask | _mask);
			if (targetMask != mergedMask)
			{
				targetMask = mergedMask;
				changed = true;
			}

			auto exactByte = _exactBytes.find(_position);
			bool const conflict =
				(exactByte != _exactBytes.end() && exactByte->second != _byteIndex) ||
				maskHasConflictingSource(_maskedBytes, _position, _byteIndex);
			if (conflict)
			{
				if (exactByte != _exactBytes.end())
				{
					uint8_t& exactMask = _maskedBytes[{_position, exactByte->second}];
					if (exactMask != 0xff)
					{
						exactMask = 0xff;
						changed = true;
					}
					_exactBytes.erase(exactByte);
					changed = true;
				}
			}
			else if (targetMask == 0xff && (exactByte == _exactBytes.end() || exactByte->second != _byteIndex))
			{
				_exactBytes[_position] = _byteIndex;
				changed = true;
			}
		};
		for (auto const& byte: _other.currentAddressStackBytes)
			mergeAddressByteMask(
				currentAddressMaskedStackBytes,
				currentAddressStackBytes,
				byte.first,
				byte.second,
				0xff
			);
		for (auto const& byte: _other.complementedCurrentAddressStackBytes)
			mergeAddressByteMask(
				complementedCurrentAddressMaskedStackBytes,
				complementedCurrentAddressStackBytes,
				byte.first,
				byte.second,
				0xff
			);
		auto mergeByteMasks = [&](
			AddressByteMaskMap& _target,
			AddressByteMap& _exactBytes,
			AddressByteMaskMap const& _source
		)
		{
			for (auto const& [key, mask]: _source)
				mergeAddressByteMask(_target, _exactBytes, key.first, key.second, mask);
		};
		mergeByteMasks(
			currentAddressMaskedStackBytes,
			currentAddressStackBytes,
			_other.currentAddressMaskedStackBytes
		);
		mergeByteMasks(
			complementedCurrentAddressMaskedStackBytes,
			complementedCurrentAddressStackBytes,
			_other.complementedCurrentAddressMaskedStackBytes
		);
		for (auto it = zeroBytePositions.begin(); it != zeroBytePositions.end();)
			if (!_other.zeroBytePositions.count(*it))
			{
				it = zeroBytePositions.erase(it);
				changed = true;
			}
			else
				++it;
		for (auto it = ffBytePositions.begin(); it != ffBytePositions.end();)
			if (!_other.ffBytePositions.count(*it))
			{
				it = ffBytePositions.erase(it);
				changed = true;
			}
			else
				++it;
		for (auto it = possibleOneBitMasks.begin(); it != possibleOneBitMasks.end();)
		{
			auto otherMask = _other.possibleOneBitMasks.find(it->first);
			if (otherMask == _other.possibleOneBitMasks.end())
			{
				it = possibleOneBitMasks.erase(it);
				changed = true;
				continue;
			}
			uint8_t const mergedMask = static_cast<uint8_t>(it->second | otherMask->second);
			if (mergedMask == 0xff)
			{
				it = possibleOneBitMasks.erase(it);
				changed = true;
			}
			else
			{
				if (it->second != mergedMask)
				{
					it->second = mergedMask;
					changed = true;
				}
				++it;
			}
		}
		if (localTagLeftShift != _other.localTagLeftShift)
		{
			if (localTagLeftShift || _other.localTagLeftShift)
			{
				mayBeUnknownLocalTag = true;
				leftShiftedLocalTags.clear();
			}
			localTagLeftShift.reset();
			changed = true;
		}
		for (auto tag: _other.foreignTags)
			changed = foreignTags.insert(tag).second || changed;
		for (size_t tag: _other.localTags)
			changed = localTags.insert(tag).second || changed;
		if (localTagLeftShift)
			for (size_t tag: _other.leftShiftedLocalTags)
				changed = leftShiftedLocalTags.insert(tag).second || changed;
		if (literalValue != _other.literalValue)
		{
			literalValue.reset();
			changed = true;
		}
		if (duplicateSourceBelowTop != _other.duplicateSourceBelowTop)
		{
			duplicateSourceBelowTop.reset();
			changed = true;
		}
		if (isZeroOfValueBelow != _other.isZeroOfValueBelow)
		{
			isZeroOfValueBelow.reset();
			changed = true;
		}
		return changed;
	}

		static AbstractStackValue unknown() { return {}; }
		static AbstractStackValue unknownCallerStackValue()
		{
			AbstractStackValue value;
			value.mayBeUnknownCallerStackValue = true;
			return value;
		}
		static AbstractStackValue externalJumpTarget()
	{
		AbstractStackValue value;
		value.mayBeExternalJumpTarget = true;
		value.mustBeExternalJumpTarget = true;
		return value;
	}
	static AbstractStackValue foreignTag()
	{
		AbstractStackValue value;
		value.mayBeForeignTag = true;
		return value;
	}
		static AbstractStackValue foreignTag(size_t _subId, size_t _tagId)
		{
			AbstractStackValue value = foreignTag();
			value.foreignTags.emplace(_subId, _tagId);
			return value;
		}
		static AbstractStackValue unknownLocalTag()
		{
			AbstractStackValue value;
			value.mayBeUnknownLocalTag = true;
			return value;
		}
		static AbstractStackValue fullyTaintedReference()
		{
			AbstractStackValue value;
			value.mayBeForeignTag = true;
		value.markMayBeCurrentAddress();
		value.markMayBeComplementedCurrentAddress();
		return value;
	}
	static AbstractStackValue currentAddress()
	{
		AbstractStackValue value;
		value.markMayBeCurrentAddress();
		return value;
	}
	static AbstractStackValue localTag(size_t _tagId)
	{
		AbstractStackValue value;
		value.localTags = {_tagId};
		return value;
	}
	static AbstractStackValue literal(u512 _value)
	{
		AbstractStackValue value;
		value.literalValue = std::move(_value);
		return value;
	}

	void markMayBeCurrentAddress()
	{
		mayBeCurrentAddress = true;
		currentAddressOffset = u512(0);
		currentAddressXorMask = u512(0);
	}

	void markMayBeComplementedCurrentAddress()
	{
		mayBeComplementedCurrentAddress = true;
		currentAddressXorMask = ~u512(0);
		complementedCurrentAddressOffset = u512(0);
	}
};

struct FunctionReturnTarget
{
	bool mustBeExternalJumpTarget = false;
	std::set<size_t> localTags;
	std::optional<u512> literalValue;

	bool operator==(FunctionReturnTarget const& _other) const
	{
		return
			mustBeExternalJumpTarget == _other.mustBeExternalJumpTarget &&
			localTags == _other.localTags &&
			literalValue == _other.literalValue;
	}
};

FunctionReturnTarget functionReturnTarget(AbstractStackValue const& _value)
{
	return {
		_value.mustBeExternalJumpTarget,
		_value.localTags,
		_value.literalValue
	};
}

bool hasKnownReturnTarget(FunctionReturnTarget const& _target)
{
	return
		_target.mustBeExternalJumpTarget ||
		!_target.localTags.empty() ||
		_target.literalValue.has_value();
}

bool hasKnownReturnTarget(AbstractStackValue const& _target)
{
	return
		_target.mustBeExternalJumpTarget ||
		!_target.localTags.empty() ||
		_target.literalValue.has_value();
}

bool outOfFunctionTargetMatchesReturnTarget(
	AbstractStackValue const& _actual,
	FunctionReturnTarget const& _expected
)
{
	if (
		_actual.mayBeForeignTag ||
		(_actual.mayBeExternalJumpTarget && !_actual.mustBeExternalJumpTarget) ||
		_actual.mayBeUnknownLocalTag ||
		!hasKnownReturnTarget(_actual) ||
		!hasKnownReturnTarget(_expected)
	)
		return false;
	if (_actual.mustBeExternalJumpTarget && !_expected.mustBeExternalJumpTarget)
		return false;
	if (
		!_actual.localTags.empty() &&
		!std::includes(
			_expected.localTags.begin(),
			_expected.localTags.end(),
			_actual.localTags.begin(),
			_actual.localTags.end()
		)
	)
		return false;
	if (_actual.literalValue && _actual.literalValue != _expected.literalValue)
		return false;
	return true;
}

bool codeCopyMayReadRange(
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	size_t _taintOffset,
	size_t _taintSize
)
{
	if (_length.literalValue && *_length.literalValue == 0)
		return false;
	if (!_codeOffset.literalValue || !_length.literalValue)
		return true;
	if (
		*_codeOffset.literalValue > std::numeric_limits<size_t>::max() ||
		*_length.literalValue > std::numeric_limits<size_t>::max()
	)
		return true;

	size_t const codeOffset = static_cast<size_t>(*_codeOffset.literalValue);
	size_t const length = static_cast<size_t>(*_length.literalValue);
	return byteRangesOverlap(codeOffset, length, _taintOffset, _taintSize);
}

bool codeCopyMayReadRanges(
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	ByteRanges const& _ranges
)
{
	if (_ranges.empty())
		return false;
	for (auto const& [taintOffset, taintSize]: _ranges)
		if (codeCopyMayReadRange(_codeOffset, _length, taintOffset, taintSize))
			return true;
	return false;
}

std::set<std::pair<size_t, size_t>> codeCopyReadForeignTags(
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	CodeCopyTaintRanges const* _taintRanges
)
{
	std::set<std::pair<size_t, size_t>> foreignTags;
	if (!_taintRanges)
		return foreignTags;
	for (auto const& [taintOffset, taintSize, subId, tagId]: _taintRanges->foreignTags)
		if (codeCopyMayReadRange(_codeOffset, _length, taintOffset, taintSize))
			foreignTags.emplace(subId, tagId);
	return foreignTags;
}

bool codeCopyMayReadLocalTagReferences(
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	CodeCopyTaintRanges const* _taintRanges
)
{
	if (!_taintRanges)
		return false;
	for (auto const& [taintOffset, taintSize, tagId]: _taintRanges->localTagReferences)
	{
		(void)tagId;
		if (codeCopyMayReadRange(_codeOffset, _length, taintOffset, taintSize))
			return true;
	}
	return false;
}

CodeCopyReadTaints codeCopyMayReadTaints(
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	CodeCopyTaintRanges const* _taintRanges
)
{
	if (!_taintRanges)
		return {};
	std::set<std::pair<size_t, size_t>> foreignTags =
		codeCopyReadForeignTags(_codeOffset, _length, _taintRanges);
	return {
		codeCopyMayReadRanges(_codeOffset, _length, _taintRanges->foreignReferences) ||
			!foreignTags.empty(),
		codeCopyMayReadRanges(_codeOffset, _length, _taintRanges->localTags) ||
			codeCopyMayReadLocalTagReferences(_codeOffset, _length, _taintRanges),
		codeCopyMayReadRanges(_codeOffset, _length, _taintRanges->currentAddresses),
		codeCopyMayReadRanges(_codeOffset, _length, _taintRanges->complementedCurrentAddresses),
		std::move(foreignTags)
	};
}

CodeCopyReadTaints extCodeCopyMayReadTaints(
	AbstractStackValue const& _address,
	AbstractStackValue const& _codeOffset,
	AbstractStackValue const& _length,
	CodeCopyTaintRanges const* _taintRanges
)
{
	(void)_address;
	// The assembler cannot prove that an arbitrary EXTCODECOPY address is not
	// the current contract, so source ranges are tainted like CODECOPY.
	return codeCopyMayReadTaints(_codeOffset, _length, _taintRanges);
}

bool valueMayBeNonZero(AbstractStackValue const& _value)
{
	return !_value.literalValue || *_value.literalValue != 0;
}

bool valueIsLiteral(AbstractStackValue const& _value, u512 const& _literal)
{
	return _value.literalValue && *_value.literalValue == _literal;
}

bool valueIsLiteralAtLeast(AbstractStackValue const& _value, u512 const& _literal)
{
	return _value.literalValue && *_value.literalValue >= _literal;
}

bool valueLowByteIsLiteral(AbstractStackValue const& _value, u512 const& _literal)
{
	return _value.literalValue && ((*_value.literalValue & u512(0xff)) == _literal);
}

bool valueIsBytePreservingAndMask(AbstractStackValue const& _value)
{
	return valueLowByteIsLiteral(_value, u512(0xff));
}

bool valueIsLocalTagPreservingAndMask(AbstractStackValue const& _value)
{
	return
		_value.literalValue &&
		((*_value.literalValue & u512(std::numeric_limits<unsigned>::max())) ==
			u512(std::numeric_limits<unsigned>::max()));
}

bool localTagLeftShiftIsLossless(size_t _shift)
{
	return _shift <= VMWordBits - c_localTagBits;
}

bool valueIsNonZeroMultipleOfByteRange(AbstractStackValue const& _value)
{
	return _value.literalValue && *_value.literalValue != 0 && valueLowByteIsLiteral(_value, u512(0));
}

bool valueMayBeLocalTag(AbstractStackValue const& _value)
{
	return _value.mayBeExternalJumpTarget || _value.mayBeUnknownLocalTag || !_value.localTags.empty();
}

bool isMemoryStoreInstruction(Instruction _instruction)
{
	return _instruction == Instruction::MSTORE || _instruction == Instruction::MSTORE8;
}

bool isStorageStoreInstruction(Instruction _instruction)
{
	return _instruction == Instruction::SSTORE;
}

bool instructionMayConsumeDuplicateRelation(Instruction _instruction)
{
	switch (_instruction)
	{
	case Instruction::ISZERO:
	case Instruction::SUB:
	case Instruction::XOR:
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::MOD:
	case Instruction::SMOD:
		return true;
	default:
		return false;
	}
}

bool operationOutputCanPropagateStackInput(Instruction _instruction)
{
	switch (_instruction)
	{
	case Instruction::LT:
	case Instruction::GT:
	case Instruction::SLT:
	case Instruction::SGT:
	case Instruction::EQ:
	case Instruction::ISZERO:
	case Instruction::KECCAK256:
	case Instruction::CALLDATALOAD:
	case Instruction::BALANCE:
	case Instruction::EXTCODESIZE:
	case Instruction::EXTCODEHASH:
	case Instruction::BLOCKHASH:
	case Instruction::MLOAD:
	case Instruction::SLOAD:
	case Instruction::CREATE:
	case Instruction::CREATE2:
		return false;
	default:
		return !isCallInstruction(_instruction);
	}
}

template <class StackArgument>
std::optional<u512> operationInputIndependentLiteral(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		if (_parameterIndex >= _arguments)
			return std::nullopt;
		return _stackArgument(_parameterIndex).literalValue;
	};
	auto isLiteral = [&](size_t _parameterIndex, u512 const& _literal)
	{
		std::optional<u512> value = literalAt(_parameterIndex);
		return value && *value == _literal;
	};
	auto sameValue = [&](size_t _left, size_t _right)
	{
		if (_left >= _arguments || _right >= _arguments)
			return false;
		AbstractStackValue const& left = _stackArgument(_left);
		AbstractStackValue const& right = _stackArgument(_right);
		if (left.literalValue && right.literalValue && *left.literalValue == *right.literalValue)
			return true;
		if (_left < _right && left.duplicateSourceBelowTop && *left.duplicateSourceBelowTop == _right - _left)
			return true;
		if (_right < _left && right.duplicateSourceBelowTop && *right.duplicateSourceBelowTop == _left - _right)
			return true;
		return false;
	};

	switch (_instruction)
	{
	case Instruction::SUB:
	case Instruction::XOR:
		if (_arguments >= 2 && sameValue(0, 1))
			return u512(0);
		break;
	case Instruction::AND:
	case Instruction::MUL:
		if (_arguments >= 2 && (isLiteral(0, 0) || isLiteral(1, 0)))
			return u512(0);
		break;
	case Instruction::OR:
		if (_arguments >= 2 && (isLiteral(0, ~u512(0)) || isLiteral(1, ~u512(0))))
			return ~u512(0);
		break;
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::MOD:
	case Instruction::SMOD:
		if (_arguments >= 2 && (isLiteral(0, 0) || isLiteral(1, 0)))
			return u512(0);
		if ((_instruction == Instruction::MOD || _instruction == Instruction::SMOD) && _arguments >= 2 && sameValue(0, 1))
			return u512(0);
		if ((_instruction == Instruction::MOD || _instruction == Instruction::SMOD) && _arguments >= 2 && isLiteral(0, 1))
			return u512(0);
		break;
	case Instruction::ADDMOD:
		if (
			_arguments >= 3 &&
			(isLiteral(0, 0) || isLiteral(0, 1) || (isLiteral(1, 0) && isLiteral(2, 0)))
		)
			return u512(0);
		break;
	case Instruction::MULMOD:
		if (
			_arguments >= 3 &&
			(isLiteral(0, 0) || isLiteral(0, 1) || isLiteral(1, 0) || isLiteral(2, 0))
		)
			return u512(0);
		break;
	case Instruction::EXP:
		if (_arguments >= 2 && isLiteral(0, 0))
			return u512(1);
		if (_arguments >= 2 && isLiteral(1, 1))
			return u512(1);
		if (_arguments >= 2 && isLiteral(1, 0))
		{
			std::optional<u512> exponent = literalAt(0);
			if (exponent && *exponent != 0)
				return u512(0);
		}
		break;
	case Instruction::SIGNEXTEND:
		if (_arguments >= 2 && isLiteral(1, 0))
			return u512(0);
		if (_arguments >= 2 && isLiteral(1, ~u512(0)))
			return ~u512(0);
		break;
	case Instruction::SHL:
	case Instruction::SHR:
		if (_arguments >= 2 && isLiteral(1, 0))
			return u512(0);
		if (_arguments >= 2)
		{
			std::optional<u512> shift = literalAt(0);
			if (shift && *shift >= VMWordBits)
				return u512(0);
		}
		break;
	case Instruction::SAR:
		if (_arguments >= 2 && isLiteral(1, 0))
			return u512(0);
		break;
	case Instruction::BYTE:
	{
		if (_arguments >= 2 && isLiteral(1, 0))
			return u512(0);
		std::optional<u512> byteIndex = literalAt(0);
		if (_arguments >= 2 && byteIndex && *byteIndex >= VMWordBytes)
			return u512(0);
		break;
	}
	default:
		break;
	}
	return std::nullopt;
}

template <class StackArgument>
bool operationOutputCanPropagateStackInput(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (operationInputIndependentLiteral(_instruction, _arguments, _stackArgument))
		return false;

	auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		if (_parameterIndex >= _arguments)
			return std::nullopt;
		return _stackArgument(_parameterIndex).literalValue;
	};
	auto isLiteral = [&](size_t _parameterIndex, u512 const& _literal)
	{
		std::optional<u512> value = literalAt(_parameterIndex);
		return value && *value == _literal;
	};
	auto sameValue = [&](size_t _left, size_t _right)
	{
		if (_left >= _arguments || _right >= _arguments)
			return false;
		AbstractStackValue const& left = _stackArgument(_left);
		AbstractStackValue const& right = _stackArgument(_right);
		if (left.literalValue && right.literalValue && *left.literalValue == *right.literalValue)
			return true;
		if (_left < _right && left.duplicateSourceBelowTop && *left.duplicateSourceBelowTop == _right - _left)
			return true;
		if (_right < _left && right.duplicateSourceBelowTop && *right.duplicateSourceBelowTop == _left - _right)
			return true;
		return false;
	};

	switch (_instruction)
	{
	case Instruction::DIV:
	case Instruction::SDIV:
		if (_arguments >= 2 && sameValue(0, 1))
			return false;
		break;
	case Instruction::EXP:
		if (_arguments >= 2 && isLiteral(1, 0))
			return false;
		break;
	case Instruction::SAR:
	{
		std::optional<u512> shift = literalAt(0);
		if (_arguments >= 2 && shift && *shift >= VMWordBits)
			return false;
		break;
	}
	default:
		break;
	}
	return operationOutputCanPropagateStackInput(_instruction);
}

template <class StackArgument, class ValuePredicate>
bool memoryStoreValueMay(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValuePredicate _valuePredicate
)
{
	return isMemoryStoreInstruction(_instruction) && _arguments >= 2 && _valuePredicate(_stackArgument(1));
}

template <class StackArgument, class ValuePredicate>
bool storageStoreValueMay(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValuePredicate _valuePredicate
)
{
	return isStorageStoreInstruction(_instruction) && _arguments >= 2 && _valuePredicate(_stackArgument(1));
}

bool addressByteMapContainsWord(AddressByteMap const& _bytes, size_t _baseOffset);
AddressByteMap currentAddressStackBytes(AbstractStackValue const& _value);
AddressByteMap complementedCurrentAddressStackBytes(AbstractStackValue const& _value);
AddressByteMaskMap currentAddressMaskedStackBytes(AbstractStackValue const& _value);
AddressByteMaskMap complementedCurrentAddressMaskedStackBytes(AbstractStackValue const& _value);

bool valueHasExactCurrentAddressFlag(AbstractStackValue const& _value)
{
	return
		_value.mayBeCurrentAddress ||
		(_value.currentAddressOffset && *_value.currentAddressOffset == 0) ||
		(_value.currentAddressXorMask && *_value.currentAddressXorMask == 0);
}

bool valueHasExactComplementedCurrentAddressFlag(AbstractStackValue const& _value)
{
	return
		_value.mayBeComplementedCurrentAddress ||
		(_value.currentAddressXorMask && *_value.currentAddressXorMask == ~u512(0)) ||
		(_value.complementedCurrentAddressOffset && *_value.complementedCurrentAddressOffset == 0);
}

bool valueMayBeCurrentAddressDerived(AbstractStackValue const& _value)
{
	return
			_value.mayBeCurrentAddress ||
			_value.mayBeCurrentAddressDerived ||
			_value.currentAddressOffset ||
			!_value.possibleCurrentAddressOffsets.empty() ||
			_value.currentAddressXorMask ||
			addressByteMapContainsWord(currentAddressStackBytes(_value), 0);
}

bool valueMayBeExactCurrentAddress(AbstractStackValue const& _value)
{
		return
			valueHasExactCurrentAddressFlag(_value) ||
			_value.possibleCurrentAddressOffsets.count(u512(0)) != 0 ||
			addressByteMapContainsWord(currentAddressStackBytes(_value), 0);
}

bool valueMayBeComplementedCurrentAddressDerived(AbstractStackValue const& _value)
{
	return
		_value.mayBeComplementedCurrentAddress ||
		_value.mayBeComplementedCurrentAddressDerived ||
		_value.currentAddressXorMask ||
		_value.complementedCurrentAddressOffset ||
		addressByteMapContainsWord(complementedCurrentAddressStackBytes(_value), 0);
}

bool valueMayBeExactComplementedCurrentAddress(AbstractStackValue const& _value)
{
	return
		valueHasExactComplementedCurrentAddressFlag(_value) ||
		addressByteMapContainsWord(complementedCurrentAddressStackBytes(_value), 0);
}

std::optional<size_t> literalSizeT(AbstractStackValue const& _value)
{
	if (!_value.literalValue || *_value.literalValue > std::numeric_limits<size_t>::max())
		return std::nullopt;
	return static_cast<size_t>(*_value.literalValue);
}

uint8_t literalByteAtPosition(u512 const& _value, size_t _position)
{
	assertThrow(_position < VMWordBytes, AssemblyException, "Invalid stack byte position.");
	size_t const shift = (VMWordBytes - 1 - _position) * 8;
	return static_cast<uint8_t>(static_cast<unsigned>((_value >> shift) & u512(0xff)));
}

std::set<size_t> zeroBytePositions(AbstractStackValue const& _value)
{
	if (!_value.literalValue)
		return _value.zeroBytePositions;
	std::set<size_t> positions;
	for (size_t position = 0; position < VMWordBytes; ++position)
		if (literalByteAtPosition(*_value.literalValue, position) == 0)
			positions.insert(position);
	return positions;
}

std::set<size_t> ffBytePositions(AbstractStackValue const& _value)
{
	if (!_value.literalValue)
		return _value.ffBytePositions;
	std::set<size_t> positions;
	for (size_t position = 0; position < VMWordBytes; ++position)
		if (literalByteAtPosition(*_value.literalValue, position) == 0xff)
			positions.insert(position);
	return positions;
}

void markZeroBytePosition(AbstractStackValue& _value, size_t _position)
{
	_value.zeroBytePositions.insert(_position);
	_value.ffBytePositions.erase(_position);
	_value.possibleOneBitMasks.erase(_position);
}

void markFFBytePosition(AbstractStackValue& _value, size_t _position)
{
	_value.ffBytePositions.insert(_position);
	_value.zeroBytePositions.erase(_position);
	_value.possibleOneBitMasks.erase(_position);
}

bool bytePositionKnownZero(AbstractStackValue const& _value, size_t _position)
{
	if (_value.literalValue)
		return literalByteAtPosition(*_value.literalValue, _position) == 0;
	return _value.zeroBytePositions.count(_position) != 0;
}

bool bytePositionKnownFF(AbstractStackValue const& _value, size_t _position)
{
	if (_value.literalValue)
		return literalByteAtPosition(*_value.literalValue, _position) == 0xff;
	return _value.ffBytePositions.count(_position) != 0;
}

uint8_t possibleOneBitMaskAtPosition(AbstractStackValue const& _value, size_t _position)
{
	if (_value.literalValue)
		return literalByteAtPosition(*_value.literalValue, _position);
	if (bytePositionKnownZero(_value, _position))
		return 0;
	if (auto mask = _value.possibleOneBitMasks.find(_position); mask != _value.possibleOneBitMasks.end())
		return mask->second;
	return 0xff;
}

void setPossibleOneBitMask(AbstractStackValue& _value, size_t _position, uint8_t _mask)
{
	if (_mask == 0)
	{
		markZeroBytePosition(_value, _position);
		return;
	}
	_value.zeroBytePositions.erase(_position);
	if (_mask == 0xff)
	{
		_value.possibleOneBitMasks.erase(_position);
		return;
	}
	_value.ffBytePositions.erase(_position);
	_value.possibleOneBitMasks[_position] = _mask;
}

bool valueKnownByteSized(AbstractStackValue const& _value)
{
	for (size_t position = 0; position + 1 < VMWordBytes; ++position)
		if (!bytePositionKnownZero(_value, position))
			return false;
	return true;
}

std::optional<uint8_t> byteSizedValuePossibleLowByteOneMask(AbstractStackValue const& _value)
{
	if (!valueKnownByteSized(_value))
		return std::nullopt;
	if (_value.literalValue)
		return literalByteAtPosition(*_value.literalValue, c_lowStackBytePosition);
	if (bytePositionKnownZero(_value, c_lowStackBytePosition))
		return 0;

	return possibleOneBitMaskAtPosition(_value, c_lowStackBytePosition);
}

bool byteSizedAddIsKnownCarryFree(AbstractStackValue const& _left, AbstractStackValue const& _right)
{
	std::optional<uint8_t> leftMask = byteSizedValuePossibleLowByteOneMask(_left);
	std::optional<uint8_t> rightMask = byteSizedValuePossibleLowByteOneMask(_right);
	return leftMask && rightMask && ((*leftMask & *rightMask) == 0);
}

void markByteExtractedValue(AbstractStackValue& _value)
{
	for (size_t position = 0; position + 1 < VMWordBytes; ++position)
		markZeroBytePosition(_value, position);
}

bool addressByteMaskHasConflictingSource(AddressByteMaskMap const& _maskedBytes, size_t _position, size_t _byteIndex)
{
	for (auto const& [key, mask]: _maskedBytes)
		if (key.first == _position && key.second != _byteIndex && mask != 0)
			return true;
	return false;
}

void setAddressByteMask(
	AddressByteMaskMap& _maskedBytes,
	AddressByteMap& _exactBytes,
	size_t _position,
	size_t _byteIndex,
	uint8_t _mask
)
{
	if (_mask == 0)
		return;
	uint8_t& storedMask = _maskedBytes[{_position, _byteIndex}];
	storedMask = static_cast<uint8_t>(storedMask | _mask);
	auto exactByte = _exactBytes.find(_position);
	bool const conflict =
		(exactByte != _exactBytes.end() && exactByte->second != _byteIndex) ||
		addressByteMaskHasConflictingSource(_maskedBytes, _position, _byteIndex);
	if (conflict)
	{
		if (exactByte != _exactBytes.end())
			_maskedBytes[{_position, exactByte->second}] = 0xff;
		_exactBytes.erase(_position);
	}
	else if (storedMask == 0xff)
		_exactBytes[_position] = _byteIndex;
}

void setAddressExactByte(
	AddressByteMaskMap& _maskedBytes,
	AddressByteMap& _exactBytes,
	size_t _position,
	size_t _byteIndex
)
{
	setAddressByteMask(_maskedBytes, _exactBytes, _position, _byteIndex, 0xff);
}

void setCurrentAddressByteMask(AbstractStackValue& _value, size_t _position, size_t _byteIndex, uint8_t _mask)
{
	setAddressByteMask(_value.currentAddressMaskedStackBytes, _value.currentAddressStackBytes, _position, _byteIndex, _mask);
}

void setComplementedCurrentAddressByteMask(AbstractStackValue& _value, size_t _position, size_t _byteIndex, uint8_t _mask)
{
	setAddressByteMask(
		_value.complementedCurrentAddressMaskedStackBytes,
		_value.complementedCurrentAddressStackBytes,
		_position,
		_byteIndex,
		_mask
	);
}

void setCurrentAddressExactByte(AbstractStackValue& _value, size_t _position, size_t _byteIndex)
{
	setAddressExactByte(_value.currentAddressMaskedStackBytes, _value.currentAddressStackBytes, _position, _byteIndex);
}

void setComplementedCurrentAddressExactByte(AbstractStackValue& _value, size_t _position, size_t _byteIndex)
{
	setAddressExactByte(
		_value.complementedCurrentAddressMaskedStackBytes,
		_value.complementedCurrentAddressStackBytes,
		_position,
		_byteIndex
	);
}

void setCurrentAddressLowByte(AbstractStackValue& _value, size_t _byteIndex)
{
	_value.currentAddressByteIndex = _byteIndex;
	setCurrentAddressExactByte(_value, c_lowStackBytePosition, _byteIndex);
}

void setComplementedCurrentAddressLowByte(AbstractStackValue& _value, size_t _byteIndex)
{
	_value.complementedCurrentAddressByteIndex = _byteIndex;
	setComplementedCurrentAddressExactByte(_value, c_lowStackBytePosition, _byteIndex);
}

void materializeExactByteMasks(
	AddressByteMaskMap const& _maskedBytes,
	AddressByteMap& _exactBytes
)
{
	for (auto const& [key, mask]: _maskedBytes)
		if (mask == 0xff && !addressByteMaskHasConflictingSource(_maskedBytes, key.first, key.second))
			_exactBytes[key.first] = key.second;
}

AddressByteMap currentAddressStackBytes(AbstractStackValue const& _value)
{
	AddressByteMap bytes = _value.currentAddressStackBytes;
	materializeExactByteMasks(_value.currentAddressMaskedStackBytes, bytes);
	if (valueHasExactCurrentAddressFlag(_value))
		for (size_t position = 0; position < VMWordBytes; ++position)
			bytes[position] = position;
	return bytes;
}

AddressByteMap complementedCurrentAddressStackBytes(AbstractStackValue const& _value)
{
	AddressByteMap bytes = _value.complementedCurrentAddressStackBytes;
	materializeExactByteMasks(_value.complementedCurrentAddressMaskedStackBytes, bytes);
	if (valueHasExactComplementedCurrentAddressFlag(_value))
		for (size_t position = 0; position < VMWordBytes; ++position)
			bytes[position] = position;
	return bytes;
}

AddressByteMaskMap currentAddressMaskedStackBytes(AbstractStackValue const& _value)
{
	AddressByteMaskMap bytes = _value.currentAddressMaskedStackBytes;
	for (auto const& [position, byteIndex]: _value.currentAddressStackBytes)
		bytes[{position, byteIndex}] = 0xff;
	if (valueHasExactCurrentAddressFlag(_value))
		for (size_t position = 0; position < VMWordBytes; ++position)
			bytes[{position, position}] = 0xff;
	return bytes;
}

AddressByteMaskMap complementedCurrentAddressMaskedStackBytes(AbstractStackValue const& _value)
{
	AddressByteMaskMap bytes = _value.complementedCurrentAddressMaskedStackBytes;
	for (auto const& [position, byteIndex]: _value.complementedCurrentAddressStackBytes)
		bytes[{position, byteIndex}] = 0xff;
	if (valueHasExactComplementedCurrentAddressFlag(_value))
		for (size_t position = 0; position < VMWordBytes; ++position)
			bytes[{position, position}] = 0xff;
	return bytes;
}

void copyStackByteMetadata(AbstractStackValue const& _input, AbstractStackValue& _output)
{
	for (auto const& [position, byteIndex]: _input.currentAddressStackBytes)
		setCurrentAddressExactByte(_output, position, byteIndex);
	for (auto const& [position, byteIndex]: _input.complementedCurrentAddressStackBytes)
		setComplementedCurrentAddressExactByte(_output, position, byteIndex);
	for (auto const& [key, mask]: _input.currentAddressMaskedStackBytes)
		setCurrentAddressByteMask(_output, key.first, key.second, mask);
	for (auto const& [key, mask]: _input.complementedCurrentAddressMaskedStackBytes)
		setComplementedCurrentAddressByteMask(_output, key.first, key.second, mask);
	for (size_t position: _input.zeroBytePositions)
		markZeroBytePosition(_output, position);
	for (size_t position: _input.ffBytePositions)
		markFFBytePosition(_output, position);
	for (auto const& [position, mask]: _input.possibleOneBitMasks)
		setPossibleOneBitMask(_output, position, mask);
}

void complementStackByteMetadata(AbstractStackValue const& _input, AbstractStackValue& _output)
{
	for (auto const& [position, byteIndex]: _input.complementedCurrentAddressStackBytes)
		setCurrentAddressExactByte(_output, position, byteIndex);
	for (auto const& [position, byteIndex]: _input.currentAddressStackBytes)
		setComplementedCurrentAddressExactByte(_output, position, byteIndex);
	for (auto const& [key, mask]: _input.currentAddressMaskedStackBytes)
		setComplementedCurrentAddressByteMask(_output, key.first, key.second, mask);
	for (auto const& [key, mask]: _input.complementedCurrentAddressMaskedStackBytes)
		setCurrentAddressByteMask(_output, key.first, key.second, mask);
	for (size_t position: zeroBytePositions(_input))
		markFFBytePosition(_output, position);
	for (size_t position: ffBytePositions(_input))
		markZeroBytePosition(_output, position);
}

void shiftStackByteMetadata(
	AbstractStackValue const& _input,
	AbstractStackValue& _output,
	size_t _shiftBytes,
	bool _left
)
{
	auto shiftPosition = [&](size_t _position) -> std::optional<size_t>
	{
		if (_left)
		{
			if (_position < _shiftBytes)
				return std::nullopt;
			return _position - _shiftBytes;
		}
		if (_position > c_lowStackBytePosition - _shiftBytes)
			return std::nullopt;
		return _position + _shiftBytes;
	};
	auto shiftMarkers = [&](AddressByteMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [position, byteIndex]: _inputBytes)
			if (std::optional<size_t> shiftedPosition = shiftPosition(position))
			{
				if (_currentAddress)
					setCurrentAddressExactByte(_output, *shiftedPosition, byteIndex);
				else
					setComplementedCurrentAddressExactByte(_output, *shiftedPosition, byteIndex);
			}
	};
	auto shiftMaskedMarkers = [&](AddressByteMaskMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [key, mask]: _inputBytes)
			if (std::optional<size_t> shiftedPosition = shiftPosition(key.first))
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, *shiftedPosition, key.second, mask);
				else
					setComplementedCurrentAddressByteMask(_output, *shiftedPosition, key.second, mask);
			}
	};
	shiftMarkers(currentAddressStackBytes(_input), true);
	shiftMarkers(complementedCurrentAddressStackBytes(_input), false);
	shiftMaskedMarkers(currentAddressMaskedStackBytes(_input), true);
	shiftMaskedMarkers(complementedCurrentAddressMaskedStackBytes(_input), false);
	for (size_t position: zeroBytePositions(_input))
		if (std::optional<size_t> shiftedPosition = shiftPosition(position))
			markZeroBytePosition(_output, *shiftedPosition);
	for (size_t position: ffBytePositions(_input))
		if (std::optional<size_t> shiftedPosition = shiftPosition(position))
			markFFBytePosition(_output, *shiftedPosition);
	if (_left)
		for (size_t position = VMWordBytes - _shiftBytes; position < VMWordBytes; ++position)
			markZeroBytePosition(_output, position);
	else
		for (size_t position = 0; position < _shiftBytes; ++position)
			markZeroBytePosition(_output, position);
}

void shiftStackBitMetadata(
	AbstractStackValue const& _input,
	AbstractStackValue& _output,
	size_t _shiftBits,
	bool _left
)
{
	assertThrow(_shiftBits < VMWordBits, AssemblyException, "Invalid stack bit shift.");
	if (_shiftBits % 8 == 0)
		shiftStackByteMetadata(_input, _output, _shiftBits / 8, _left);

	size_t const shiftBytes = _shiftBits / 8;
	size_t const bitShift = _shiftBits % 8;

	auto addShiftedMask = [&](size_t _position, size_t _byteIndex, uint8_t _mask, bool _currentAddress)
	{
		if (_mask == 0)
			return;
		auto addOutputMask = [&](size_t _outputPosition, uint8_t _outputMask)
		{
			if (_currentAddress)
				setCurrentAddressByteMask(_output, _outputPosition, _byteIndex, _outputMask);
			else
				setComplementedCurrentAddressByteMask(_output, _outputPosition, _byteIndex, _outputMask);
		};
		if (_left)
		{
			if (_position < shiftBytes)
				return;
			size_t const shiftedPosition = _position - shiftBytes;
			if (bitShift == 0)
				addOutputMask(shiftedPosition, _mask);
			else
			{
				addOutputMask(shiftedPosition, static_cast<uint8_t>(_mask << bitShift));
				if (shiftedPosition > 0)
					addOutputMask(shiftedPosition - 1, static_cast<uint8_t>(_mask >> (8 - bitShift)));
			}
		}
		else
		{
			if (_position > c_lowStackBytePosition - shiftBytes)
				return;
			size_t const shiftedPosition = _position + shiftBytes;
			if (bitShift == 0)
				addOutputMask(shiftedPosition, _mask);
			else
			{
				addOutputMask(shiftedPosition, static_cast<uint8_t>(_mask >> bitShift));
				if (shiftedPosition < c_lowStackBytePosition)
					addOutputMask(shiftedPosition + 1, static_cast<uint8_t>(_mask << (8 - bitShift)));
			}
		}
	};

	if (bitShift != 0)
	{
		for (auto const& [key, mask]: currentAddressMaskedStackBytes(_input))
			addShiftedMask(key.first, key.second, mask, true);
		for (auto const& [key, mask]: complementedCurrentAddressMaskedStackBytes(_input))
			addShiftedMask(key.first, key.second, mask, false);
	}

	std::array<uint8_t, VMWordBytes> possibleOneBitMasks{};
	auto addShiftedPossibleMask = [&](size_t _position, uint8_t _mask)
	{
		if (_left)
		{
			if (_position < shiftBytes)
				return;
			size_t const shiftedPosition = _position - shiftBytes;
			if (bitShift == 0)
				possibleOneBitMasks[shiftedPosition] =
					static_cast<uint8_t>(possibleOneBitMasks[shiftedPosition] | _mask);
			else
			{
				possibleOneBitMasks[shiftedPosition] = static_cast<uint8_t>(
					possibleOneBitMasks[shiftedPosition] | static_cast<uint8_t>(_mask << bitShift)
				);
				if (shiftedPosition > 0)
					possibleOneBitMasks[shiftedPosition - 1] = static_cast<uint8_t>(
						possibleOneBitMasks[shiftedPosition - 1] | static_cast<uint8_t>(_mask >> (8 - bitShift))
					);
			}
		}
		else
		{
			if (_position > c_lowStackBytePosition - shiftBytes)
				return;
			size_t const shiftedPosition = _position + shiftBytes;
			if (bitShift == 0)
				possibleOneBitMasks[shiftedPosition] =
					static_cast<uint8_t>(possibleOneBitMasks[shiftedPosition] | _mask);
			else
			{
				possibleOneBitMasks[shiftedPosition] = static_cast<uint8_t>(
					possibleOneBitMasks[shiftedPosition] | static_cast<uint8_t>(_mask >> bitShift)
				);
				if (shiftedPosition < c_lowStackBytePosition)
					possibleOneBitMasks[shiftedPosition + 1] = static_cast<uint8_t>(
						possibleOneBitMasks[shiftedPosition + 1] | static_cast<uint8_t>(_mask << (8 - bitShift))
					);
			}
		}
	};
	for (size_t position = 0; position < VMWordBytes; ++position)
		addShiftedPossibleMask(position, possibleOneBitMaskAtPosition(_input, position));
	for (size_t position = 0; position < VMWordBytes; ++position)
		setPossibleOneBitMask(_output, position, possibleOneBitMasks[position]);
}

void propagateOrStackByteMetadata(
	AbstractStackValue const& _left,
	AbstractStackValue const& _right,
	AbstractStackValue& _output
)
{
	std::set<size_t> const leftZeroBytes = zeroBytePositions(_left);
	std::set<size_t> const rightZeroBytes = zeroBytePositions(_right);
	std::set<size_t> outputZeroBytes;
	std::set_intersection(
		leftZeroBytes.begin(),
		leftZeroBytes.end(),
		rightZeroBytes.begin(),
		rightZeroBytes.end(),
		std::inserter(outputZeroBytes, outputZeroBytes.end())
	);
	for (size_t position: outputZeroBytes)
		markZeroBytePosition(_output, position);
	for (size_t position: ffBytePositions(_left))
		markFFBytePosition(_output, position);
	for (size_t position: ffBytePositions(_right))
		markFFBytePosition(_output, position);
	for (size_t position = 0; position < VMWordBytes; ++position)
		setPossibleOneBitMask(
			_output,
			position,
			static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_left, position) |
				possibleOneBitMaskAtPosition(_right, position)
			)
		);

	auto preserveMarkers = [&](
		AddressByteMap const& _inputBytes,
		AbstractStackValue const& _otherInput,
		bool _currentAddress
	)
	{
		for (auto const& [position, byteIndex]: _inputBytes)
			if (bytePositionKnownZero(_otherInput, position))
			{
				if (_currentAddress)
					setCurrentAddressExactByte(_output, position, byteIndex);
				else
					setComplementedCurrentAddressExactByte(_output, position, byteIndex);
			}
	};
	preserveMarkers(currentAddressStackBytes(_left), _right, true);
	preserveMarkers(currentAddressStackBytes(_right), _left, true);
	preserveMarkers(complementedCurrentAddressStackBytes(_left), _right, false);
	preserveMarkers(complementedCurrentAddressStackBytes(_right), _left, false);

	auto preserveMaskedMarkers = [&](
		AddressByteMaskMap const& _inputBytes,
		AbstractStackValue const& _otherInput,
		bool _currentAddress
	)
	{
		for (auto const& [key, mask]: _inputBytes)
			if ((possibleOneBitMaskAtPosition(_otherInput, key.first) & mask) == 0)
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, key.first, key.second, mask);
				else
					setComplementedCurrentAddressByteMask(_output, key.first, key.second, mask);
			}
	};
	auto combineMaskedMarkers = [&](
		AddressByteMaskMap const& _leftBytes,
		AddressByteMaskMap const& _rightBytes,
		AbstractStackValue const& _leftInput,
		AbstractStackValue const& _rightInput,
		bool _currentAddress
	)
	{
		for (auto const& [key, leftMask]: _leftBytes)
		{
			auto rightByte = _rightBytes.find(key);
			if (rightByte == _rightBytes.end())
				continue;
			uint8_t const rightMask = rightByte->second;
			if ((leftMask & rightMask) != 0)
				continue;
			uint8_t const leftOtherMask = static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_leftInput, key.first) & static_cast<uint8_t>(~leftMask)
			);
			uint8_t const rightOtherMask = static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_rightInput, key.first) & static_cast<uint8_t>(~rightMask)
			);
			if ((rightOtherMask & leftMask) != 0 || (leftOtherMask & rightMask) != 0)
				continue;
			uint8_t const combinedMask = static_cast<uint8_t>(leftMask | rightMask);
			if (_currentAddress)
				setCurrentAddressByteMask(_output, key.first, key.second, combinedMask);
			else
				setComplementedCurrentAddressByteMask(_output, key.first, key.second, combinedMask);
		}
	};
	AddressByteMaskMap const leftCurrentBytes = currentAddressMaskedStackBytes(_left);
	AddressByteMaskMap const rightCurrentBytes = currentAddressMaskedStackBytes(_right);
	AddressByteMaskMap const leftComplementedBytes = complementedCurrentAddressMaskedStackBytes(_left);
	AddressByteMaskMap const rightComplementedBytes = complementedCurrentAddressMaskedStackBytes(_right);
	preserveMaskedMarkers(leftCurrentBytes, _right, true);
	preserveMaskedMarkers(rightCurrentBytes, _left, true);
	preserveMaskedMarkers(leftComplementedBytes, _right, false);
	preserveMaskedMarkers(rightComplementedBytes, _left, false);
	combineMaskedMarkers(leftCurrentBytes, rightCurrentBytes, _left, _right, true);
	combineMaskedMarkers(leftComplementedBytes, rightComplementedBytes, _left, _right, false);
}

void propagateDisjointMaskedStackByteMetadata(
	AbstractStackValue const& _left,
	AbstractStackValue const& _right,
	AbstractStackValue& _output
)
{
	auto preserveMaskedMarkers = [&](
		AddressByteMaskMap const& _inputBytes,
		AbstractStackValue const& _otherInput,
		bool _currentAddress
	)
	{
		for (auto const& [key, mask]: _inputBytes)
			if ((possibleOneBitMaskAtPosition(_otherInput, key.first) & mask) == 0)
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, key.first, key.second, mask);
				else
					setComplementedCurrentAddressByteMask(_output, key.first, key.second, mask);
			}
	};
	auto combineMaskedMarkers = [&](
		AddressByteMaskMap const& _leftBytes,
		AddressByteMaskMap const& _rightBytes,
		AbstractStackValue const& _leftInput,
		AbstractStackValue const& _rightInput,
		bool _currentAddress
	)
	{
		for (auto const& [key, leftMask]: _leftBytes)
		{
			auto rightByte = _rightBytes.find(key);
			if (rightByte == _rightBytes.end())
				continue;
			uint8_t const rightMask = rightByte->second;
			if ((leftMask & rightMask) != 0)
				continue;
			uint8_t const leftOtherMask = static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_leftInput, key.first) & static_cast<uint8_t>(~leftMask)
			);
			uint8_t const rightOtherMask = static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_rightInput, key.first) & static_cast<uint8_t>(~rightMask)
			);
			if ((rightOtherMask & leftMask) != 0 || (leftOtherMask & rightMask) != 0)
				continue;
			uint8_t const combinedMask = static_cast<uint8_t>(leftMask | rightMask);
			if (_currentAddress)
				setCurrentAddressByteMask(_output, key.first, key.second, combinedMask);
			else
				setComplementedCurrentAddressByteMask(_output, key.first, key.second, combinedMask);
		}
	};
	AddressByteMaskMap const leftCurrentBytes = currentAddressMaskedStackBytes(_left);
	AddressByteMaskMap const rightCurrentBytes = currentAddressMaskedStackBytes(_right);
	AddressByteMaskMap const leftComplementedBytes = complementedCurrentAddressMaskedStackBytes(_left);
	AddressByteMaskMap const rightComplementedBytes = complementedCurrentAddressMaskedStackBytes(_right);
	preserveMaskedMarkers(leftCurrentBytes, _right, true);
	preserveMaskedMarkers(rightCurrentBytes, _left, true);
	preserveMaskedMarkers(leftComplementedBytes, _right, false);
	preserveMaskedMarkers(rightComplementedBytes, _left, false);
	combineMaskedMarkers(leftCurrentBytes, rightCurrentBytes, _left, _right, true);
	combineMaskedMarkers(leftComplementedBytes, rightComplementedBytes, _left, _right, false);
}

bool bytewiseInputsAreKnownDisjoint(AbstractStackValue const& _left, AbstractStackValue const& _right)
{
	for (size_t position = 0; position < VMWordBytes; ++position)
		if ((possibleOneBitMaskAtPosition(_left, position) & possibleOneBitMaskAtPosition(_right, position)) != 0)
			return false;
	return true;
}

void propagateAddStackByteMetadata(
	AbstractStackValue const& _left,
	AbstractStackValue const& _right,
	AbstractStackValue& _output
)
{
	if (byteSizedAddIsKnownCarryFree(_left, _right))
		markByteExtractedValue(_output);
	if (bytewiseInputsAreKnownDisjoint(_left, _right))
		propagateOrStackByteMetadata(_left, _right, _output);
}

void propagateXorStackByteMetadata(
	AbstractStackValue const& _left,
	AbstractStackValue const& _right,
	AbstractStackValue& _output
)
{
	for (size_t position = 0; position < VMWordBytes; ++position)
		setPossibleOneBitMask(
			_output,
			position,
			static_cast<uint8_t>(
				possibleOneBitMaskAtPosition(_left, position) |
				possibleOneBitMaskAtPosition(_right, position)
			)
		);
	propagateDisjointMaskedStackByteMetadata(_left, _right, _output);
	if (valueKnownByteSized(_left) && valueKnownByteSized(_right))
		markByteExtractedValue(_output);
	if (bytewiseInputsAreKnownDisjoint(_left, _right))
		propagateOrStackByteMetadata(_left, _right, _output);
}

std::optional<size_t> powerOfTwoBitShift(u512 const& _value)
{
	if (_value == 0)
		return std::nullopt;
	for (size_t shiftBits = 0; shiftBits < VMWordBits; ++shiftBits)
		if (_value == (u512(1) << shiftBits))
			return shiftBits;
	return std::nullopt;
}

void propagateMulStackByteMetadata(
	AbstractStackValue const& _left,
	AbstractStackValue const& _right,
	AbstractStackValue& _output
)
{
	if (auto shiftBits = _right.literalValue ? powerOfTwoBitShift(*_right.literalValue) : std::nullopt)
		shiftStackBitMetadata(_left, _output, *shiftBits, true);
	if (auto shiftBits = _left.literalValue ? powerOfTwoBitShift(*_left.literalValue) : std::nullopt)
		shiftStackBitMetadata(_right, _output, *shiftBits, true);
}

void propagateDivStackByteMetadata(
	AbstractStackValue const& _numerator,
	AbstractStackValue const& _denominator,
	AbstractStackValue& _output
)
{
	if (auto shiftBits = _denominator.literalValue ? powerOfTwoBitShift(*_denominator.literalValue) : std::nullopt)
		shiftStackBitMetadata(_numerator, _output, *shiftBits, false);
}

void propagateSignedDivStackByteMetadata(
	AbstractStackValue const& _numerator,
	AbstractStackValue const& _denominator,
	AbstractStackValue& _output
)
{
	if (!bytePositionKnownZero(_numerator, 0))
		return;
	propagateDivStackByteMetadata(_numerator, _denominator, _output);
}

void propagateModStackByteMetadata(
	AbstractStackValue const& _value,
	u512 const& _modulus,
	AbstractStackValue& _output
)
{
	std::optional<size_t> keptBits = powerOfTwoBitShift(_modulus);
	if (!keptBits)
		return;
	auto keptMaskAtPosition = [&](size_t _position) -> uint8_t
	{
		size_t const byteLowBit = (VMWordBytes - 1 - _position) * 8;
		if (*keptBits <= byteLowBit)
			return 0;
		if (*keptBits >= byteLowBit + 8)
			return 0xff;
		return static_cast<uint8_t>((1u << (*keptBits - byteLowBit)) - 1);
	};
	auto preserveKeptBytes = [&](AddressByteMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [position, byteIndex]: _inputBytes)
			if (keptMaskAtPosition(position) == 0xff)
			{
				if (_currentAddress)
					setCurrentAddressExactByte(_output, position, byteIndex);
				else
					setComplementedCurrentAddressExactByte(_output, position, byteIndex);
			}
	};
	auto preserveKeptMaskedBytes = [&](AddressByteMaskMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [key, mask]: _inputBytes)
		{
			uint8_t const keptMask = static_cast<uint8_t>(mask & keptMaskAtPosition(key.first));
			if (keptMask != 0)
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, key.first, key.second, keptMask);
				else
					setComplementedCurrentAddressByteMask(_output, key.first, key.second, keptMask);
			}
		}
	};
	preserveKeptBytes(currentAddressStackBytes(_value), true);
	preserveKeptBytes(complementedCurrentAddressStackBytes(_value), false);
	preserveKeptMaskedBytes(currentAddressMaskedStackBytes(_value), true);
	preserveKeptMaskedBytes(complementedCurrentAddressMaskedStackBytes(_value), false);
	for (size_t position = 0; position < VMWordBytes; ++position)
	{
		uint8_t const keptMask = keptMaskAtPosition(position);
		setPossibleOneBitMask(
			_output,
			position,
			static_cast<uint8_t>(possibleOneBitMaskAtPosition(_value, position) & keptMask)
		);
		if (keptMask == 0xff && bytePositionKnownZero(_value, position))
			markZeroBytePosition(_output, position);
		if (keptMask == 0xff && bytePositionKnownFF(_value, position))
			markFFBytePosition(_output, position);
	}
}

void propagateSignedModStackByteMetadata(
	AbstractStackValue const& _value,
	u512 const& _modulus,
	AbstractStackValue& _output
)
{
	std::optional<size_t> keptBits = powerOfTwoBitShift(_modulus);
	if (!keptBits)
		return;
	if (bytePositionKnownZero(_value, 0))
	{
		propagateModStackByteMetadata(_value, _modulus, _output);
		return;
	}
	auto keptMaskAtPosition = [&](size_t _position) -> uint8_t
	{
		size_t const byteLowBit = (VMWordBytes - 1 - _position) * 8;
		if (*keptBits <= byteLowBit)
			return 0;
		if (*keptBits >= byteLowBit + 8)
			return 0xff;
		return static_cast<uint8_t>((1u << (*keptBits - byteLowBit)) - 1);
	};
	auto preserveKeptMaskedBytes = [&](AddressByteMaskMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [key, mask]: _inputBytes)
		{
			uint8_t const keptMask = static_cast<uint8_t>(mask & keptMaskAtPosition(key.first));
			if (keptMask != 0)
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, key.first, key.second, keptMask);
				else
					setComplementedCurrentAddressByteMask(_output, key.first, key.second, keptMask);
			}
		}
	};
	preserveKeptMaskedBytes(currentAddressMaskedStackBytes(_value), true);
	preserveKeptMaskedBytes(complementedCurrentAddressMaskedStackBytes(_value), false);
}

void propagateAndStackByteMetadata(
	AbstractStackValue const& _input,
	u512 const& _mask,
	AbstractStackValue& _output
)
{
	AddressByteMap const currentBytes = currentAddressStackBytes(_input);
	AddressByteMap const complementedBytes = complementedCurrentAddressStackBytes(_input);
	AddressByteMaskMap const currentMaskedBytes = currentAddressMaskedStackBytes(_input);
	AddressByteMaskMap const complementedMaskedBytes = complementedCurrentAddressMaskedStackBytes(_input);
	for (size_t position: zeroBytePositions(_input))
		markZeroBytePosition(_output, position);
	for (size_t position = 0; position < VMWordBytes; ++position)
	{
		uint8_t const maskByte = literalByteAtPosition(_mask, position);
		setPossibleOneBitMask(
			_output,
			position,
			static_cast<uint8_t>(possibleOneBitMaskAtPosition(_input, position) & maskByte)
		);
		for (auto const& [key, currentMask]: currentMaskedBytes)
			if (key.first == position)
				setCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(currentMask & maskByte));
		for (auto const& [key, complementedMask]: complementedMaskedBytes)
			if (key.first == position)
				setComplementedCurrentAddressByteMask(
					_output,
					position,
					key.second,
					static_cast<uint8_t>(complementedMask & maskByte)
				);
		if (maskByte == 0)
			markZeroBytePosition(_output, position);
		else if (maskByte == 0xff)
		{
			if (auto byte = currentBytes.find(position); byte != currentBytes.end())
				setCurrentAddressExactByte(_output, position, byte->second);
			if (
				auto byte = complementedBytes.find(position);
				byte != complementedBytes.end()
			)
				setComplementedCurrentAddressExactByte(_output, position, byte->second);
			if (bytePositionKnownFF(_input, position))
				markFFBytePosition(_output, position);
		}
	}
}

void propagateXorStackByteMetadata(
	AbstractStackValue const& _input,
	u512 const& _literal,
	AbstractStackValue& _output
)
{
	AddressByteMaskMap const currentMaskedBytes = currentAddressMaskedStackBytes(_input);
	AddressByteMaskMap const complementedMaskedBytes = complementedCurrentAddressMaskedStackBytes(_input);
	for (size_t position = 0; position < VMWordBytes; ++position)
	{
		uint8_t const literalByte = literalByteAtPosition(_literal, position);
		uint8_t const preservedMask = static_cast<uint8_t>(~literalByte);
		uint8_t const possibleMask =
			bytePositionKnownFF(_input, position) ?
			static_cast<uint8_t>(~literalByte) :
			(
				bytePositionKnownZero(_input, position) ?
				literalByte :
				static_cast<uint8_t>((possibleOneBitMaskAtPosition(_input, position) & preservedMask) | literalByte)
			);
		setPossibleOneBitMask(_output, position, possibleMask);

		if (bytePositionKnownZero(_input, position))
		{
			if (literalByte == 0)
				markZeroBytePosition(_output, position);
			else if (literalByte == 0xff)
				markFFBytePosition(_output, position);
		}
		if (bytePositionKnownFF(_input, position))
		{
			if (literalByte == 0)
				markFFBytePosition(_output, position);
			else if (literalByte == 0xff)
				markZeroBytePosition(_output, position);
		}
		for (auto const& [key, mask]: currentMaskedBytes)
			if (key.first == position)
			{
				setCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & preservedMask));
				setComplementedCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & literalByte));
			}
		for (auto const& [key, mask]: complementedMaskedBytes)
			if (key.first == position)
			{
				setComplementedCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & preservedMask));
				setCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & literalByte));
			}
	}
}

void propagateByteExtractionStackByteMetadata(
	AbstractStackValue const& _input,
	std::optional<size_t> _byteIndex,
	AbstractStackValue& _output
)
{
	markByteExtractedValue(_output);
	if (!_byteIndex || *_byteIndex >= VMWordBytes)
		return;

	if (bytePositionKnownZero(_input, *_byteIndex))
		markZeroBytePosition(_output, c_lowStackBytePosition);
	if (bytePositionKnownFF(_input, *_byteIndex))
		markFFBytePosition(_output, c_lowStackBytePosition);
	setPossibleOneBitMask(_output, c_lowStackBytePosition, possibleOneBitMaskAtPosition(_input, *_byteIndex));
	for (auto const& [key, mask]: currentAddressMaskedStackBytes(_input))
		if (key.first == *_byteIndex)
			setCurrentAddressByteMask(_output, c_lowStackBytePosition, key.second, mask);
	for (auto const& [key, mask]: complementedCurrentAddressMaskedStackBytes(_input))
		if (key.first == *_byteIndex)
			setComplementedCurrentAddressByteMask(_output, c_lowStackBytePosition, key.second, mask);
}

void propagateSignExtendStackByteMetadata(
	AbstractStackValue const& _input,
	std::optional<size_t> _byteIndex,
	AbstractStackValue& _output
)
{
	if (!_byteIndex)
		return;
	if (*_byteIndex >= VMWordBytes)
	{
		copyStackByteMetadata(_input, _output);
		return;
	}

	size_t const firstKeptPosition = VMWordBytes - 1 - *_byteIndex;
	auto preserveKeptBytes = [&](AddressByteMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [position, byteIndex]: _inputBytes)
			if (position >= firstKeptPosition)
			{
				if (_currentAddress)
					setCurrentAddressExactByte(_output, position, byteIndex);
				else
					setComplementedCurrentAddressExactByte(_output, position, byteIndex);
			}
	};
	auto preserveKeptMaskedBytes = [&](AddressByteMaskMap const& _inputBytes, bool _currentAddress)
	{
		for (auto const& [key, mask]: _inputBytes)
			if (key.first >= firstKeptPosition)
			{
				if (_currentAddress)
					setCurrentAddressByteMask(_output, key.first, key.second, mask);
				else
					setComplementedCurrentAddressByteMask(_output, key.first, key.second, mask);
			}
	};
	preserveKeptBytes(currentAddressStackBytes(_input), true);
	preserveKeptBytes(complementedCurrentAddressStackBytes(_input), false);
	preserveKeptMaskedBytes(currentAddressMaskedStackBytes(_input), true);
	preserveKeptMaskedBytes(complementedCurrentAddressMaskedStackBytes(_input), false);
	for (size_t position = firstKeptPosition; position < VMWordBytes; ++position)
	{
		if (bytePositionKnownZero(_input, position))
			markZeroBytePosition(_output, position);
		if (bytePositionKnownFF(_input, position))
			markFFBytePosition(_output, position);
		setPossibleOneBitMask(_output, position, possibleOneBitMaskAtPosition(_input, position));
	}
	if ((possibleOneBitMaskAtPosition(_input, firstKeptPosition) & 0x80) == 0)
		for (size_t position = 0; position < firstKeptPosition; ++position)
			markZeroBytePosition(_output, position);
	else if (bytePositionKnownFF(_input, firstKeptPosition))
		for (size_t position = 0; position < firstKeptPosition; ++position)
			markFFBytePosition(_output, position);
}

void propagateLiteralSubStackByteMetadata(
	u512 const& _minuend,
	AbstractStackValue const& _subtrahend,
	AbstractStackValue& _output
)
{
	AddressByteMaskMap const currentMaskedBytes = currentAddressMaskedStackBytes(_subtrahend);
	AddressByteMaskMap const complementedMaskedBytes = complementedCurrentAddressMaskedStackBytes(_subtrahend);
	bool borrowMayReachPosition = false;
	for (size_t reversePosition = VMWordBytes; reversePosition > 0; --reversePosition)
	{
		size_t const position = reversePosition - 1;
		uint8_t const literalByte = literalByteAtPosition(_minuend, position);
		uint8_t const subtrahendPossibleMask = possibleOneBitMaskAtPosition(_subtrahend, position);
		bool const bytePreservedWithoutBorrow =
			(subtrahendPossibleMask & static_cast<uint8_t>(~literalByte)) == 0;
		if (!borrowMayReachPosition && bytePreservedWithoutBorrow)
		{
			setPossibleOneBitMask(_output, position, literalByte);
			if (literalByte == 0)
				markZeroBytePosition(_output, position);
			else if (literalByte == 0xff && bytePositionKnownZero(_subtrahend, position))
				markFFBytePosition(_output, position);
			for (auto const& [key, mask]: currentMaskedBytes)
				if (key.first == position)
					setComplementedCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & literalByte));
			for (auto const& [key, mask]: complementedMaskedBytes)
				if (key.first == position)
					setCurrentAddressByteMask(_output, position, key.second, static_cast<uint8_t>(mask & literalByte));
		}

		borrowMayReachPosition =
			borrowMayReachPosition ?
			subtrahendPossibleMask >= literalByte :
			subtrahendPossibleMask > literalByte;
	}
}

bool addressByteMapContainsWord(AddressByteMap const& _bytes, size_t _baseOffset)
{
	if (_baseOffset > std::numeric_limits<size_t>::max() - (VMWordBytes - 1))
		return false;
	for (size_t byteIndex = 0; byteIndex < VMWordBytes; ++byteIndex)
	{
		auto byte = _bytes.find(_baseOffset + byteIndex);
		if (byte == _bytes.end() || byte->second != byteIndex)
			return false;
	}
	return true;
}

bool addressByteMapContainsAnyWord(AddressByteMap const& _bytes)
{
	if (_bytes.size() < VMWordBytes)
		return false;
	for (auto const& [offset, byteIndex]: _bytes)
		if (offset >= byteIndex && addressByteMapContainsWord(_bytes, offset - byteIndex))
			return true;
	return false;
}

bool addressByteMapMayLoadWord(AddressByteMap const& _bytes, AbstractStackValue const& _offset)
{
	if (_bytes.empty())
		return false;
	if (std::optional<size_t> literalOffset = literalSizeT(_offset))
		return addressByteMapContainsWord(_bytes, *literalOffset);
	return addressByteMapContainsAnyWord(_bytes);
}

struct AddressByteStore
{
	std::optional<size_t> offset;
	size_t byteIndex = 0;
};

struct AddressWordStore
{
	std::optional<size_t> offset;
};

std::optional<size_t> callOutputLengthParameter(Instruction _instruction);

// Returns true when the byte cannot be represented precisely and callers need
// to keep the broad exact-address memory fallback.
bool recordAddressByteStore(AddressByteMap& _bytes, AddressByteStore const& _store)
{
	if (!_store.offset)
		return true;
	_bytes[*_store.offset] = _store.byteIndex;
	if (_bytes.size() > c_maxTrackedAddressByteStores)
	{
		_bytes.clear();
		return true;
	}
	return false;
}

bool recordAddressByteStores(AddressByteMap& _bytes, std::vector<AddressByteStore> const& _stores)
{
	for (AddressByteStore const& store: _stores)
		if (recordAddressByteStore(_bytes, store))
		{
			_bytes.clear();
			return true;
		}
	return false;
}

bool recordAddressWordStore(AddressByteMap& _bytes, AddressWordStore const& _store)
{
	if (!_store.offset || *_store.offset > std::numeric_limits<size_t>::max() - (VMWordBytes - 1))
		return true;
	for (size_t byteIndex = 0; byteIndex < VMWordBytes; ++byteIndex)
		_bytes[*_store.offset + byteIndex] = byteIndex;
	if (_bytes.size() > c_maxTrackedAddressByteStores)
	{
		_bytes.clear();
		return true;
	}
	return false;
}

void clearAddressByteMaps(AddressByteMap& _currentAddressBytes, AddressByteMap& _complementedCurrentAddressBytes)
{
	_currentAddressBytes.clear();
	_complementedCurrentAddressBytes.clear();
}

struct AddressByteMapInvalidation
{
	bool currentAddressPrecisionLost = false;
	bool complementedCurrentAddressPrecisionLost = false;
};

void eraseAddressByteRange(AddressByteMap& _bytes, size_t _offset, size_t _size)
{
	if (_size == 0)
		return;
	size_t const maxEnd = std::numeric_limits<size_t>::max();
	bool const endOverflows = _size > maxEnd - _offset;
	size_t const end = endOverflows ? maxEnd : _offset + _size;
	auto it = _bytes.lower_bound(_offset);
	while (it != _bytes.end() && (endOverflows || it->first < end))
		it = _bytes.erase(it);
}

void eraseAddressByteRange(
	AddressByteMap& _currentAddressBytes,
	AddressByteMap& _complementedCurrentAddressBytes,
	size_t _offset,
	size_t _size
)
{
	eraseAddressByteRange(_currentAddressBytes, _offset, _size);
	eraseAddressByteRange(_complementedCurrentAddressBytes, _offset, _size);
}

AddressByteMapInvalidation invalidateAddressByteMapsForWrite(
	AddressByteMap& _currentAddressBytes,
	AddressByteMap& _complementedCurrentAddressBytes,
	std::optional<size_t> _offset,
	std::optional<size_t> _size
)
{
	if (_size && *_size == 0)
		return {};
	if (!_offset || !_size)
	{
		AddressByteMapInvalidation invalidation{
			!_currentAddressBytes.empty(),
			!_complementedCurrentAddressBytes.empty()
		};
		clearAddressByteMaps(_currentAddressBytes, _complementedCurrentAddressBytes);
		return invalidation;
	}
	eraseAddressByteRange(_currentAddressBytes, _complementedCurrentAddressBytes, *_offset, *_size);
	return {};
}

template <class StackArgument>
AddressByteMapInvalidation invalidateAddressByteMapsForMemoryWrite(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	AddressByteMap& _currentAddressBytes,
	AddressByteMap& _complementedCurrentAddressBytes
)
{
	if (_instruction == Instruction::MSTORE && _arguments >= 2)
	{
		return invalidateAddressByteMapsForWrite(
			_currentAddressBytes,
			_complementedCurrentAddressBytes,
			literalSizeT(_stackArgument(0)),
			VMWordBytes
		);
	}
	if (_instruction == Instruction::MSTORE8 && _arguments >= 2)
	{
		return invalidateAddressByteMapsForWrite(
			_currentAddressBytes,
			_complementedCurrentAddressBytes,
			literalSizeT(_stackArgument(0)),
			1
		);
	}
	if (
		(_instruction == Instruction::CALLDATACOPY ||
		_instruction == Instruction::CODECOPY ||
		_instruction == Instruction::RETURNDATACOPY) &&
		_arguments >= 3
	)
	{
		return invalidateAddressByteMapsForWrite(
			_currentAddressBytes,
			_complementedCurrentAddressBytes,
			literalSizeT(_stackArgument(0)),
			literalSizeT(_stackArgument(2))
		);
	}
	if (_instruction == Instruction::EXTCODECOPY && _arguments >= 4)
	{
		return invalidateAddressByteMapsForWrite(
			_currentAddressBytes,
			_complementedCurrentAddressBytes,
			literalSizeT(_stackArgument(1)),
			literalSizeT(_stackArgument(3))
		);
	}
	if (
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		_instruction == Instruction::STATICCALL
	)
	{
		std::optional<size_t> outputLengthParameter = callOutputLengthParameter(_instruction);
		if (!outputLengthParameter || *outputLengthParameter >= _arguments)
		{
			AddressByteMapInvalidation invalidation{
				!_currentAddressBytes.empty(),
				!_complementedCurrentAddressBytes.empty()
			};
			clearAddressByteMaps(_currentAddressBytes, _complementedCurrentAddressBytes);
			return invalidation;
		}
		else if (valueMayBeNonZero(_stackArgument(*outputLengthParameter)))
		{
			AddressByteMapInvalidation invalidation{
				!_currentAddressBytes.empty(),
				!_complementedCurrentAddressBytes.empty()
			};
			clearAddressByteMaps(_currentAddressBytes, _complementedCurrentAddressBytes);
			return invalidation;
		}
	}
	return {};
}

template <class StackArgument, class ByteIndex, class ExactValuePredicate>
std::optional<AddressByteStore> memoryStoreAddressByteStore(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ByteIndex AbstractStackValue::*_byteIndex,
	ExactValuePredicate _exactValuePredicate
)
{
	if (!isMemoryStoreInstruction(_instruction) || _arguments < 2)
		return std::nullopt;
	std::optional<size_t> byteIndex = _stackArgument(1).*_byteIndex;
	if (!byteIndex && _instruction == Instruction::MSTORE8 && _exactValuePredicate(_stackArgument(1)))
		byteIndex = VMWordBytes - 1;
	if (!byteIndex)
		return std::nullopt;
	std::optional<size_t> offset = literalSizeT(_stackArgument(0));
	if (_instruction == Instruction::MSTORE && offset)
	{
		if (*offset > std::numeric_limits<size_t>::max() - (VMWordBytes - 1))
			offset.reset();
		else
			*offset += VMWordBytes - 1;
	}
	return AddressByteStore{offset, *byteIndex};
}

template <class StackArgument, class StackBytes>
std::vector<AddressByteStore> memoryStoreAddressStackByteStores(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	StackBytes _stackBytes
)
{
	if (!isMemoryStoreInstruction(_instruction) || _arguments < 2)
		return {};
	AddressByteMap const stackBytes = _stackBytes(_stackArgument(1));
	if (stackBytes.empty())
		return {};

	std::optional<size_t> offset = literalSizeT(_stackArgument(0));
	if (_instruction == Instruction::MSTORE8)
	{
		if (auto byte = stackBytes.find(c_lowStackBytePosition); byte != stackBytes.end())
			return {AddressByteStore{offset, byte->second}};
		return {};
	}

	if (!offset)
		return {AddressByteStore{std::nullopt, 0}};

	std::vector<AddressByteStore> stores;
	for (auto const& [position, byteIndex]: stackBytes)
	{
		if (*offset > std::numeric_limits<size_t>::max() - position)
			return {AddressByteStore{std::nullopt, 0}};
		stores.push_back(AddressByteStore{*offset + position, byteIndex});
	}
	return stores;
}

template <class StackArgument, class ValuePredicate>
std::optional<AddressWordStore> memoryStoreAddressWordStore(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValuePredicate _valuePredicate
)
{
	if (_instruction != Instruction::MSTORE || _arguments < 2 || !_valuePredicate(_stackArgument(1)))
		return std::nullopt;
	return AddressWordStore{literalSizeT(_stackArgument(0))};
}

template <class StackArgument, class ValuePredicate>
bool identityOperationMayPreserveValue(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValuePredicate _valuePredicate
)
{
	if (_arguments < 2)
		return false;

	auto valueWithLiteral = [&](u512 const& _literal)
	{
		return
			(_valuePredicate(_stackArgument(0)) && valueIsLiteral(_stackArgument(1), _literal)) ||
			(_valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), _literal));
	};

	switch (_instruction)
	{
	case Instruction::ADD:
	case Instruction::OR:
	case Instruction::XOR:
		return valueWithLiteral(0);
	case Instruction::MUL:
		return valueWithLiteral(1);
	case Instruction::AND:
		return valueWithLiteral(~u512(0));
	case Instruction::SUB:
		return _valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), 0);
	case Instruction::DIV:
	case Instruction::SDIV:
		return _valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), 1);
	case Instruction::EXP:
		return _valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), 1);
	case Instruction::SIGNEXTEND:
		return _valuePredicate(_stackArgument(1)) && valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1);
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		return _valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), 0);
	default:
		return false;
	}
}

template <class StackArgument, class ValuePredicate>
bool operationMayComplementValue(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValuePredicate _valuePredicate
)
{
	if (_instruction == Instruction::NOT)
		return _arguments >= 1 && _valuePredicate(_stackArgument(0));
	if (_instruction != Instruction::XOR || _arguments < 2)
		return false;
	return
		(_valuePredicate(_stackArgument(0)) && valueIsLiteral(_stackArgument(1), ~u512(0))) ||
		(_valuePredicate(_stackArgument(1)) && valueIsLiteral(_stackArgument(0), ~u512(0)));
}

template <class StackArgument, class ValueConsumer>
void visitIdentityPreservedInputs(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	ValueConsumer _consume
)
{
	if (_arguments < 2)
		return;

	auto preserveEitherWithLiteral = [&](u512 const& _literal)
	{
		if (valueIsLiteral(_stackArgument(1), _literal))
			_consume(_stackArgument(0));
		if (valueIsLiteral(_stackArgument(0), _literal))
			_consume(_stackArgument(1));
	};
	auto preserveSecondWithTopLiteral = [&](u512 const& _literal)
	{
		if (valueIsLiteral(_stackArgument(0), _literal))
			_consume(_stackArgument(1));
	};

	switch (_instruction)
	{
	case Instruction::ADD:
	case Instruction::OR:
	case Instruction::XOR:
		preserveEitherWithLiteral(0);
		break;
	case Instruction::MUL:
		preserveEitherWithLiteral(1);
		break;
	case Instruction::AND:
		preserveEitherWithLiteral(~u512(0));
		break;
	case Instruction::SUB:
		preserveSecondWithTopLiteral(0);
		break;
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::EXP:
		preserveSecondWithTopLiteral(1);
		break;
	case Instruction::SIGNEXTEND:
		if (valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1))
			_consume(_stackArgument(1));
		break;
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		if (valueIsLiteral(_stackArgument(0), 0))
			_consume(_stackArgument(1));
		break;
	default:
		break;
	}
}

template <class StackArgument>
void propagateByteMarkerPreservedMetadata(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	AbstractStackValue& _output
)
{
	if (_arguments < 1)
		return;

	auto preserveByteMarkers = [&](AbstractStackValue const& _input)
	{
		if (_input.currentAddressByteIndex)
			setCurrentAddressLowByte(_output, *_input.currentAddressByteIndex);
		if (_input.complementedCurrentAddressByteIndex)
			setComplementedCurrentAddressLowByte(_output, *_input.complementedCurrentAddressByteIndex);
		if (valueMayBeExactCurrentAddress(_input))
			setCurrentAddressLowByte(_output, c_lowStackBytePosition);
		if (valueMayBeExactComplementedCurrentAddress(_input))
			setComplementedCurrentAddressLowByte(_output, c_lowStackBytePosition);
	};
	auto complementByteMarkers = [&](AbstractStackValue const& _input)
	{
		if (_input.currentAddressByteIndex)
			setComplementedCurrentAddressLowByte(_output, *_input.currentAddressByteIndex);
		if (_input.complementedCurrentAddressByteIndex)
			setCurrentAddressLowByte(_output, *_input.complementedCurrentAddressByteIndex);
		if (valueMayBeExactCurrentAddress(_input))
			setComplementedCurrentAddressLowByte(_output, c_lowStackBytePosition);
		if (valueMayBeExactComplementedCurrentAddress(_input))
			setCurrentAddressLowByte(_output, c_lowStackBytePosition);
	};
	if (_instruction == Instruction::NOT)
	{
		if (_arguments >= 1)
		{
			complementByteMarkers(_stackArgument(0));
			complementStackByteMetadata(_stackArgument(0), _output);
		}
		return;
	}
	if (_arguments < 2)
		return;

	auto preserveEitherWithLowByteLiteral = [&](u512 const& _literal)
	{
		if (valueLowByteIsLiteral(_stackArgument(1), _literal))
			preserveByteMarkers(_stackArgument(0));
		if (valueLowByteIsLiteral(_stackArgument(0), _literal))
			preserveByteMarkers(_stackArgument(1));
	};
	auto preserveSecondWithTopLowByteLiteral = [&](u512 const& _literal)
	{
		if (valueLowByteIsLiteral(_stackArgument(0), _literal))
			preserveByteMarkers(_stackArgument(1));
	};
	auto markByteSizedIfInputByteSized = [&](AbstractStackValue const& _input)
	{
		if (valueKnownByteSized(_input))
			markByteExtractedValue(_output);
	};

	switch (_instruction)
	{
	case Instruction::ADD:
	case Instruction::OR:
		preserveEitherWithLowByteLiteral(u512(0));
		break;
	case Instruction::XOR:
		preserveEitherWithLowByteLiteral(u512(0));
		if (valueLowByteIsLiteral(_stackArgument(1), u512(0xff)))
			complementByteMarkers(_stackArgument(0));
		if (valueLowByteIsLiteral(_stackArgument(0), u512(0xff)))
			complementByteMarkers(_stackArgument(1));
		if (valueIsLiteral(_stackArgument(1), ~u512(0)))
			complementStackByteMetadata(_stackArgument(0), _output);
		if (valueIsLiteral(_stackArgument(0), ~u512(0)))
			complementStackByteMetadata(_stackArgument(1), _output);
		break;
	case Instruction::MUL:
		preserveEitherWithLowByteLiteral(u512(1));
		break;
	case Instruction::AND:
		if (valueIsBytePreservingAndMask(_stackArgument(1)))
			preserveByteMarkers(_stackArgument(0));
		if (valueIsBytePreservingAndMask(_stackArgument(0)))
			preserveByteMarkers(_stackArgument(1));
		break;
	case Instruction::SUB:
		preserveSecondWithTopLowByteLiteral(u512(0));
		if (valueLowByteIsLiteral(_stackArgument(1), u512(0xff)))
		{
			complementByteMarkers(_stackArgument(0));
			if (valueIsLiteral(_stackArgument(1), u512(0xff)))
				markByteSizedIfInputByteSized(_stackArgument(0));
		}
		break;
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::EXP:
		if (valueIsLiteral(_stackArgument(0), u512(1)))
			preserveByteMarkers(_stackArgument(1));
		break;
	case Instruction::MOD:
	case Instruction::SMOD:
		if (valueIsNonZeroMultipleOfByteRange(_stackArgument(0)))
		{
			preserveByteMarkers(_stackArgument(1));
			markByteSizedIfInputByteSized(_stackArgument(1));
		}
		break;
	case Instruction::ADDMOD:
		if (_arguments >= 3 && valueIsNonZeroMultipleOfByteRange(_stackArgument(0)))
		{
			if (valueLowByteIsLiteral(_stackArgument(1), u512(0)))
			{
				preserveByteMarkers(_stackArgument(2));
				if (valueIsLiteral(_stackArgument(1), u512(0)))
					markByteSizedIfInputByteSized(_stackArgument(2));
			}
			if (valueLowByteIsLiteral(_stackArgument(2), u512(0)))
			{
				preserveByteMarkers(_stackArgument(1));
				if (valueIsLiteral(_stackArgument(2), u512(0)))
					markByteSizedIfInputByteSized(_stackArgument(1));
			}
		}
		break;
	case Instruction::MULMOD:
		if (_arguments >= 3 && valueIsNonZeroMultipleOfByteRange(_stackArgument(0)))
		{
			if (valueLowByteIsLiteral(_stackArgument(1), u512(1)))
			{
				preserveByteMarkers(_stackArgument(2));
				if (valueIsLiteral(_stackArgument(1), u512(1)))
					markByteSizedIfInputByteSized(_stackArgument(2));
			}
			if (valueLowByteIsLiteral(_stackArgument(2), u512(1)))
			{
				preserveByteMarkers(_stackArgument(1));
				if (valueIsLiteral(_stackArgument(2), u512(1)))
					markByteSizedIfInputByteSized(_stackArgument(1));
			}
		}
		break;
	case Instruction::BYTE:
		if (valueIsLiteral(_stackArgument(0), u512(VMWordBytes - 1)))
			preserveByteMarkers(_stackArgument(1));
		break;
	case Instruction::SIGNEXTEND:
		preserveByteMarkers(_stackArgument(1));
		break;
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		if (valueIsLiteral(_stackArgument(0), u512(0)))
			preserveByteMarkers(_stackArgument(1));
		break;
	default:
		break;
	}
}

template <class StackArgument>
void propagateIdentityPreservedMetadata(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	AbstractStackValue& _output
)
{
	visitIdentityPreservedInputs(
		_instruction,
		_arguments,
			_stackArgument,
			[&](AbstractStackValue const& _input)
			{
				if (_input.mayBeExternalJumpTarget)
					_output.mayBeExternalJumpTarget = true;
				if (_input.mustBeExternalJumpTarget)
					_output.mustBeExternalJumpTarget = true;
				if (_input.mayBeUnknownLocalTag)
					_output.mayBeUnknownLocalTag = true;
				if (_input.mayBeUnknownCallerStackValue)
					_output.mayBeUnknownCallerStackValue = true;
				if (_input.currentAddressByteIndex)
					setCurrentAddressLowByte(_output, *_input.currentAddressByteIndex);
				if (_input.complementedCurrentAddressByteIndex)
					setComplementedCurrentAddressLowByte(_output, *_input.complementedCurrentAddressByteIndex);
				_output.possibleCurrentAddressOffsets.insert(
					_input.possibleCurrentAddressOffsets.begin(),
					_input.possibleCurrentAddressOffsets.end()
				);
				copyStackByteMetadata(_input, _output);
				_output.foreignTags.insert(_input.foreignTags.begin(), _input.foreignTags.end());
				_output.localTags.insert(_input.localTags.begin(), _input.localTags.end());
			}
	);
	if (_instruction == Instruction::AND && _arguments >= 2)
	{
		auto preserveLocalTagMetadata = [&](AbstractStackValue const& _input)
		{
			if (_input.mayBeUnknownLocalTag)
				_output.mayBeUnknownLocalTag = true;
			_output.localTags.insert(_input.localTags.begin(), _input.localTags.end());
		};
		if (valueIsLocalTagPreservingAndMask(_stackArgument(1)))
			preserveLocalTagMetadata(_stackArgument(0));
		if (valueIsLocalTagPreservingAndMask(_stackArgument(0)))
			preserveLocalTagMetadata(_stackArgument(1));
	}
	if (_instruction == Instruction::AND && _arguments >= 2)
	{
		if (auto mask = _stackArgument(1).literalValue)
			propagateAndStackByteMetadata(_stackArgument(0), *mask, _output);
		if (auto mask = _stackArgument(0).literalValue)
			propagateAndStackByteMetadata(_stackArgument(1), *mask, _output);
	}
	if (_instruction == Instruction::SUB && _arguments >= 2)
		if (auto minuend = _stackArgument(1).literalValue)
			propagateLiteralSubStackByteMetadata(*minuend, _stackArgument(0), _output);
	if (_instruction == Instruction::ADD && _arguments >= 2)
		propagateAddStackByteMetadata(_stackArgument(0), _stackArgument(1), _output);
	if (_instruction == Instruction::ADDMOD && _arguments >= 3)
		if (auto modulus = _stackArgument(0).literalValue)
		{
			AbstractStackValue added;
			propagateAddStackByteMetadata(_stackArgument(1), _stackArgument(2), added);
			propagateModStackByteMetadata(added, *modulus, _output);
		}
	if (_instruction == Instruction::MUL && _arguments >= 2)
		propagateMulStackByteMetadata(_stackArgument(0), _stackArgument(1), _output);
	if (_instruction == Instruction::MULMOD && _arguments >= 3)
		if (auto modulus = _stackArgument(0).literalValue)
		{
			AbstractStackValue multiplied;
			propagateMulStackByteMetadata(_stackArgument(1), _stackArgument(2), multiplied);
			propagateModStackByteMetadata(multiplied, *modulus, _output);
		}
	if (_instruction == Instruction::DIV && _arguments >= 2)
		propagateDivStackByteMetadata(_stackArgument(1), _stackArgument(0), _output);
	if (_instruction == Instruction::SDIV && _arguments >= 2)
		propagateSignedDivStackByteMetadata(_stackArgument(1), _stackArgument(0), _output);
	if (_instruction == Instruction::MOD && _arguments >= 2)
		if (auto modulus = _stackArgument(0).literalValue)
			propagateModStackByteMetadata(_stackArgument(1), *modulus, _output);
	if (_instruction == Instruction::SMOD && _arguments >= 2)
		if (auto modulus = _stackArgument(0).literalValue)
			propagateSignedModStackByteMetadata(_stackArgument(1), *modulus, _output);
	if (_instruction == Instruction::OR && _arguments >= 2)
		propagateOrStackByteMetadata(_stackArgument(0), _stackArgument(1), _output);
	if (_instruction == Instruction::XOR && _arguments >= 2)
	{
		propagateXorStackByteMetadata(_stackArgument(0), _stackArgument(1), _output);
		if (auto literal = _stackArgument(1).literalValue)
			propagateXorStackByteMetadata(_stackArgument(0), *literal, _output);
		if (auto literal = _stackArgument(0).literalValue)
			propagateXorStackByteMetadata(_stackArgument(1), *literal, _output);
	}
	if (_instruction == Instruction::BYTE && _arguments >= 2)
		propagateByteExtractionStackByteMetadata(
			_stackArgument(1),
			literalSizeT(_stackArgument(0)),
			_output
		);
	if (_instruction == Instruction::SIGNEXTEND && _arguments >= 2)
		propagateSignExtendStackByteMetadata(
			_stackArgument(1),
			literalSizeT(_stackArgument(0)),
			_output
		);
	if (
		(_instruction == Instruction::SHL || _instruction == Instruction::SHR || _instruction == Instruction::SAR) &&
		_arguments >= 2
	)
		if (std::optional<size_t> shift = literalSizeT(_stackArgument(0)); shift && *shift < VMWordBits)
			if (_instruction != Instruction::SAR || bytePositionKnownZero(_stackArgument(1), 0))
				shiftStackBitMetadata(
					_stackArgument(1),
					_output,
					*shift,
					_instruction == Instruction::SHL
				);
	if ((_instruction == Instruction::SHL || _instruction == Instruction::SHR) && _arguments >= 2)
	{
		std::optional<size_t> shift = literalSizeT(_stackArgument(0));
		AbstractStackValue const& shiftedValue = _stackArgument(1);
		if (shift && _instruction == Instruction::SHL && localTagLeftShiftIsLossless(*shift))
		{
			if (shiftedValue.mayBeUnknownLocalTag)
				_output.mayBeUnknownLeftShiftedLocalTag = true;
			if (!shiftedValue.localTags.empty())
			{
				_output.localTagLeftShift = *shift;
				_output.leftShiftedLocalTags.insert(shiftedValue.localTags.begin(), shiftedValue.localTags.end());
			}
		}
		else if (
			shift &&
			_instruction == Instruction::SHR &&
			shiftedValue.localTagLeftShift &&
			*shiftedValue.localTagLeftShift == *shift
		)
		{
			if (shiftedValue.mayBeUnknownLeftShiftedLocalTag)
				_output.mayBeUnknownLocalTag = true;
			_output.localTags.insert(shiftedValue.leftShiftedLocalTags.begin(), shiftedValue.leftShiftedLocalTags.end());
		}
	}
	propagateByteMarkerPreservedMetadata(_instruction, _arguments, _stackArgument, _output);
}

template <class StackArgument>
std::optional<u512> operationCurrentAddressOffset(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (_instruction == Instruction::ADDRESS || _instruction == Instruction::CALLER)
		return u512(0);
	if (_arguments < 1)
		return std::nullopt;

	auto offsetAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).currentAddressOffset;
	};
	auto complementedOffsetAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).complementedCurrentAddressOffset;
	};
	auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).literalValue;
	};
	if (_instruction == Instruction::NOT)
	{
		if (auto complementedOffset = complementedOffsetAt(0))
			return u512(0) - *complementedOffset;
		return std::nullopt;
	}
	if (_arguments < 2)
		return std::nullopt;
	auto preserveEitherWithLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto offset = offsetAt(0); offset && valueIsLiteral(_stackArgument(1), _literal))
			return offset;
		if (auto offset = offsetAt(1); offset && valueIsLiteral(_stackArgument(0), _literal))
			return offset;
		return std::nullopt;
	};
	auto preserveSecondWithTopLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto offset = offsetAt(1); offset && valueIsLiteral(_stackArgument(0), _literal))
			return offset;
		return std::nullopt;
	};

	switch (_instruction)
	{
	case Instruction::ADD:
		if (auto offset = offsetAt(0); offset)
			if (auto literal = literalAt(1))
				return *offset + *literal;
		if (auto offset = offsetAt(1); offset)
			if (auto literal = literalAt(0))
				return *offset + *literal;
		return std::nullopt;
	case Instruction::SUB:
		if (auto offset = offsetAt(1); offset)
			if (auto literal = literalAt(0))
				return *offset - *literal;
		if (auto complementedOffset = complementedOffsetAt(0); complementedOffset)
			if (auto literal = literalAt(1))
				return *literal + 1 - *complementedOffset;
		return std::nullopt;
	case Instruction::XOR:
		if (auto complementedOffset = complementedOffsetAt(0); complementedOffset && valueIsLiteral(_stackArgument(1), ~u512(0)))
			return u512(0) - *complementedOffset;
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset && valueIsLiteral(_stackArgument(0), ~u512(0)))
			return u512(0) - *complementedOffset;
		return preserveEitherWithLiteral(0);
	case Instruction::OR:
		return preserveEitherWithLiteral(0);
	case Instruction::MUL:
		return preserveEitherWithLiteral(1);
	case Instruction::AND:
		return preserveEitherWithLiteral(~u512(0));
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::EXP:
		return preserveSecondWithTopLiteral(1);
	case Instruction::SIGNEXTEND:
		if (auto offset = offsetAt(1); offset && valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1))
			return offset;
		return std::nullopt;
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		if (auto offset = offsetAt(1); offset && valueIsLiteral(_stackArgument(0), 0))
			return offset;
		return std::nullopt;
	default:
		return std::nullopt;
	}
	}

	std::set<u512> currentAddressOffsetCandidates(AbstractStackValue const& _value)
	{
		std::set<u512> offsets = _value.possibleCurrentAddressOffsets;
		if (_value.currentAddressOffset)
			offsets.insert(*_value.currentAddressOffset);
		return offsets;
	}

	template <class StackArgument>
	std::set<u512> operationPossibleCurrentAddressOffsets(
		Instruction _instruction,
		size_t _arguments,
		StackArgument const& _stackArgument
	)
	{
		if (_instruction == Instruction::ADDRESS || _instruction == Instruction::CALLER)
			return {u512(0)};
		if (_arguments < 1)
			return {};

		auto offsetsAt = [&](size_t _parameterIndex)
		{
			return currentAddressOffsetCandidates(_stackArgument(_parameterIndex));
		};
		auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
		{
			return _stackArgument(_parameterIndex).literalValue;
		};
		auto appendOffset = [](std::set<u512>& _offsets, u512 const& _offset)
		{
			if (_offsets.size() < c_maxTrackedCurrentAddressOffsets)
				_offsets.insert(_offset);
		};
		auto preserveEitherWithLiteral = [&](u512 const& _literal)
		{
			std::set<u512> offsets;
			if (valueIsLiteral(_stackArgument(1), _literal))
				for (u512 const& offset: offsetsAt(0))
					appendOffset(offsets, offset);
			if (valueIsLiteral(_stackArgument(0), _literal))
				for (u512 const& offset: offsetsAt(1))
					appendOffset(offsets, offset);
			return offsets;
		};
		auto preserveSecondWithTopLiteral = [&](u512 const& _literal)
		{
			std::set<u512> offsets;
			if (valueIsLiteral(_stackArgument(0), _literal))
				for (u512 const& offset: offsetsAt(1))
					appendOffset(offsets, offset);
			return offsets;
		};

		if (_instruction == Instruction::NOT)
			return {};
		if (_arguments < 2)
			return {};

		switch (_instruction)
		{
		case Instruction::ADD:
		{
			std::set<u512> offsets;
			if (auto literal = literalAt(1))
				for (u512 const& offset: offsetsAt(0))
					appendOffset(offsets, offset + *literal);
			if (auto literal = literalAt(0))
				for (u512 const& offset: offsetsAt(1))
					appendOffset(offsets, offset + *literal);
			return offsets;
		}
		case Instruction::SUB:
		{
			std::set<u512> offsets;
			if (auto literal = literalAt(0))
				for (u512 const& offset: offsetsAt(1))
					appendOffset(offsets, offset - *literal);
			return offsets;
		}
		case Instruction::XOR:
		case Instruction::OR:
			return preserveEitherWithLiteral(0);
		case Instruction::MUL:
			return preserveEitherWithLiteral(1);
		case Instruction::AND:
			return preserveEitherWithLiteral(~u512(0));
		case Instruction::DIV:
		case Instruction::SDIV:
		case Instruction::EXP:
			return preserveSecondWithTopLiteral(1);
		case Instruction::SIGNEXTEND:
			if (valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1))
				return offsetsAt(1);
			return {};
		case Instruction::SHL:
		case Instruction::SHR:
		case Instruction::SAR:
			if (valueIsLiteral(_stackArgument(0), 0))
				return offsetsAt(1);
			return {};
		default:
			return {};
		}
	}

	template <class StackArgument>
	std::optional<u512> operationComplementedCurrentAddressOffset(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (_instruction == Instruction::ADDRESS || _instruction == Instruction::CALLER)
		return std::nullopt;
	if (_arguments < 1)
		return std::nullopt;

	auto offsetAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).currentAddressOffset;
	};
	auto complementedOffsetAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).complementedCurrentAddressOffset;
	};
	auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).literalValue;
	};
	if (_instruction == Instruction::NOT)
	{
		if (auto offset = offsetAt(0))
			return u512(0) - *offset;
		return std::nullopt;
	}
	if (_arguments < 2)
		return std::nullopt;

	auto preserveEitherWithLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto complementedOffset = complementedOffsetAt(0); complementedOffset && valueIsLiteral(_stackArgument(1), _literal))
			return complementedOffset;
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset && valueIsLiteral(_stackArgument(0), _literal))
			return complementedOffset;
		return std::nullopt;
	};
	auto preserveSecondWithTopLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset && valueIsLiteral(_stackArgument(0), _literal))
			return complementedOffset;
		return std::nullopt;
	};

	switch (_instruction)
	{
	case Instruction::ADD:
		if (auto complementedOffset = complementedOffsetAt(0); complementedOffset)
			if (auto literal = literalAt(1))
				return *complementedOffset + *literal;
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset)
			if (auto literal = literalAt(0))
				return *complementedOffset + *literal;
		return std::nullopt;
	case Instruction::SUB:
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset)
			if (auto literal = literalAt(0))
				return *complementedOffset - *literal;
		if (auto offset = offsetAt(0); offset)
			if (auto literal = literalAt(1))
				return *literal + 1 - *offset;
		return std::nullopt;
	case Instruction::XOR:
		if (auto offset = offsetAt(0); offset && valueIsLiteral(_stackArgument(1), ~u512(0)))
			return u512(0) - *offset;
		if (auto offset = offsetAt(1); offset && valueIsLiteral(_stackArgument(0), ~u512(0)))
			return u512(0) - *offset;
		return preserveEitherWithLiteral(0);
	case Instruction::OR:
		return preserveEitherWithLiteral(0);
	case Instruction::MUL:
		return preserveEitherWithLiteral(1);
	case Instruction::AND:
		return preserveEitherWithLiteral(~u512(0));
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::EXP:
		return preserveSecondWithTopLiteral(1);
	case Instruction::SIGNEXTEND:
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset && valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1))
			return complementedOffset;
		return std::nullopt;
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		if (auto complementedOffset = complementedOffsetAt(1); complementedOffset && valueIsLiteral(_stackArgument(0), 0))
			return complementedOffset;
		return std::nullopt;
	default:
		return std::nullopt;
	}
}

template <class StackArgument>
std::optional<u512> operationCurrentAddressXorMask(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (_instruction == Instruction::ADDRESS || _instruction == Instruction::CALLER)
		return u512(0);
	if (_arguments < 1)
		return std::nullopt;

	auto maskAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).currentAddressXorMask;
	};
	auto literalAt = [&](size_t _parameterIndex) -> std::optional<u512>
	{
		return _stackArgument(_parameterIndex).literalValue;
	};
	if (_instruction == Instruction::NOT)
	{
		if (auto mask = maskAt(0))
			return *mask ^ ~u512(0);
		return std::nullopt;
	}
	if (_arguments < 2)
		return std::nullopt;

	auto preserveEitherWithLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto mask = maskAt(0); mask && valueIsLiteral(_stackArgument(1), _literal))
			return mask;
		if (auto mask = maskAt(1); mask && valueIsLiteral(_stackArgument(0), _literal))
			return mask;
		return std::nullopt;
	};
	auto preserveSecondWithTopLiteral = [&](u512 const& _literal) -> std::optional<u512>
	{
		if (auto mask = maskAt(1); mask && valueIsLiteral(_stackArgument(0), _literal))
			return mask;
		return std::nullopt;
	};

	switch (_instruction)
	{
	case Instruction::XOR:
		if (auto mask = maskAt(0); mask)
			if (auto literal = literalAt(1))
				return *mask ^ *literal;
		if (auto mask = maskAt(1); mask)
			if (auto literal = literalAt(0))
				return *mask ^ *literal;
		return std::nullopt;
	case Instruction::ADD:
	case Instruction::OR:
		return preserveEitherWithLiteral(0);
	case Instruction::MUL:
		return preserveEitherWithLiteral(1);
	case Instruction::AND:
		return preserveEitherWithLiteral(~u512(0));
	case Instruction::SUB:
		return preserveSecondWithTopLiteral(0);
	case Instruction::DIV:
	case Instruction::SDIV:
	case Instruction::EXP:
		return preserveSecondWithTopLiteral(1);
	case Instruction::SIGNEXTEND:
		if (auto mask = maskAt(1); mask && valueIsLiteralAtLeast(_stackArgument(0), VMWordBytes - 1))
			return mask;
		return std::nullopt;
	case Instruction::SHL:
	case Instruction::SHR:
	case Instruction::SAR:
		if (auto mask = maskAt(1); mask && valueIsLiteral(_stackArgument(0), 0))
			return mask;
		return std::nullopt;
	default:
		return std::nullopt;
	}
}

template <class StackArgument>
bool operationMayProduceCurrentAddress(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	std::optional<u512> const offset = operationCurrentAddressOffset(_instruction, _arguments, _stackArgument);
	std::optional<u512> const xorMask = operationCurrentAddressXorMask(_instruction, _arguments, _stackArgument);
	auto const isCurrentAddress = [](AbstractStackValue const& _value) { return _value.mayBeCurrentAddress; };
	auto const isComplementedCurrentAddress =
		[](AbstractStackValue const& _value) { return _value.mayBeComplementedCurrentAddress; };
	return
		(offset && *offset == 0) ||
		(xorMask && *xorMask == 0) ||
		identityOperationMayPreserveValue(_instruction, _arguments, _stackArgument, isCurrentAddress) ||
		operationMayComplementValue(_instruction, _arguments, _stackArgument, isComplementedCurrentAddress);
}

template <class StackArgument>
bool operationMayProduceComplementedCurrentAddress(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	std::optional<u512> const xorMask = operationCurrentAddressXorMask(_instruction, _arguments, _stackArgument);
	std::optional<u512> const complementedOffset =
		operationComplementedCurrentAddressOffset(_instruction, _arguments, _stackArgument);
	auto const isCurrentAddress = [](AbstractStackValue const& _value) { return _value.mayBeCurrentAddress; };
	auto const isComplementedCurrentAddress =
		[](AbstractStackValue const& _value) { return _value.mayBeComplementedCurrentAddress; };
	return
		(complementedOffset && *complementedOffset == 0) ||
		(xorMask && *xorMask == ~u512(0)) ||
		identityOperationMayPreserveValue(_instruction, _arguments, _stackArgument, isComplementedCurrentAddress) ||
		operationMayComplementValue(_instruction, _arguments, _stackArgument, isCurrentAddress);
}

template <class StackArgument>
std::optional<size_t> operationCurrentAddressByteIndex(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (_instruction != Instruction::BYTE || _arguments < 2)
		return std::nullopt;
	std::optional<size_t> byteIndex = literalSizeT(_stackArgument(0));
	if (!byteIndex || *byteIndex >= VMWordBytes)
		return std::nullopt;
	AddressByteMap const stackBytes = currentAddressStackBytes(_stackArgument(1));
	if (auto byte = stackBytes.find(*byteIndex); byte != stackBytes.end())
		return byte->second;
	if (!valueMayBeExactCurrentAddress(_stackArgument(1)))
		return std::nullopt;
	return byteIndex;
}

template <class StackArgument>
std::optional<size_t> operationComplementedCurrentAddressByteIndex(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument
)
{
	if (_instruction != Instruction::BYTE || _arguments < 2)
		return std::nullopt;
	std::optional<size_t> byteIndex = literalSizeT(_stackArgument(0));
	if (!byteIndex || *byteIndex >= VMWordBytes)
		return std::nullopt;
	AddressByteMap const stackBytes = complementedCurrentAddressStackBytes(_stackArgument(1));
	if (auto byte = stackBytes.find(*byteIndex); byte != stackBytes.end())
		return byte->second;
	if (!valueMayBeExactComplementedCurrentAddress(_stackArgument(1)))
		return std::nullopt;
	return byteIndex;
}

template <class StackArgument>
AbstractStackValue operationOutputValue(
	Instruction _instruction,
	size_t _arguments,
	StackArgument const& _stackArgument,
	bool _mayBeForeignTag,
	bool _mayBeCurrentAddress,
	bool _mayBeComplementedCurrentAddress = false,
	bool _mayBeUnknownLocalTag = false
)
{
	AbstractStackValue output;
	if (std::optional<u512> literal = operationInputIndependentLiteral(_instruction, _arguments, _stackArgument))
	{
		output.literalValue = *literal;
		return output;
	}

	output.mayBeForeignTag = _mayBeForeignTag;
	output.mayBeUnknownLocalTag = _mayBeUnknownLocalTag;
	if (_mayBeCurrentAddress)
		output.markMayBeCurrentAddress();
	if (_mayBeComplementedCurrentAddress)
		output.markMayBeComplementedCurrentAddress();

	bool externalJumpTargetInput = false;
	if (operationOutputCanPropagateStackInput(_instruction, _arguments, _stackArgument))
		for (size_t i = 0; i < _arguments; ++i)
			externalJumpTargetInput = externalJumpTargetInput || _stackArgument(i).mayBeExternalJumpTarget;
	if (externalJumpTargetInput)
	{
		output.mayBeExternalJumpTarget = true;
	}

		if (std::optional<u512> offset = operationCurrentAddressOffset(_instruction, _arguments, _stackArgument))
		{
			output.currentAddressOffset = *offset;
			if (*offset == 0)
				output.markMayBeCurrentAddress();
		}
		std::set<u512> possibleCurrentOffsets =
			operationPossibleCurrentAddressOffsets(_instruction, _arguments, _stackArgument);
		if (
			!possibleCurrentOffsets.empty() &&
			!(
				output.currentAddressOffset &&
				possibleCurrentOffsets.size() == 1 &&
				possibleCurrentOffsets.count(*output.currentAddressOffset)
			)
		)
		{
			if (possibleCurrentOffsets.count(u512(0)))
				output.mayBeCurrentAddress = true;
			output.mayBeCurrentAddressDerived = true;
			output.possibleCurrentAddressOffsets = std::move(possibleCurrentOffsets);
		}
		if (std::optional<u512> xorMask = operationCurrentAddressXorMask(_instruction, _arguments, _stackArgument))
		{
			output.currentAddressXorMask = *xorMask;
			if (*xorMask == 0)
				output.markMayBeCurrentAddress();
			if (*xorMask == ~u512(0))
				output.markMayBeComplementedCurrentAddress();
		}
	if (std::optional<u512> complementedOffset = operationComplementedCurrentAddressOffset(_instruction, _arguments, _stackArgument))
	{
		output.complementedCurrentAddressOffset = *complementedOffset;
		if (*complementedOffset == 0)
			output.markMayBeComplementedCurrentAddress();
	}
	if (operationMayProduceCurrentAddress(_instruction, _arguments, _stackArgument))
		output.markMayBeCurrentAddress();
	if (operationMayProduceComplementedCurrentAddress(_instruction, _arguments, _stackArgument))
		output.markMayBeComplementedCurrentAddress();
	if (
		identityOperationMayPreserveValue(_instruction, _arguments, _stackArgument, valueMayBeCurrentAddressDerived) ||
		operationMayComplementValue(_instruction, _arguments, _stackArgument, valueMayBeComplementedCurrentAddressDerived)
	)
		output.mayBeCurrentAddressDerived = true;
	if (
		identityOperationMayPreserveValue(_instruction, _arguments, _stackArgument, valueMayBeComplementedCurrentAddressDerived) ||
		operationMayComplementValue(_instruction, _arguments, _stackArgument, valueMayBeCurrentAddressDerived)
	)
		output.mayBeComplementedCurrentAddressDerived = true;
	if (std::optional<size_t> byteIndex = operationCurrentAddressByteIndex(_instruction, _arguments, _stackArgument))
	{
		setCurrentAddressLowByte(output, *byteIndex);
		markByteExtractedValue(output);
	}
	if (std::optional<size_t> byteIndex = operationComplementedCurrentAddressByteIndex(_instruction, _arguments, _stackArgument))
	{
		setComplementedCurrentAddressLowByte(output, *byteIndex);
		markByteExtractedValue(output);
	}
	if (_instruction == Instruction::PUSH0)
		output.literalValue = u512(0);
	propagateIdentityPreservedMetadata(_instruction, _arguments, _stackArgument, output);
	return output;
}

bool callMayWriteCurrentStorage(Instruction _instruction)
{
	return _instruction == Instruction::CALL || _instruction == Instruction::DELEGATECALL;
}

bool isCreateInstruction(Instruction _instruction)
{
	return _instruction == Instruction::CREATE || _instruction == Instruction::CREATE2;
}

bool callWithInputMayWriteCurrentStorage(Instruction _instruction, bool _callMayExecuteCurrentCode)
{
	return _instruction == Instruction::DELEGATECALL || (_instruction == Instruction::CALL && _callMayExecuteCurrentCode);
}

bool externalInstructionMayReenterAndWriteCurrentStorage(Instruction _instruction, bool _createMayExecuteInitCode)
{
	return
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		(isCreateInstruction(_instruction) && _createMayExecuteInitCode);
}

bool callMayWriteReturnDataToMemory(Instruction _instruction)
{
	return
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		_instruction == Instruction::STATICCALL ||
		isCreateInstruction(_instruction);
}

std::optional<size_t> callInputLengthParameter(Instruction _instruction)
{
	if (
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		_instruction == Instruction::STATICCALL
	)
		return static_cast<size_t>(instructionInfo(_instruction).args) - 3;
	if (isCreateInstruction(_instruction))
		return 2;
	return std::nullopt;
}

std::optional<size_t> callOutputLengthParameter(Instruction _instruction)
{
	if (
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		_instruction == Instruction::STATICCALL
	)
		return static_cast<size_t>(instructionInfo(_instruction).args) - 1;
	return std::nullopt;
}

std::optional<size_t> callAddressParameter(Instruction _instruction)
{
	if (
		_instruction == Instruction::CALL ||
		_instruction == Instruction::DELEGATECALL ||
		_instruction == Instruction::STATICCALL
	)
		return 1;
	return std::nullopt;
}

AbstractStackValue verbatimPushLiteral(bytes const& _data, size_t _offset, size_t _size);

struct VerbatimCallEffects
{
	bool mayWriteForeignToMemory = false;
	bool mayWriteForeignToStorage = false;
	bool mayWriteLocalTagToMemory = false;
	bool mayWriteLocalTagToStorage = false;
	bool returnDataMayContainForeignTag = false;
	bool returnDataMayContainLocalTag = false;
	bool mayWriteCurrentAddressToMemory = false;
	bool mayWriteCurrentAddressToStorage = false;
	bool mayWriteComplementedCurrentAddressToMemory = false;
	bool mayWriteComplementedCurrentAddressToStorage = false;
	bool mayWriteExactCurrentAddressToMemory = false;
	bool mayWriteExactCurrentAddressToStorage = false;
	bool mayWriteExactComplementedCurrentAddressToMemory = false;
	bool mayWriteExactComplementedCurrentAddressToStorage = false;
	bool returnDataMayContainCurrentAddress = false;
	bool returnDataMayContainComplementedCurrentAddress = false;
	bool returnDataMayContainExactCurrentAddress = false;
	bool returnDataMayContainExactComplementedCurrentAddress = false;
	bool outputMayBeForeignTag = false;
	bool outputMayBeUnknownLocalTag = false;
	bool outputMayBeCurrentAddress = false;
	bool outputMayBeComplementedCurrentAddress = false;
	bool memoryAddressByteMapsKnown = false;
	AddressByteMap memoryCurrentAddressBytes;
	AddressByteMap memoryComplementedCurrentAddressBytes;
	std::vector<AbstractStackValue> outputValues;
};

VerbatimCallEffects unknownVerbatimCallEffects()
{
	VerbatimCallEffects result;
	result.mayWriteForeignToMemory = true;
	result.mayWriteForeignToStorage = true;
	result.mayWriteLocalTagToMemory = true;
	result.mayWriteLocalTagToStorage = true;
	result.returnDataMayContainForeignTag = true;
	result.returnDataMayContainLocalTag = true;
	result.mayWriteCurrentAddressToMemory = true;
	result.mayWriteCurrentAddressToStorage = true;
	result.mayWriteComplementedCurrentAddressToMemory = true;
	result.mayWriteComplementedCurrentAddressToStorage = true;
	result.mayWriteExactCurrentAddressToMemory = true;
	result.mayWriteExactCurrentAddressToStorage = true;
	result.mayWriteExactComplementedCurrentAddressToMemory = true;
	result.mayWriteExactComplementedCurrentAddressToStorage = true;
	result.returnDataMayContainCurrentAddress = true;
	result.returnDataMayContainComplementedCurrentAddress = true;
	result.returnDataMayContainExactCurrentAddress = true;
	result.returnDataMayContainExactComplementedCurrentAddress = true;
	result.outputMayBeForeignTag = true;
	result.outputMayBeUnknownLocalTag = true;
	result.outputMayBeCurrentAddress = true;
	result.outputMayBeComplementedCurrentAddress = true;
	return result;
}

VerbatimCallEffects analyzeVerbatimCallEffects(
	bytes const& _data,
	std::vector<AbstractStackValue> _stack,
	bool _memoryMayContainForeignTag,
	bool _storageMayContainForeignTag,
	bool _memoryMayContainLocalTag,
	bool _storageMayContainLocalTag,
	bool _returnDataMayContainForeignTag,
	bool _returnDataMayContainLocalTag,
	bool _returnDataMayContainCurrentAddress,
	bool _returnDataMayContainComplementedCurrentAddress,
	bool _memoryMayContainCurrentAddress,
	bool _storageMayContainCurrentAddress,
	bool _memoryMayContainComplementedCurrentAddress,
	bool _storageMayContainComplementedCurrentAddress,
	bool _returnDataMayContainExactCurrentAddress,
	bool _returnDataMayContainExactComplementedCurrentAddress,
	bool _memoryMayContainExactCurrentAddress,
	bool _storageMayContainExactCurrentAddress,
	bool _memoryMayContainExactComplementedCurrentAddress,
	bool _storageMayContainExactComplementedCurrentAddress,
	AddressByteMap _memoryCurrentAddressBytes,
	AddressByteMap _memoryComplementedCurrentAddressBytes,
	CodeCopyTaintRanges const* _taintRanges
)
{
	VerbatimCallEffects result;
	bool memoryMayContainForeignTag = _memoryMayContainForeignTag;
	bool storageMayContainForeignTag = _storageMayContainForeignTag;
	bool memoryMayContainLocalTag = _memoryMayContainLocalTag;
	bool storageMayContainLocalTag = _storageMayContainLocalTag;
	bool returnDataMayContainForeignTag = _returnDataMayContainForeignTag;
	bool returnDataMayContainLocalTag = _returnDataMayContainLocalTag;
	bool returnDataMayContainCurrentAddress = _returnDataMayContainCurrentAddress;
	bool returnDataMayContainComplementedCurrentAddress = _returnDataMayContainComplementedCurrentAddress;
	bool returnDataMayContainExactCurrentAddress = _returnDataMayContainExactCurrentAddress;
	bool returnDataMayContainExactComplementedCurrentAddress = _returnDataMayContainExactComplementedCurrentAddress;
	bool memoryMayContainCurrentAddress = _memoryMayContainCurrentAddress;
	bool storageMayContainCurrentAddress = _storageMayContainCurrentAddress;
	bool memoryMayContainComplementedCurrentAddress = _memoryMayContainComplementedCurrentAddress;
	bool storageMayContainComplementedCurrentAddress = _storageMayContainComplementedCurrentAddress;
	bool memoryMayContainExactCurrentAddress = _memoryMayContainExactCurrentAddress;
	bool storageMayContainExactCurrentAddress = _storageMayContainExactCurrentAddress;
	bool memoryMayContainExactComplementedCurrentAddress = _memoryMayContainExactComplementedCurrentAddress;
	bool storageMayContainExactComplementedCurrentAddress = _storageMayContainExactComplementedCurrentAddress;
	AddressByteMap memoryCurrentAddressBytes = std::move(_memoryCurrentAddressBytes);
	AddressByteMap memoryComplementedCurrentAddressBytes = std::move(_memoryComplementedCurrentAddressBytes);
	result.returnDataMayContainForeignTag = returnDataMayContainForeignTag;
	result.returnDataMayContainLocalTag = returnDataMayContainLocalTag;
	result.returnDataMayContainCurrentAddress = returnDataMayContainCurrentAddress;
	result.returnDataMayContainComplementedCurrentAddress = returnDataMayContainComplementedCurrentAddress;
	result.returnDataMayContainExactCurrentAddress = returnDataMayContainExactCurrentAddress;
	result.returnDataMayContainExactComplementedCurrentAddress = returnDataMayContainExactComplementedCurrentAddress;
	for (size_t offset = 0; offset < _data.size();)
	{
		Instruction instruction = static_cast<Instruction>(_data[offset++]);
		if (!isValidInstruction(instruction))
			return unknownVerbatimCallEffects();

		InstructionInfo const& info = instructionInfo(instruction);
		if (info.additional < 0 || info.args < 0 || info.ret < 0)
			return unknownVerbatimCallEffects();
		size_t const additional = static_cast<size_t>(info.additional);
		if (additional > _data.size() - offset)
			return unknownVerbatimCallEffects();

		if (!instructionMayConsumeDuplicateRelation(instruction))
			for (AbstractStackValue& value: _stack)
				value.duplicateSourceBelowTop.reset();

		if (isPushInstruction(instruction))
		{
			_stack.push_back(verbatimPushLiteral(_data, offset, additional));
			offset += additional;
			continue;
		}
		offset += additional;

		if (isDupInstruction(instruction))
		{
			size_t depth = getDupNumber(instruction);
			if (_stack.size() < depth)
				return unknownVerbatimCallEffects();
			AbstractStackValue duplicatedValue = _stack[_stack.size() - depth];
			duplicatedValue.duplicateSourceBelowTop = depth;
			_stack.push_back(std::move(duplicatedValue));
			continue;
		}
		if (isSwapInstruction(instruction))
		{
			size_t depth = getSwapNumber(instruction) + 1;
			if (_stack.size() < depth)
				return unknownVerbatimCallEffects();
			std::swap(_stack.back(), _stack[_stack.size() - depth]);
			continue;
		}

		size_t const arguments = static_cast<size_t>(info.args);
		size_t const returnValues = static_cast<size_t>(info.ret);
		if (_stack.size() < arguments)
			return unknownVerbatimCallEffects();

		auto stackArgument = [&](size_t _parameterIndex) -> AbstractStackValue const&
		{
			assertThrow(_parameterIndex < arguments, AssemblyException, "Invalid stack argument index.");
			return _stack[_stack.size() - 1 - _parameterIndex];
		};

		CodeCopyReadTaints const codeCopyReadTaints =
			instruction == Instruction::CODECOPY && arguments >= 3 ?
			codeCopyMayReadTaints(stackArgument(1), stackArgument(2), _taintRanges) :
			(
				instruction == Instruction::EXTCODECOPY && arguments >= 4 ?
				extCodeCopyMayReadTaints(
					stackArgument(0),
					stackArgument(2),
					stackArgument(3),
					_taintRanges
				) :
				CodeCopyReadTaints{}
			);
			bool const returnDataCopyReadsForeignReference =
				instruction == Instruction::RETURNDATACOPY &&
				arguments >= 3 &&
				returnDataMayContainForeignTag &&
				valueMayBeNonZero(stackArgument(2));
			bool const returnDataCopyReadsLocalTag =
				instruction == Instruction::RETURNDATACOPY &&
				arguments >= 3 &&
				returnDataMayContainLocalTag &&
				valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsCurrentAddress =
			instruction == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			returnDataMayContainCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsComplementedCurrentAddress =
			instruction == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			returnDataMayContainComplementedCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const operationWritesForeignToMemory =
			memoryStoreValueMay(instruction, arguments, stackArgument, [](AbstractStackValue const& _value) {
				return _value.mayBeForeignTag;
			});
		bool const operationWritesLocalTagToMemory =
			memoryStoreValueMay(instruction, arguments, stackArgument, valueMayBeLocalTag);
		bool const operationWritesCurrentAddressToMemory =
			memoryStoreValueMay(instruction, arguments, stackArgument, valueMayBeCurrentAddressDerived);
		bool const operationWritesComplementedCurrentAddressToMemory =
			memoryStoreValueMay(instruction, arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived);
		std::optional<AddressWordStore> const operationWritesExactCurrentAddressWordToMemory =
			memoryStoreAddressWordStore(instruction, arguments, stackArgument, valueMayBeExactCurrentAddress);
		std::optional<AddressWordStore> const operationWritesExactComplementedCurrentAddressWordToMemory =
			memoryStoreAddressWordStore(instruction, arguments, stackArgument, valueMayBeExactComplementedCurrentAddress);
		std::optional<AddressByteStore> const operationWritesCurrentAddressByteToMemory =
			memoryStoreAddressByteStore(
				instruction,
				arguments,
				stackArgument,
				&AbstractStackValue::currentAddressByteIndex,
				valueMayBeExactCurrentAddress
			);
		std::optional<AddressByteStore> const operationWritesComplementedCurrentAddressByteToMemory =
			memoryStoreAddressByteStore(
				instruction,
				arguments,
				stackArgument,
				&AbstractStackValue::complementedCurrentAddressByteIndex,
				valueMayBeExactComplementedCurrentAddress
			);
		std::vector<AddressByteStore> const operationWritesCurrentAddressStackBytesToMemory =
			memoryStoreAddressStackByteStores(
				instruction,
				arguments,
				stackArgument,
				currentAddressStackBytes
			);
		std::vector<AddressByteStore> const operationWritesComplementedCurrentAddressStackBytesToMemory =
			memoryStoreAddressStackByteStores(
				instruction,
				arguments,
				stackArgument,
				complementedCurrentAddressStackBytes
			);
		bool const operationLoadsCurrentAddressFromBytes =
			instruction == Instruction::MLOAD &&
			arguments >= 1 &&
			addressByteMapMayLoadWord(memoryCurrentAddressBytes, stackArgument(0));
		bool const operationLoadsComplementedCurrentAddressFromBytes =
			instruction == Instruction::MLOAD &&
			arguments >= 1 &&
			addressByteMapMayLoadWord(memoryComplementedCurrentAddressBytes, stackArgument(0));
		bool const operationWritesForeignToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, [](AbstractStackValue const& _value) {
				return _value.mayBeForeignTag;
			});
		bool const operationWritesLocalTagToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, valueMayBeLocalTag);
		bool const operationWritesCurrentAddressToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, valueMayBeCurrentAddressDerived);
		bool const operationWritesComplementedCurrentAddressToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived);
		bool const operationWritesExactCurrentAddressToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, valueMayBeExactCurrentAddress);
		bool const operationWritesExactComplementedCurrentAddressToStorage =
			storageStoreValueMay(instruction, arguments, stackArgument, valueMayBeExactComplementedCurrentAddress);

		bool foreignInput = false;
		bool localTagInput = false;
		bool localTagDerivedInput = false;
		bool currentAddressInput = false;
		bool complementedCurrentAddressInput = false;
		for (size_t i = 0; i < arguments; ++i)
		{
			foreignInput = foreignInput || stackArgument(i).mayBeForeignTag;
			localTagInput =
				localTagInput ||
				stackArgument(i).mayBeExternalJumpTarget ||
				stackArgument(i).mayBeUnknownLocalTag ||
				!stackArgument(i).localTags.empty();
			localTagDerivedInput =
				localTagDerivedInput ||
				stackArgument(i).mayBeUnknownLocalTag ||
				!stackArgument(i).localTags.empty();
			currentAddressInput = currentAddressInput || valueMayBeCurrentAddressDerived(stackArgument(i));
			complementedCurrentAddressInput =
				complementedCurrentAddressInput ||
				valueMayBeComplementedCurrentAddressDerived(stackArgument(i));
		}

		bool callMayWriteForeignMemoryToStorage = false;
		bool callMayWriteLocalTagToStorage = false;
		bool callMayWriteForeignReturnDataToMemory = false;
		bool callMayWriteLocalTagReturnDataToMemory = false;
		bool callMayWriteCurrentAddressReturnDataToMemory = false;
		bool callMayWriteComplementedCurrentAddressReturnDataToMemory = false;
		bool callMayWriteExactCurrentAddressReturnDataToMemory = false;
		bool callMayWriteExactComplementedCurrentAddressReturnDataToMemory = false;
		bool callMayWriteCurrentAddressToStorage = false;
		bool callMayWriteComplementedCurrentAddressToStorage = false;
		bool callMayWriteExactCurrentAddressToStorage = false;
		bool callMayWriteExactComplementedCurrentAddressToStorage = false;
		std::optional<size_t> addressParameter = callAddressParameter(instruction);
		bool const callMayExecuteCurrentCode =
			addressParameter &&
			*addressParameter < arguments &&
			valueMayBeExactCurrentAddress(stackArgument(*addressParameter));
		if (callMayWriteReturnDataToMemory(instruction))
		{
			std::optional<size_t> inputLengthParameter = callInputLengthParameter(instruction);
				bool const callInputMayContainForeign =
					memoryMayContainForeignTag &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				bool const callInputMayContainLocalTag =
					memoryMayContainLocalTag &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				bool const createMayExecuteInitCode =
					isCreateInstruction(instruction) &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode) && callInputMayContainForeign)
					callMayWriteForeignMemoryToStorage = true;
				if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode) && callInputMayContainLocalTag)
					callMayWriteLocalTagToStorage = true;
				if (externalInstructionMayReenterAndWriteCurrentStorage(instruction, createMayExecuteInitCode))
				{
					callMayWriteForeignMemoryToStorage = true;
					callMayWriteLocalTagToStorage = true;
					callMayWriteCurrentAddressToStorage = true;
					callMayWriteComplementedCurrentAddressToStorage = true;
					callMayWriteExactCurrentAddressToStorage = true;
					callMayWriteExactComplementedCurrentAddressToStorage = true;
				}
				bool const callMayExposeForeignToReturnData =
					callMayExecuteCurrentCode ||
					(instruction == Instruction::DELEGATECALL && (storageMayContainForeignTag || callInputMayContainForeign)) ||
					(isCreateInstruction(instruction) && callInputMayContainForeign);
				bool const callMayExposeLocalTagToReturnData =
					callMayExecuteCurrentCode ||
					(instruction == Instruction::DELEGATECALL && (storageMayContainLocalTag || callInputMayContainLocalTag)) ||
					(isCreateInstruction(instruction) && callInputMayContainLocalTag);
			bool const callMayExposeCurrentAddressToReturnData = true;
			bool const callMayExposeComplementedCurrentAddressToReturnData = true;
			bool const callMayExposeExactCurrentAddressToReturnData = true;
			bool const callMayExposeExactComplementedCurrentAddressToReturnData = true;
				result.returnDataMayContainForeignTag = callMayExposeForeignToReturnData;
				returnDataMayContainForeignTag = callMayExposeForeignToReturnData;
				result.returnDataMayContainLocalTag = callMayExposeLocalTagToReturnData;
				returnDataMayContainLocalTag = callMayExposeLocalTagToReturnData;
				result.returnDataMayContainCurrentAddress = callMayExposeCurrentAddressToReturnData;
			returnDataMayContainCurrentAddress = callMayExposeCurrentAddressToReturnData;
			result.returnDataMayContainComplementedCurrentAddress = callMayExposeComplementedCurrentAddressToReturnData;
			returnDataMayContainComplementedCurrentAddress = callMayExposeComplementedCurrentAddressToReturnData;
			result.returnDataMayContainExactCurrentAddress = callMayExposeExactCurrentAddressToReturnData;
			returnDataMayContainExactCurrentAddress = callMayExposeExactCurrentAddressToReturnData;
			result.returnDataMayContainExactComplementedCurrentAddress =
				callMayExposeExactComplementedCurrentAddressToReturnData;
			returnDataMayContainExactComplementedCurrentAddress =
				callMayExposeExactComplementedCurrentAddressToReturnData;

			std::optional<size_t> outputLengthParameter = callOutputLengthParameter(instruction);
			if (
				callMayExposeForeignToReturnData &&
				outputLengthParameter &&
				*outputLengthParameter < arguments &&
				valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteForeignReturnDataToMemory = true;
				if (
					callMayExposeLocalTagToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteLocalTagReturnDataToMemory = true;
				if (
					callMayExposeCurrentAddressToReturnData &&
				outputLengthParameter &&
				*outputLengthParameter < arguments &&
				valueMayBeNonZero(stackArgument(*outputLengthParameter))
			)
				callMayWriteCurrentAddressReturnDataToMemory = true;
			if (
				callMayExposeExactCurrentAddressToReturnData &&
				outputLengthParameter &&
				*outputLengthParameter < arguments &&
				valueMayBeNonZero(stackArgument(*outputLengthParameter))
			)
				callMayWriteExactCurrentAddressReturnDataToMemory = true;
			if (
				callMayExposeComplementedCurrentAddressToReturnData &&
				outputLengthParameter &&
				*outputLengthParameter < arguments &&
				valueMayBeNonZero(stackArgument(*outputLengthParameter))
			)
				callMayWriteComplementedCurrentAddressReturnDataToMemory = true;
			if (
				callMayExposeExactComplementedCurrentAddressToReturnData &&
				outputLengthParameter &&
				*outputLengthParameter < arguments &&
				valueMayBeNonZero(stackArgument(*outputLengthParameter))
			)
				callMayWriteExactComplementedCurrentAddressReturnDataToMemory = true;
			}
			if (callMayExecuteCurrentCode && callMayWriteCurrentStorage(instruction))
				callMayWriteForeignMemoryToStorage = true;
			if (callMayExecuteCurrentCode && callMayWriteCurrentStorage(instruction))
				callMayWriteLocalTagToStorage = true;
			if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode))
		{
			callMayWriteCurrentAddressToStorage = true;
			callMayWriteComplementedCurrentAddressToStorage = true;
			callMayWriteExactCurrentAddressToStorage = true;
			callMayWriteExactComplementedCurrentAddressToStorage = true;
		}

			bool const outputCanPropagateStackInput =
				operationOutputCanPropagateStackInput(instruction, arguments, stackArgument);
			bool foreignOutput =
				(outputCanPropagateStackInput && foreignInput) ||
				(instruction == Instruction::MLOAD && memoryMayContainForeignTag) ||
				(instruction == Instruction::SLOAD && storageMayContainForeignTag);
			AbstractStackValue identityPreservedMetadata;
			propagateIdentityPreservedMetadata(instruction, arguments, stackArgument, identityPreservedMetadata);
			bool const preservesKnownLocalTag =
				!identityPreservedMetadata.localTags.empty() &&
				!identityPreservedMetadata.mayBeUnknownLocalTag;
			bool localTagOutput =
				(
					outputCanPropagateStackInput &&
					localTagDerivedInput &&
					!preservesKnownLocalTag
				) ||
				(instruction == Instruction::MLOAD && memoryMayContainLocalTag) ||
				(instruction == Instruction::SLOAD && storageMayContainLocalTag);
			bool currentAddressOutput =
				(instruction == Instruction::MLOAD && (memoryMayContainExactCurrentAddress || operationLoadsCurrentAddressFromBytes)) ||
				(instruction == Instruction::SLOAD && storageMayContainExactCurrentAddress);
			bool currentAddressDerivedOutput =
				(instruction == Instruction::MLOAD && memoryMayContainCurrentAddress) ||
				(instruction == Instruction::SLOAD && storageMayContainCurrentAddress);
		bool complementedCurrentAddressOutput =
			(
				instruction == Instruction::MLOAD &&
				(memoryMayContainExactComplementedCurrentAddress || operationLoadsComplementedCurrentAddressFromBytes)
			) ||
			(instruction == Instruction::SLOAD && storageMayContainExactComplementedCurrentAddress);
		bool complementedCurrentAddressDerivedOutput =
			(instruction == Instruction::MLOAD && memoryMayContainComplementedCurrentAddress) ||
			(instruction == Instruction::SLOAD && storageMayContainComplementedCurrentAddress);

		AddressByteMapInvalidation const addressByteMapInvalidation = invalidateAddressByteMapsForMemoryWrite(
			instruction,
			arguments,
			stackArgument,
			memoryCurrentAddressBytes,
			memoryComplementedCurrentAddressBytes
		);

		if (
			operationWritesForeignToMemory ||
			codeCopyReadTaints.mayReadForeignReference ||
			returnDataCopyReadsForeignReference ||
			callMayWriteForeignReturnDataToMemory
		)
		{
				memoryMayContainForeignTag = true;
				result.mayWriteForeignToMemory = true;
			}
			if (
				operationWritesLocalTagToMemory ||
				codeCopyReadTaints.mayReadLocalTag ||
				returnDataCopyReadsLocalTag ||
				callMayWriteLocalTagReturnDataToMemory
			)
			{
				memoryMayContainLocalTag = true;
				result.mayWriteLocalTagToMemory = true;
			}
		if (codeCopyReadTaints.mayReadCurrentAddress)
		{
			memoryMayContainCurrentAddress = true;
			memoryMayContainExactCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
			result.mayWriteExactCurrentAddressToMemory = true;
		}
		if (codeCopyReadTaints.mayReadComplementedCurrentAddress)
		{
			memoryMayContainComplementedCurrentAddress = true;
			memoryMayContainExactComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
			result.mayWriteExactComplementedCurrentAddressToMemory = true;
		}
		if (returnDataCopyReadsCurrentAddress || callMayWriteCurrentAddressReturnDataToMemory)
		{
			memoryMayContainCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
		}
		if (addressByteMapInvalidation.currentAddressPrecisionLost)
		{
			memoryMayContainCurrentAddress = true;
			memoryMayContainExactCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
			result.mayWriteExactCurrentAddressToMemory = true;
		}
		if (
			(
				instruction == Instruction::RETURNDATACOPY &&
				arguments >= 3 &&
				returnDataMayContainExactCurrentAddress &&
				valueMayBeNonZero(stackArgument(2))
			) ||
			callMayWriteExactCurrentAddressReturnDataToMemory
		)
		{
			memoryMayContainExactCurrentAddress = true;
			result.mayWriteExactCurrentAddressToMemory = true;
		}
		if (returnDataCopyReadsComplementedCurrentAddress || callMayWriteComplementedCurrentAddressReturnDataToMemory)
		{
			memoryMayContainComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
		}
		if (addressByteMapInvalidation.complementedCurrentAddressPrecisionLost)
		{
			memoryMayContainComplementedCurrentAddress = true;
			memoryMayContainExactComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
			result.mayWriteExactComplementedCurrentAddressToMemory = true;
		}
		if (
			(
				instruction == Instruction::RETURNDATACOPY &&
				arguments >= 3 &&
				returnDataMayContainExactComplementedCurrentAddress &&
				valueMayBeNonZero(stackArgument(2))
			) ||
			callMayWriteExactComplementedCurrentAddressReturnDataToMemory
		)
		{
			memoryMayContainExactComplementedCurrentAddress = true;
			result.mayWriteExactComplementedCurrentAddressToMemory = true;
		}
			if (operationWritesForeignToStorage)
			{
				storageMayContainForeignTag = true;
				result.mayWriteForeignToStorage = true;
			}
			if (operationWritesLocalTagToStorage)
			{
				storageMayContainLocalTag = true;
				result.mayWriteLocalTagToStorage = true;
			}
			if (callMayWriteForeignMemoryToStorage)
			{
				storageMayContainForeignTag = true;
				result.mayWriteForeignToStorage = true;
			}
			if (callMayWriteLocalTagToStorage)
			{
				storageMayContainLocalTag = true;
				result.mayWriteLocalTagToStorage = true;
			}
		if (callMayWriteCurrentAddressToStorage)
		{
			storageMayContainCurrentAddress = true;
			result.mayWriteCurrentAddressToStorage = true;
		}
		if (callMayWriteExactCurrentAddressToStorage)
		{
			storageMayContainExactCurrentAddress = true;
			result.mayWriteExactCurrentAddressToStorage = true;
		}
		if (callMayWriteComplementedCurrentAddressToStorage)
		{
			storageMayContainComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToStorage = true;
		}
		if (callMayWriteExactComplementedCurrentAddressToStorage)
		{
			storageMayContainExactComplementedCurrentAddress = true;
			result.mayWriteExactComplementedCurrentAddressToStorage = true;
		}
		if (operationWritesCurrentAddressToMemory)
		{
			memoryMayContainCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
		}
		if (operationWritesExactCurrentAddressWordToMemory)
		{
			if (recordAddressWordStore(memoryCurrentAddressBytes, *operationWritesExactCurrentAddressWordToMemory))
			{
				memoryMayContainExactCurrentAddress = true;
				result.mayWriteExactCurrentAddressToMemory = true;
			}
		}
		if (
			operationWritesCurrentAddressByteToMemory &&
			recordAddressByteStore(memoryCurrentAddressBytes, *operationWritesCurrentAddressByteToMemory)
		)
		{
			memoryMayContainCurrentAddress = true;
			memoryMayContainExactCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
			result.mayWriteExactCurrentAddressToMemory = true;
		}
		if (recordAddressByteStores(memoryCurrentAddressBytes, operationWritesCurrentAddressStackBytesToMemory))
		{
			memoryMayContainCurrentAddress = true;
			memoryMayContainExactCurrentAddress = true;
			result.mayWriteCurrentAddressToMemory = true;
			result.mayWriteExactCurrentAddressToMemory = true;
		}
		if (operationWritesCurrentAddressToStorage)
		{
			storageMayContainCurrentAddress = true;
			result.mayWriteCurrentAddressToStorage = true;
		}
		if (operationWritesExactCurrentAddressToStorage)
		{
			storageMayContainExactCurrentAddress = true;
			result.mayWriteExactCurrentAddressToStorage = true;
		}
		if (operationWritesComplementedCurrentAddressToMemory)
		{
			memoryMayContainComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
		}
		if (operationWritesExactComplementedCurrentAddressWordToMemory)
		{
			if (
				recordAddressWordStore(
					memoryComplementedCurrentAddressBytes,
					*operationWritesExactComplementedCurrentAddressWordToMemory
				)
			)
			{
				memoryMayContainExactComplementedCurrentAddress = true;
				result.mayWriteExactComplementedCurrentAddressToMemory = true;
			}
		}
		if (
			operationWritesComplementedCurrentAddressByteToMemory &&
			recordAddressByteStore(memoryComplementedCurrentAddressBytes, *operationWritesComplementedCurrentAddressByteToMemory)
		)
		{
			memoryMayContainComplementedCurrentAddress = true;
			memoryMayContainExactComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
			result.mayWriteExactComplementedCurrentAddressToMemory = true;
		}
		if (
			recordAddressByteStores(
				memoryComplementedCurrentAddressBytes,
				operationWritesComplementedCurrentAddressStackBytesToMemory
			)
		)
		{
			memoryMayContainComplementedCurrentAddress = true;
			memoryMayContainExactComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToMemory = true;
			result.mayWriteExactComplementedCurrentAddressToMemory = true;
		}
		if (operationWritesComplementedCurrentAddressToStorage)
		{
			storageMayContainComplementedCurrentAddress = true;
			result.mayWriteComplementedCurrentAddressToStorage = true;
		}
		if (operationWritesExactComplementedCurrentAddressToStorage)
		{
			storageMayContainExactComplementedCurrentAddress = true;
			result.mayWriteExactComplementedCurrentAddressToStorage = true;
		}

		AbstractStackValue outputValue = operationOutputValue(
			instruction,
			arguments,
				stackArgument,
				foreignOutput,
				currentAddressOutput,
				complementedCurrentAddressOutput,
				localTagOutput
			);
		if (currentAddressDerivedOutput)
			outputValue.mayBeCurrentAddressDerived = true;
		if (complementedCurrentAddressDerivedOutput)
			outputValue.mayBeComplementedCurrentAddressDerived = true;
		_stack.erase(_stack.end() - static_cast<std::vector<AbstractStackValue>::difference_type>(arguments), _stack.end());
		for (AbstractStackValue& value: _stack)
			value.duplicateSourceBelowTop.reset();
		for (size_t i = 0; i < returnValues; ++i)
			_stack.push_back(outputValue);
	}

	for (AbstractStackValue const& value: _stack)
	{
		result.outputMayBeForeignTag = result.outputMayBeForeignTag || value.mayBeForeignTag;
		result.outputMayBeUnknownLocalTag =
			result.outputMayBeUnknownLocalTag ||
			value.mayBeExternalJumpTarget ||
			value.mayBeUnknownLocalTag ||
			!value.localTags.empty();
		result.outputMayBeCurrentAddress =
			result.outputMayBeCurrentAddress ||
			valueMayBeCurrentAddressDerived(value);
		result.outputMayBeComplementedCurrentAddress =
			result.outputMayBeComplementedCurrentAddress ||
			valueMayBeComplementedCurrentAddressDerived(value);
	}
	result.memoryAddressByteMapsKnown = true;
	result.memoryCurrentAddressBytes = std::move(memoryCurrentAddressBytes);
	result.memoryComplementedCurrentAddressBytes = std::move(memoryComplementedCurrentAddressBytes);
	result.outputValues = std::move(_stack);

	return result;
}

AbstractStackValue verbatimPushLiteral(bytes const& _data, size_t _offset, size_t _size)
{
	u512 value = 0;
	for (size_t i = 0; i < _size; ++i)
		value = (value << 8) | _data[_offset + i];
	return AbstractStackValue::literal(value);
}

CodeCopyReadTaints allTaints(CodeCopyTaintRanges const* _taintRanges)
{
	if (!_taintRanges)
		return {};
	CodeCopyReadTaints taints{
		!_taintRanges->foreignReferences.empty() || !_taintRanges->foreignTags.empty(),
		!_taintRanges->localTags.empty() || !_taintRanges->localTagReferences.empty(),
		!_taintRanges->currentAddresses.empty(),
		!_taintRanges->complementedCurrentAddresses.empty(),
		{}
	};
	for (auto const& [offset, size, subId, tagId]: _taintRanges->foreignTags)
	{
		(void)offset;
		(void)size;
		taints.foreignTags.emplace(subId, tagId);
	}
	return taints;
}

CodeCopyReadTaints verbatimMayCopyTaints(
	bytes const& _data,
	std::vector<AbstractStackValue> _stack,
	CodeCopyTaintRanges const* _taintRanges
)
{
	if (!_taintRanges)
		return {};

	CodeCopyReadTaints copiedTaints;
	for (size_t offset = 0; offset < _data.size();)
	{
		Instruction instruction = static_cast<Instruction>(_data[offset++]);
		if (!isValidInstruction(instruction))
			return allTaints(_taintRanges);

		InstructionInfo const& info = instructionInfo(instruction);
		if (info.additional < 0 || info.args < 0 || info.ret < 0)
			return allTaints(_taintRanges);
		size_t const additional = static_cast<size_t>(info.additional);
		if (additional > _data.size() - offset)
			return allTaints(_taintRanges);

		if (!instructionMayConsumeDuplicateRelation(instruction))
			for (AbstractStackValue& value: _stack)
				value.duplicateSourceBelowTop.reset();

		if (isPushInstruction(instruction))
		{
			_stack.push_back(verbatimPushLiteral(_data, offset, additional));
			offset += additional;
			continue;
		}
		offset += additional;

		if (isDupInstruction(instruction))
		{
			size_t depth = getDupNumber(instruction);
			if (_stack.size() < depth)
				return allTaints(_taintRanges);
			AbstractStackValue duplicatedValue = _stack[_stack.size() - depth];
			duplicatedValue.duplicateSourceBelowTop = depth;
			_stack.push_back(std::move(duplicatedValue));
			continue;
		}
		if (isSwapInstruction(instruction))
		{
			size_t depth = getSwapNumber(instruction) + 1;
			if (_stack.size() < depth)
				return allTaints(_taintRanges);
			std::swap(_stack.back(), _stack[_stack.size() - depth]);
			continue;
		}

		size_t const arguments = static_cast<size_t>(info.args);
		size_t const returnValues = static_cast<size_t>(info.ret);
		if (_stack.size() < arguments)
			return allTaints(_taintRanges);

		auto stackArgument = [&](size_t _parameterIndex) -> AbstractStackValue const&
		{
			assertThrow(_parameterIndex < arguments, AssemblyException, "Invalid stack argument index.");
			return _stack[_stack.size() - 1 - _parameterIndex];
		};

		if (
			instruction == Instruction::CODECOPY &&
			arguments >= 3
		)
		{
			CodeCopyReadTaints taints = codeCopyMayReadTaints(stackArgument(1), stackArgument(2), _taintRanges);
				copiedTaints.mayReadForeignReference =
					copiedTaints.mayReadForeignReference || taints.mayReadForeignReference;
				copiedTaints.foreignTags.insert(taints.foreignTags.begin(), taints.foreignTags.end());
				copiedTaints.mayReadLocalTag =
					copiedTaints.mayReadLocalTag || taints.mayReadLocalTag;
				copiedTaints.mayReadCurrentAddress =
					copiedTaints.mayReadCurrentAddress || taints.mayReadCurrentAddress;
				copiedTaints.mayReadComplementedCurrentAddress =
				copiedTaints.mayReadComplementedCurrentAddress || taints.mayReadComplementedCurrentAddress;
		}
		if (
			instruction == Instruction::EXTCODECOPY &&
			arguments >= 4
		)
		{
			CodeCopyReadTaints taints = extCodeCopyMayReadTaints(stackArgument(0), stackArgument(2), stackArgument(3), _taintRanges);
				copiedTaints.mayReadForeignReference =
					copiedTaints.mayReadForeignReference || taints.mayReadForeignReference;
				copiedTaints.foreignTags.insert(taints.foreignTags.begin(), taints.foreignTags.end());
				copiedTaints.mayReadLocalTag =
					copiedTaints.mayReadLocalTag || taints.mayReadLocalTag;
				copiedTaints.mayReadCurrentAddress =
					copiedTaints.mayReadCurrentAddress || taints.mayReadCurrentAddress;
				copiedTaints.mayReadComplementedCurrentAddress =
				copiedTaints.mayReadComplementedCurrentAddress || taints.mayReadComplementedCurrentAddress;
		}

		bool foreignInput = false;
		for (size_t i = 0; i < arguments; ++i)
			foreignInput = foreignInput || stackArgument(i).mayBeForeignTag;

		AbstractStackValue outputValue = operationOutputValue(
			instruction,
			arguments,
			stackArgument,
			operationOutputCanPropagateStackInput(instruction, arguments, stackArgument) && foreignInput,
			false
		);
		_stack.erase(_stack.end() - static_cast<std::vector<AbstractStackValue>::difference_type>(arguments), _stack.end());
		for (AbstractStackValue& value: _stack)
			value.duplicateSourceBelowTop.reset();
		for (size_t i = 0; i < returnValues; ++i)
			_stack.push_back(outputValue);
	}

	return copiedTaints;
}

struct FunctionFrame
{
	size_t stackBase = 0;
	size_t returnSlots = 0;
	FunctionReturnTarget returnTarget;

	bool operator==(FunctionFrame const& _other) const
	{
		return
			stackBase == _other.stackBase &&
			returnSlots == _other.returnSlots &&
			returnTarget == _other.returnTarget;
	}
};

struct AbstractStackState
{
		std::vector<AbstractStackValue> stack;
		bool unknownBelow = false;
		bool enteringFunction = false;
		bool enteredViaFunctionJump = false;
		bool impreciseControlFlow = false;
		bool memoryMayContainForeignTag = false;
		bool storageMayContainForeignTag = false;
		bool memoryMayContainLocalTag = false;
		bool storageMayContainLocalTag = false;
		bool returnDataMayContainForeignTag = false;
		bool returnDataMayContainLocalTag = false;
		bool returnDataMayContainCurrentAddress = false;
	bool returnDataMayContainComplementedCurrentAddress = false;
	bool memoryMayContainCurrentAddress = false;
	bool storageMayContainCurrentAddress = false;
	bool memoryMayContainComplementedCurrentAddress = false;
	bool storageMayContainComplementedCurrentAddress = false;
	bool returnDataMayContainExactCurrentAddress = false;
	bool returnDataMayContainExactComplementedCurrentAddress = false;
	bool memoryMayContainExactCurrentAddress = false;
	bool storageMayContainExactCurrentAddress = false;
	bool memoryMayContainExactComplementedCurrentAddress = false;
	bool storageMayContainExactComplementedCurrentAddress = false;
	AddressByteMap memoryCurrentAddressBytes;
	AddressByteMap memoryComplementedCurrentAddressBytes;
	std::vector<FunctionFrame> functionFrames;

	bool operator==(AbstractStackState const& _other) const
	{
		return
				unknownBelow == _other.unknownBelow &&
				enteringFunction == _other.enteringFunction &&
				enteredViaFunctionJump == _other.enteredViaFunctionJump &&
				memoryMayContainForeignTag == _other.memoryMayContainForeignTag &&
				storageMayContainForeignTag == _other.storageMayContainForeignTag &&
				memoryMayContainLocalTag == _other.memoryMayContainLocalTag &&
				storageMayContainLocalTag == _other.storageMayContainLocalTag &&
				returnDataMayContainForeignTag == _other.returnDataMayContainForeignTag &&
				returnDataMayContainLocalTag == _other.returnDataMayContainLocalTag &&
				returnDataMayContainCurrentAddress == _other.returnDataMayContainCurrentAddress &&
			returnDataMayContainComplementedCurrentAddress == _other.returnDataMayContainComplementedCurrentAddress &&
			memoryMayContainCurrentAddress == _other.memoryMayContainCurrentAddress &&
			storageMayContainCurrentAddress == _other.storageMayContainCurrentAddress &&
			memoryMayContainComplementedCurrentAddress == _other.memoryMayContainComplementedCurrentAddress &&
			storageMayContainComplementedCurrentAddress == _other.storageMayContainComplementedCurrentAddress &&
			returnDataMayContainExactCurrentAddress == _other.returnDataMayContainExactCurrentAddress &&
			returnDataMayContainExactComplementedCurrentAddress == _other.returnDataMayContainExactComplementedCurrentAddress &&
			memoryMayContainExactCurrentAddress == _other.memoryMayContainExactCurrentAddress &&
			storageMayContainExactCurrentAddress == _other.storageMayContainExactCurrentAddress &&
			memoryMayContainExactComplementedCurrentAddress == _other.memoryMayContainExactComplementedCurrentAddress &&
			storageMayContainExactComplementedCurrentAddress == _other.storageMayContainExactComplementedCurrentAddress &&
			memoryCurrentAddressBytes == _other.memoryCurrentAddressBytes &&
			memoryComplementedCurrentAddressBytes == _other.memoryComplementedCurrentAddressBytes &&
			functionFrames == _other.functionFrames &&
			stack == _other.stack;
	}
};

bool functionReturnTargetsMergeable(FunctionReturnTarget const& _left, FunctionReturnTarget const& _right)
{
	if (hasKnownReturnTarget(_left) != hasKnownReturnTarget(_right))
		return false;
	if (_left.literalValue && _right.literalValue && _left.literalValue != _right.literalValue)
		return false;
	return true;
}

bool mergeFunctionReturnTarget(FunctionReturnTarget& _target, FunctionReturnTarget const& _other)
{
	assertThrow(
		functionReturnTargetsMergeable(_target, _other),
		AssemblyException,
		"Incompatible function return targets."
	);

	bool changed = false;
	if (_other.mustBeExternalJumpTarget && !_target.mustBeExternalJumpTarget)
	{
		_target.mustBeExternalJumpTarget = true;
		changed = true;
	}
	for (size_t tag: _other.localTags)
		changed = _target.localTags.insert(tag).second || changed;
	if (!_target.literalValue && _other.literalValue)
	{
		_target.literalValue = _other.literalValue;
		changed = true;
	}
	return changed;
}

bool functionFramesMergeable(
	std::vector<FunctionFrame> const& _left,
	std::vector<FunctionFrame> const& _right
)
{
	if (_left.size() != _right.size())
		return false;
	for (size_t i = 0; i < _left.size(); ++i)
		if (
			_left[i].stackBase != _right[i].stackBase ||
			_left[i].returnSlots != _right[i].returnSlots ||
			!functionReturnTargetsMergeable(_left[i].returnTarget, _right[i].returnTarget)
		)
			return false;
	return true;
}

bool mergeFunctionFrames(std::vector<FunctionFrame>& _target, std::vector<FunctionFrame> const& _other)
{
	assertThrow(functionFramesMergeable(_target, _other), AssemblyException, "Incompatible function frames.");
	bool changed = false;
	for (size_t i = 0; i < _target.size(); ++i)
		changed = mergeFunctionReturnTarget(_target[i].returnTarget, _other[i].returnTarget) || changed;
	return changed;
}

bool mergeAddressByteMap(AddressByteMap& _target, AddressByteMap const& _other, bool& _mayContainExactAddress)
{
	if (_target == _other)
		return false;

	bool changed = false;
	if (!_target.empty())
	{
		_target.clear();
		changed = true;
	}
	if (!_mayContainExactAddress)
	{
		_mayContainExactAddress = true;
		changed = true;
	}
	return changed;
}

bool stackStatesMergeable(AbstractStackState const& _left, AbstractStackState const& _right)
{
	return
		_left.stack.size() == _right.stack.size() &&
		_left.unknownBelow == _right.unknownBelow &&
		_left.enteringFunction == _right.enteringFunction &&
		_left.enteredViaFunctionJump == _right.enteredViaFunctionJump &&
		functionFramesMergeable(_left.functionFrames, _right.functionFrames);
}

bool mergeStackState(AbstractStackState& _target, AbstractStackState const& _other)
{
	assertThrow(stackStatesMergeable(_target, _other), AssemblyException, "Incompatible stack states.");
	bool changed = false;
	auto mergeFlag = [&](bool& _targetFlag, bool _otherFlag)
	{
		if (_otherFlag && !_targetFlag)
		{
			_targetFlag = true;
			changed = true;
		}
	};

	mergeFlag(_target.memoryMayContainForeignTag, _other.memoryMayContainForeignTag);
	mergeFlag(_target.impreciseControlFlow, _other.impreciseControlFlow);
	mergeFlag(_target.storageMayContainForeignTag, _other.storageMayContainForeignTag);
	mergeFlag(_target.memoryMayContainLocalTag, _other.memoryMayContainLocalTag);
	mergeFlag(_target.storageMayContainLocalTag, _other.storageMayContainLocalTag);
	mergeFlag(_target.returnDataMayContainForeignTag, _other.returnDataMayContainForeignTag);
	mergeFlag(_target.returnDataMayContainLocalTag, _other.returnDataMayContainLocalTag);
	mergeFlag(_target.returnDataMayContainCurrentAddress, _other.returnDataMayContainCurrentAddress);
	mergeFlag(_target.returnDataMayContainComplementedCurrentAddress, _other.returnDataMayContainComplementedCurrentAddress);
	mergeFlag(_target.memoryMayContainCurrentAddress, _other.memoryMayContainCurrentAddress);
	mergeFlag(_target.storageMayContainCurrentAddress, _other.storageMayContainCurrentAddress);
	mergeFlag(_target.memoryMayContainComplementedCurrentAddress, _other.memoryMayContainComplementedCurrentAddress);
	mergeFlag(_target.storageMayContainComplementedCurrentAddress, _other.storageMayContainComplementedCurrentAddress);
	mergeFlag(_target.returnDataMayContainExactCurrentAddress, _other.returnDataMayContainExactCurrentAddress);
	mergeFlag(_target.returnDataMayContainExactComplementedCurrentAddress, _other.returnDataMayContainExactComplementedCurrentAddress);
	mergeFlag(_target.memoryMayContainExactCurrentAddress, _other.memoryMayContainExactCurrentAddress);
	mergeFlag(_target.storageMayContainExactCurrentAddress, _other.storageMayContainExactCurrentAddress);
	mergeFlag(_target.memoryMayContainExactComplementedCurrentAddress, _other.memoryMayContainExactComplementedCurrentAddress);
	mergeFlag(_target.storageMayContainExactComplementedCurrentAddress, _other.storageMayContainExactComplementedCurrentAddress);

	changed =
		mergeAddressByteMap(
			_target.memoryCurrentAddressBytes,
			_other.memoryCurrentAddressBytes,
			_target.memoryMayContainExactCurrentAddress
		) ||
		changed;
	changed =
		mergeAddressByteMap(
			_target.memoryComplementedCurrentAddressBytes,
			_other.memoryComplementedCurrentAddressBytes,
			_target.memoryMayContainExactComplementedCurrentAddress
		) ||
		changed;
	changed = mergeFunctionFrames(_target.functionFrames, _other.functionFrames) || changed;

	for (size_t i = 0; i < _target.stack.size(); ++i)
		changed = _target.stack[i].merge(_other.stack[i]) || changed;
	return changed;
}

void refineIsZeroTestedValueToZero(AbstractStackState& _state, AbstractStackValue const& _condition)
{
	if (!_condition.isZeroOfValueBelow)
		return;
	size_t const sourceBelowCondition = *_condition.isZeroOfValueBelow;
	if (sourceBelowCondition == 0 || sourceBelowCondition > _state.stack.size())
		return;
	_state.stack[_state.stack.size() - sourceBelowCondition] = AbstractStackValue::literal(0);
}

[[maybe_unused]] std::optional<std::string> validateStackAndJumpTargets(
	AssemblyItems const& _items,
	std::set<size_t> const& _tagsReferencedFromOutside,
	std::set<size_t> const& _copiedTagsReferencedFromOutside,
	std::map<size_t, size_t> const& _externalTagParams,
	std::map<size_t, size_t> const& _externalTagReturns,
	std::map<size_t, size_t> const* _literalJumpTargets = nullptr,
	CodeCopyTaintRanges const* _codeCopyTaintRanges = nullptr,
	std::set<std::pair<size_t, size_t>>* _usedForeignTags = nullptr,
		std::set<std::pair<size_t, size_t>>* _copiedForeignTags = nullptr,
	bool _allowUnsetImmutableOccurrences = false,
	bool _validateFunctionFrames = true,
	bool _rejectLiteralJumpTargetsWithoutMap = false,
	bool _allowUnresolvedDynamicJumpTargets = false,
	bool _pruneImpreciseInvalidStates = false,
	size_t _initialStackHeight = 0
)
{
	if (_items.empty())
		return std::nullopt;
	if (_initialStackHeight > Assembly::StackLimit)
		return "Stack too deep.";

	std::map<size_t, size_t> tagToItemIndex;
	for (auto const& [index, item]: _items | ranges::views::enumerate)
		if (item.type() == Tag)
		{
			auto [subId, tagId] = item.splitForeignPushTag();
			if (subId == std::numeric_limits<size_t>::max())
				tagToItemIndex.emplace(tagId, static_cast<size_t>(index));
		}
	std::set<size_t> tagsTargetedByIntoFunction;
	for (size_t index = 1; index < _items.size(); ++index)
	{
		AssemblyItem const& item = _items[index];
		if (
			item.type() != Operation ||
			(item.instruction() != Instruction::JUMP && item.instruction() != Instruction::JUMPI) ||
			item.getJumpType() != AssemblyItem::JumpType::IntoFunction
		)
			continue;
		AssemblyItem const& targetItem = _items[index - 1];
		if (targetItem.type() != PushTag)
			continue;
		auto [subId, tagId] = targetItem.splitForeignPushTag();
		if (subId == std::numeric_limits<size_t>::max())
			tagsTargetedByIntoFunction.insert(tagId);
	}

	std::vector<std::vector<AbstractStackState>> states(_items.size());
	std::vector<std::pair<size_t, size_t>> worklist;
	std::optional<std::string> pendingError;

	auto normalizeTransientRelations = [&](size_t _index, AbstractStackState& _state)
	{
		if (_index >= _items.size())
			return;
		AssemblyItem const& nextItem = _items[_index];
		bool const itemMayConsumeDuplicateRelation =
			nextItem.type() == Operation &&
			instructionMayConsumeDuplicateRelation(nextItem.instruction());
		bool const itemMayConsumeIsZeroRelation =
			nextItem.type() == PushTag ||
			nextItem.type() == Push ||
			(nextItem.type() == Operation && nextItem.instruction() == Instruction::JUMPI);
		if (!itemMayConsumeDuplicateRelation || !itemMayConsumeIsZeroRelation)
			for (AbstractStackValue& value: _state.stack)
			{
				if (!itemMayConsumeDuplicateRelation)
					value.duplicateSourceBelowTop.reset();
				if (!itemMayConsumeIsZeroRelation)
					value.isZeroOfValueBelow.reset();
			}
	};

	auto mergeStateAt = [&](size_t _index, AbstractStackState _state)
	{
		if (_index >= _items.size() || pendingError)
			return;
		normalizeTransientRelations(_index, _state);
		if (_state.stack.size() > Assembly::StackLimit)
		{
			pendingError = "Stack too deep.";
			return;
		}

		auto& indexStates = states[_index];
		for (size_t stateIndex = 0; stateIndex < indexStates.size(); ++stateIndex)
		{
			if (indexStates[stateIndex] == _state)
				return;
			if (stackStatesMergeable(indexStates[stateIndex], _state))
			{
				if (mergeStackState(indexStates[stateIndex], _state))
					worklist.emplace_back(_index, stateIndex);
				return;
			}
		}

		indexStates.emplace_back(std::move(_state));
		worklist.emplace_back(_index, indexStates.size() - 1);
	};

	AbstractStackState initialState;
	initialState.stack.insert(initialState.stack.end(), _initialStackHeight, AbstractStackValue::unknown());
	mergeStateAt(0, std::move(initialState));
	auto seedExternalEntry = [&](size_t _tagId, size_t _index, bool _copiedBytecodeReference)
	{
		size_t params = 0;
		size_t const* namedTagParams = util::valueOrNullptr(_externalTagParams, _tagId);
		size_t const* namedTagReturns = util::valueOrNullptr(_externalTagReturns, _tagId);
		if (namedTagParams)
			params = *namedTagParams;
		if (params >= Assembly::StackLimit)
		{
			pendingError = "Stack too deep.";
			return;
		}
		AbstractStackState entryState;
		entryState.stack = {AbstractStackValue::externalJumpTarget()};
		entryState.stack.insert(entryState.stack.end(), params, AbstractStackValue::unknown());
		// Copied bytecode exposes PUSH tag immediates as data. Those tags may be
		// internal labels, so allow their real caller's stack below the synthetic
		// entry while still validating computed targets.
		entryState.unknownBelow = _copiedBytecodeReference;
		entryState.enteringFunction =
			_validateFunctionFrames && !_copiedBytecodeReference && (namedTagParams || namedTagReturns);
		mergeStateAt(_index, std::move(entryState));
	};
	for (auto const& [tagId, index]: tagToItemIndex)
	{
		if (_tagsReferencedFromOutside.count(tagId))
			seedExternalEntry(tagId, index, false);
		if (pendingError)
			break;
		if (
			!_tagsReferencedFromOutside.count(tagId) &&
			_copiedTagsReferencedFromOutside.count(tagId)
		)
			seedExternalEntry(tagId, index, true);
		if (pendingError)
			break;
	}

	for (size_t worklistIndex = 0; worklistIndex < worklist.size(); ++worklistIndex)
	{
		if (pendingError)
			return pendingError;
		size_t const itemIndex = worklist[worklistIndex].first;
		size_t const stateIndex = worklist[worklistIndex].second;
		if (stateIndex >= states[itemIndex].size())
			continue;
		AbstractStackState state = states[itemIndex][stateIndex];
		AssemblyItem const& item = _items[itemIndex];
		auto itemError = [&](std::string const& _message)
		{
			auto [name, data] = item.nameAndData();
			return data.empty() ?
				fmt::format("{} at assembly item {} ({}).", _message, itemIndex, name) :
				fmt::format("{} at assembly item {} ({} {}).", _message, itemIndex, name, data);
		};

		auto checkStackLimit = [&]() -> std::optional<std::string>
		{
			if (state.stack.size() > Assembly::StackLimit)
				return itemError("Stack too deep");
			return std::nullopt;
		};

			auto ensureDepth = [&](size_t _depth) -> bool
			{
				if (state.stack.size() >= _depth)
					return true;
				if (!state.unknownBelow)
					return false;
				size_t const insertedSlots = _depth - state.stack.size();
				state.stack.insert(
					state.stack.begin(),
					insertedSlots,
					AbstractStackValue::unknownCallerStackValue()
				);
				for (FunctionFrame& frame: state.functionFrames)
					frame.stackBase += insertedSlots;
				return true;
			};

			auto continueWith = [&](AbstractStackState _state)
			{
				mergeStateAt(itemIndex + 1, std::move(_state));
			};

			auto checkFunctionFrameAccess = [&](size_t _lowestAccessedIndex) -> std::optional<std::string>
			{
				if (
					_validateFunctionFrames &&
					!state.functionFrames.empty() &&
					_lowestAccessedIndex < state.functionFrames.back().stackBase
				)
					return itemError("Function body accesses below its stack frame");
				return std::nullopt;
			};

			if (item.type() == Tag)
			{
				if (item.data() <= std::numeric_limits<size_t>::max())
				{
					size_t const tagId = static_cast<size_t>(item.data());
					if (
						_allowUnresolvedDynamicJumpTargets &&
						_validateFunctionFrames &&
						tagsTargetedByIntoFunction.count(tagId) &&
						!state.enteredViaFunctionJump &&
						!state.enteringFunction &&
						!state.unknownBelow
					)
						continue;
					size_t const* namedTagParams = util::valueOrNullptr(_externalTagParams, tagId);
					size_t const* namedTagReturns = util::valueOrNullptr(_externalTagReturns, tagId);
					if (_validateFunctionFrames && state.enteringFunction && (namedTagParams || namedTagReturns))
					{
						size_t const params = namedTagParams ? *namedTagParams : 0;
						size_t const returns = namedTagReturns ? *namedTagReturns : 0;
						if (params >= Assembly::StackLimit)
							return itemError("Stack too deep");
						if (state.stack.size() < params + 1)
							return itemError("Named function entry has too few stack slots");

						size_t const stackBase = state.stack.size() - params - 1;
						FunctionFrame frame{
							stackBase,
							returns,
							functionReturnTarget(state.stack[stackBase])
						};
						if (!state.functionFrames.empty())
						{
							FunctionFrame& currentFrame = state.functionFrames.back();
							if (frame.stackBase < currentFrame.stackBase)
								return itemError("Named function entry uses a return target below the current stack frame");
							if (frame.stackBase == currentFrame.stackBase)
								currentFrame = frame;
							else
								state.functionFrames.push_back(frame);
						}
						else
							state.functionFrames.push_back(frame);
					}
				}
				state.enteredViaFunctionJump = false;
				state.enteringFunction = false;
				continueWith(std::move(state));
				continue;
			}
			state.enteredViaFunctionJump = false;

			if (item.type() == PushTag)
			{
				auto [subId, tagId] = item.splitForeignPushTag();
				state.stack.push_back(
					subId == std::numeric_limits<size_t>::max() ?
					AbstractStackValue::localTag(tagId) :
					AbstractStackValue::foreignTag(subId, tagId)
				);
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}
			if (item.type() == Push)
			{
				state.stack.push_back(AbstractStackValue::literal(item.data()));
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}
			if (item.type() == PushDeployTimeAddress)
			{
				state.stack.push_back(AbstractStackValue::currentAddress());
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}
			if (
				item.type() == PushData ||
				item.type() == PushSub ||
				item.type() == PushLibraryAddress
			)
			{
				state.stack.push_back(AbstractStackValue::foreignTag());
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}
			if (item.type() == PushSubSize || item.type() == PushProgramSize)
			{
				if (u512 const* pushedValue = item.pushedValue())
					state.stack.push_back(AbstractStackValue::literal(*pushedValue));
				else
					state.stack.push_back(AbstractStackValue::unknown());
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}
			if (item.type() == PushImmutable)
			{
				state.stack.push_back(AbstractStackValue::fullyTaintedReference());
				if (auto error = checkStackLimit())
					return error;
				continueWith(std::move(state));
				continue;
			}

					if (item.type() == Operation)
				{
					Instruction instruction = item.instruction();
				if (isDupInstruction(instruction))
				{
					size_t depth = getDupNumber(instruction);
					if (!ensureDepth(depth))
					{
						if (_pruneImpreciseInvalidStates && state.impreciseControlFlow)
							continue;
						return itemError("Stack underflow");
					}
					if (auto error = checkStackLimit())
						return error;
					if (auto error = checkFunctionFrameAccess(state.stack.size() - depth))
						return error;
					AbstractStackValue duplicatedValue = state.stack[state.stack.size() - depth];
					duplicatedValue.duplicateSourceBelowTop = depth;
					state.stack.push_back(std::move(duplicatedValue));
					if (auto error = checkStackLimit())
						return error;
					continueWith(std::move(state));
					continue;
				}
				if (isSwapInstruction(instruction))
				{
					size_t depth = getSwapNumber(instruction) + 1;
					if (!ensureDepth(depth))
					{
						if (_pruneImpreciseInvalidStates && state.impreciseControlFlow)
							continue;
						return itemError("Stack underflow");
					}
					if (auto error = checkStackLimit())
						return error;
					if (auto error = checkFunctionFrameAccess(state.stack.size() - depth))
						return error;
					std::swap(state.stack.back(), state.stack[state.stack.size() - depth]);
					continueWith(std::move(state));
					continue;
				}
			}

			size_t arguments = item.arguments();
			if (!ensureDepth(arguments))
			{
				if (_pruneImpreciseInvalidStates && state.impreciseControlFlow)
					continue;
				return itemError("Stack underflow");
			}
			if (auto error = checkStackLimit())
				return error;
			if (arguments > 0)
				if (auto error = checkFunctionFrameAccess(state.stack.size() - arguments))
					return error;

			auto stackArgument = [&](size_t _parameterIndex) -> AbstractStackValue const&
		{
			assertThrow(_parameterIndex < arguments, AssemblyException, "Invalid stack argument index.");
			return state.stack[state.stack.size() - 1 - _parameterIndex];
		};

		if (item.type() == AssignImmutable)
		{
			std::optional<size_t> occurrences = item.immutableOccurrences();
			assertThrow(
				occurrences || _allowUnsetImmutableOccurrences,
				AssemblyException,
				"Immutable occurrences not set."
			);
			// Before assembly has counted immutable references, use the maximum
			// stack pressure of this synthetic sequence for reachability scans.
			occurrences = occurrences.value_or(2);
			size_t const peakIncrease = *occurrences == 0 ? 0 : (*occurrences == 1 ? 1 : 3);
			if (state.stack.size() > Assembly::StackLimit - peakIncrease)
				return itemError("Stack too deep");
		}
		bool assignImmutableWritesMemory = false;
		bool assignImmutableMayWriteForeignToMemory = false;
		bool assignImmutableMayWriteLocalTagToMemory = false;
		bool assignImmutableMayWriteCurrentAddressToMemory = false;
		bool assignImmutableMayWriteComplementedCurrentAddressToMemory = false;
		bool assignImmutableMayWriteExactCurrentAddressToMemory = false;
		bool assignImmutableMayWriteExactComplementedCurrentAddressToMemory = false;
		if (item.type() == AssignImmutable)
		{
			std::optional<size_t> occurrences = item.immutableOccurrences();
			assertThrow(
				occurrences || _allowUnsetImmutableOccurrences,
				AssemblyException,
				"Immutable occurrences not set."
			);
			// optimiseInternal() can validate before immutable references are
			// counted. An unset count may later be zero, in which case this item
			// compiles to POP POP and does not write memory.
			assignImmutableWritesMemory = occurrences && *occurrences != 0;
			if (assignImmutableWritesMemory)
			{
				AbstractStackValue const& immutableSource = stackArgument(1);
				assignImmutableMayWriteForeignToMemory = immutableSource.mayBeForeignTag;
				assignImmutableMayWriteLocalTagToMemory =
					immutableSource.mayBeExternalJumpTarget ||
					immutableSource.mayBeUnknownLocalTag ||
					!immutableSource.localTags.empty();
				assignImmutableMayWriteCurrentAddressToMemory = valueMayBeCurrentAddressDerived(immutableSource);
				assignImmutableMayWriteComplementedCurrentAddressToMemory =
					valueMayBeComplementedCurrentAddressDerived(immutableSource);
				assignImmutableMayWriteExactCurrentAddressToMemory = valueMayBeExactCurrentAddress(immutableSource);
				assignImmutableMayWriteExactComplementedCurrentAddressToMemory =
					valueMayBeExactComplementedCurrentAddress(immutableSource);
			}
		}
		if (item.type() == VerbatimBytecode)
		{
			try
			{
				VerbatimStackEffect const effect = verbatimStackEffect(item.verbatimData(), "VERBATIM bytecode");
				assertThrow(
					effect.arguments == arguments && effect.returnValues == item.returnValues(),
					AssemblyException,
					"Invalid VERBATIM stack effect."
				);
				size_t const untouchedStackHeight = state.stack.size() - arguments;
				if (effect.peakHeight > Assembly::StackLimit - untouchedStackHeight)
					return itemError("Stack too deep");
			}
			catch (AssemblyImportException const&)
			{
				return itemError("Invalid VERBATIM bytecode");
			}
		}

		bool foreignInput = false;
		bool externalJumpTargetInput = false;
		bool localTagInput = false;
		bool localTagDerivedInput = false;
		bool currentAddressInput = false;
		bool complementedCurrentAddressInput = false;
		bool exactCurrentAddressInput = false;
		bool exactComplementedCurrentAddressInput = false;
		for (size_t i = 0; i < arguments; ++i)
		{
			foreignInput = foreignInput || state.stack[state.stack.size() - 1 - i].mayBeForeignTag;
			externalJumpTargetInput =
				externalJumpTargetInput ||
				state.stack[state.stack.size() - 1 - i].mayBeExternalJumpTarget;
			localTagInput =
				localTagInput ||
				state.stack[state.stack.size() - 1 - i].mayBeExternalJumpTarget ||
				state.stack[state.stack.size() - 1 - i].mayBeUnknownLocalTag ||
				!state.stack[state.stack.size() - 1 - i].localTags.empty();
			localTagDerivedInput =
				localTagDerivedInput ||
				state.stack[state.stack.size() - 1 - i].mayBeUnknownLocalTag ||
				!state.stack[state.stack.size() - 1 - i].localTags.empty();
			currentAddressInput =
				currentAddressInput ||
				valueMayBeCurrentAddressDerived(state.stack[state.stack.size() - 1 - i]);
			complementedCurrentAddressInput =
				complementedCurrentAddressInput ||
				valueMayBeComplementedCurrentAddressDerived(state.stack[state.stack.size() - 1 - i]);
			exactCurrentAddressInput =
				exactCurrentAddressInput ||
				valueMayBeExactCurrentAddress(state.stack[state.stack.size() - 1 - i]);
			exactComplementedCurrentAddressInput =
				exactComplementedCurrentAddressInput ||
				valueMayBeExactComplementedCurrentAddress(state.stack[state.stack.size() - 1 - i]);
		}
		AbstractStackValue jumpTarget = arguments > 0 ? state.stack.back() : AbstractStackValue::unknown();
		bool const isJump =
			item.type() == Operation &&
			(item.instruction() == Instruction::JUMP || item.instruction() == Instruction::JUMPI);
		bool const isConditionalJump =
			item.type() == Operation &&
			item.instruction() == Instruction::JUMPI;
		std::optional<AbstractStackValue> jumpCondition;
		if (isConditionalJump && arguments > 1)
			jumpCondition = state.stack[state.stack.size() - 2];
		bool const jumpMayBeTaken =
			isJump &&
			(
				!jumpCondition ||
				!jumpCondition->literalValue ||
				*jumpCondition->literalValue != 0
			);
		bool const conditionalJumpMayFallThrough =
			isConditionalJump &&
			(!jumpCondition || !jumpCondition->literalValue || *jumpCondition->literalValue == 0);
		bool const conditionalJumpHasDynamicCondition =
			isConditionalJump &&
			(!jumpCondition || !jumpCondition->literalValue);
		bool const jumpTargetCarriesAddressTaint =
			valueMayBeCurrentAddressDerived(jumpTarget) ||
			valueMayBeComplementedCurrentAddressDerived(jumpTarget);

			if (
				jumpMayBeTaken &&
				arguments > 0 &&
				jumpTarget.mayBeForeignTag
			)
			{
				if (
					_pruneImpreciseInvalidStates &&
					state.impreciseControlFlow &&
					(state.unknownBelow || !jumpTargetCarriesAddressTaint)
				)
					continue;
				return itemError("Foreign tag cannot be used as local jump target");
			}
			if (
				jumpMayBeTaken &&
				arguments > 0 &&
				jumpTarget.mayBeUnknownLocalTag &&
				!_allowUnresolvedDynamicJumpTargets
			)
			{
				if (_pruneImpreciseInvalidStates && state.impreciseControlFlow)
					continue;
				return itemError("Local jump target cannot be validated");
			}
			if (
				jumpMayBeTaken &&
				arguments > 0 &&
				jumpTarget.mayBeExternalJumpTarget &&
				item.getJumpType() != AssemblyItem::JumpType::OutOfFunction
			)
			{
				return itemError("External jump target requires out-of-function jump");
			}
				if (
					jumpMayBeTaken &&
					arguments > 0 &&
						jumpTarget.localTags.empty() &&
						!jumpTarget.literalValue &&
						item.getJumpType() == AssemblyItem::JumpType::OutOfFunction &&
						!jumpTarget.mustBeExternalJumpTarget &&
						!jumpTarget.mayBeUnknownCallerStackValue &&
						!_allowUnresolvedDynamicJumpTargets
					)
					{
						if (
							_pruneImpreciseInvalidStates &&
							state.impreciseControlFlow &&
							(state.unknownBelow || !jumpTargetCarriesAddressTaint)
						)
							continue;
						return itemError("Dynamic out-of-function jump target cannot be validated");
					}
		if (
			jumpMayBeTaken &&
			item.getJumpType() == AssemblyItem::JumpType::OutOfFunction &&
			_validateFunctionFrames &&
			!state.functionFrames.empty()
		)
		{
			FunctionFrame const& currentFrame = state.functionFrames.back();
			size_t const returnedSlots = state.stack.size() - arguments - currentFrame.stackBase;
			if (returnedSlots != currentFrame.returnSlots)
				return itemError("Out-of-function jump returns wrong number of stack slots");
			if (arguments == 0 || !outOfFunctionTargetMatchesReturnTarget(jumpTarget, currentFrame.returnTarget))
				return itemError("Out-of-function jump targets a different return location");
		}

		std::optional<size_t> literalJumpTargetItemIndex;
		if (
			_rejectLiteralJumpTargetsWithoutMap &&
			!_literalJumpTargets &&
			jumpMayBeTaken &&
			jumpTarget.literalValue
		)
			return itemError("Literal jump target cannot be preserved by layout-changing optimisation");
		if (
			_literalJumpTargets &&
			jumpMayBeTaken &&
			jumpTarget.literalValue
		)
		{
			u512 const& target = *jumpTarget.literalValue;
			if (
				target > std::numeric_limits<size_t>::max() ||
				!_literalJumpTargets->count(static_cast<size_t>(target))
			)
				return itemError("Literal jump target is not an assembly item");
			size_t const targetItemIndex = _literalJumpTargets->at(static_cast<size_t>(target));
			if (targetItemIndex >= _items.size())
				return itemError("Literal jump target is not an assembly item");
			AssemblyItem const& targetItem = _items[targetItemIndex];
			if (
				targetItem.type() != Tag &&
				!(targetItem.type() == Operation && targetItem.instruction() == Instruction::JUMPDEST)
			)
				return itemError("Literal jump target is not a jump destination");
			literalJumpTargetItemIndex = targetItemIndex;
		}

		auto recordUsedForeignTags = [&](AbstractStackValue const& _value)
		{
			if (_usedForeignTags)
				_usedForeignTags->insert(_value.foreignTags.begin(), _value.foreignTags.end());
		};
		if (_usedForeignTags)
		{
			if (isJump)
			{
				if (jumpMayBeTaken && arguments > 0)
					recordUsedForeignTags(jumpTarget);
				if (jumpCondition)
					recordUsedForeignTags(*jumpCondition);
			}
			else
				for (size_t i = 0; i < arguments; ++i)
					recordUsedForeignTags(stackArgument(i));
		}

		CodeCopyReadTaints const codeCopyReadTaints =
			item.type() == Operation &&
			item.instruction() == Instruction::CODECOPY &&
			arguments >= 3 ?
			codeCopyMayReadTaints(stackArgument(1), stackArgument(2), _codeCopyTaintRanges) :
			(
				item.type() == Operation &&
				item.instruction() == Instruction::EXTCODECOPY &&
				arguments >= 4 ?
				extCodeCopyMayReadTaints(
					stackArgument(0),
					stackArgument(2),
					stackArgument(3),
					_codeCopyTaintRanges
				) :
				CodeCopyReadTaints{}
			);
		if (_copiedForeignTags)
			_copiedForeignTags->insert(codeCopyReadTaints.foreignTags.begin(), codeCopyReadTaints.foreignTags.end());
		bool const returnDataCopyReadsForeignReference =
			item.type() == Operation &&
			item.instruction() == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			state.returnDataMayContainForeignTag &&
			valueMayBeNonZero(stackArgument(2));
			bool const returnDataCopyReadsLocalTag =
				item.type() == Operation &&
				item.instruction() == Instruction::RETURNDATACOPY &&
				arguments >= 3 &&
				state.returnDataMayContainLocalTag &&
				valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsCurrentAddress =
			item.type() == Operation &&
			item.instruction() == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			state.returnDataMayContainCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsComplementedCurrentAddress =
			item.type() == Operation &&
			item.instruction() == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			state.returnDataMayContainComplementedCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsExactCurrentAddress =
			item.type() == Operation &&
			item.instruction() == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			state.returnDataMayContainExactCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const returnDataCopyReadsExactComplementedCurrentAddress =
			item.type() == Operation &&
			item.instruction() == Instruction::RETURNDATACOPY &&
			arguments >= 3 &&
			state.returnDataMayContainExactComplementedCurrentAddress &&
			valueMayBeNonZero(stackArgument(2));
		bool const operationWritesForeignToMemory =
			item.type() == Operation &&
			memoryStoreValueMay(item.instruction(), arguments, stackArgument, [](AbstractStackValue const& _value) {
				return _value.mayBeForeignTag;
			});
		bool const operationWritesLocalTagToMemory =
			item.type() == Operation &&
			memoryStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeLocalTag);
		bool const operationWritesCurrentAddressToMemory =
			item.type() == Operation &&
			memoryStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeCurrentAddressDerived);
		bool const operationWritesComplementedCurrentAddressToMemory =
			item.type() == Operation &&
			memoryStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived);
		std::optional<AddressWordStore> const operationWritesExactCurrentAddressWordToMemory =
			item.type() == Operation ?
			memoryStoreAddressWordStore(item.instruction(), arguments, stackArgument, valueMayBeExactCurrentAddress) :
			std::optional<AddressWordStore>{};
		std::optional<AddressWordStore> const operationWritesExactComplementedCurrentAddressWordToMemory =
			item.type() == Operation ?
			memoryStoreAddressWordStore(
				item.instruction(),
				arguments,
				stackArgument,
				valueMayBeExactComplementedCurrentAddress
			) :
			std::optional<AddressWordStore>{};
		bool const operationWritesForeignToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, [](AbstractStackValue const& _value) {
				return _value.mayBeForeignTag;
			});
		bool const operationWritesLocalTagToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeLocalTag);
		bool const operationWritesCurrentAddressToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeCurrentAddressDerived);
		bool const operationWritesComplementedCurrentAddressToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived);
		bool const operationWritesExactCurrentAddressToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeExactCurrentAddress);
		bool const operationWritesExactComplementedCurrentAddressToStorage =
			item.type() == Operation &&
			storageStoreValueMay(item.instruction(), arguments, stackArgument, valueMayBeExactComplementedCurrentAddress);
		std::optional<u512> const operationCurrentAddressOffsetValue =
			item.type() == Operation ?
			operationCurrentAddressOffset(item.instruction(), arguments, stackArgument) :
			std::optional<u512>{};
		std::optional<u512> const operationCurrentAddressXorMaskValue =
			item.type() == Operation ?
			operationCurrentAddressXorMask(item.instruction(), arguments, stackArgument) :
			std::optional<u512>{};
		std::optional<u512> const operationComplementedCurrentAddressOffsetValue =
			item.type() == Operation ?
			operationComplementedCurrentAddressOffset(item.instruction(), arguments, stackArgument) :
			std::optional<u512>{};
		bool const operationMayProduceCurrentAddressValue =
			item.type() == Operation &&
			operationMayProduceCurrentAddress(item.instruction(), arguments, stackArgument);
		bool const operationMayProduceComplementedCurrentAddressValue =
			item.type() == Operation &&
			operationMayProduceComplementedCurrentAddress(item.instruction(), arguments, stackArgument);
		bool const operationMayProduceCurrentAddressDerivedValue =
			item.type() == Operation &&
			(
				identityOperationMayPreserveValue(item.instruction(), arguments, stackArgument, valueMayBeCurrentAddressDerived) ||
				operationMayComplementValue(item.instruction(), arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived)
			);
			bool const operationMayProduceComplementedCurrentAddressDerivedValue =
				item.type() == Operation &&
				(
					identityOperationMayPreserveValue(item.instruction(), arguments, stackArgument, valueMayBeComplementedCurrentAddressDerived) ||
					operationMayComplementValue(item.instruction(), arguments, stackArgument, valueMayBeCurrentAddressDerived)
				);
			std::set<u512> const operationPossibleCurrentAddressOffsetsValue =
				item.type() == Operation ?
				operationPossibleCurrentAddressOffsets(item.instruction(), arguments, stackArgument) :
				std::set<u512>{};
			std::optional<size_t> const operationCurrentAddressByteIndexValue =
				item.type() == Operation ?
				operationCurrentAddressByteIndex(item.instruction(), arguments, stackArgument) :
				std::optional<size_t>{};
		std::optional<size_t> const operationComplementedCurrentAddressByteIndexValue =
			item.type() == Operation ?
			operationComplementedCurrentAddressByteIndex(item.instruction(), arguments, stackArgument) :
			std::optional<size_t>{};
		std::optional<u512> const operationInputIndependentLiteralValue =
			item.type() == Operation ?
			operationInputIndependentLiteral(item.instruction(), arguments, stackArgument) :
			std::optional<u512>{};
		bool const operationOutputCanPropagateStackInputValue =
			item.type() == Operation &&
			operationOutputCanPropagateStackInput(item.instruction(), arguments, stackArgument);
		std::optional<AddressByteStore> const operationWritesCurrentAddressByteToMemory =
			item.type() == Operation ?
			memoryStoreAddressByteStore(
				item.instruction(),
				arguments,
				stackArgument,
				&AbstractStackValue::currentAddressByteIndex,
				valueMayBeExactCurrentAddress
			) :
			std::optional<AddressByteStore>{};
		std::optional<AddressByteStore> const operationWritesComplementedCurrentAddressByteToMemory =
			item.type() == Operation ?
			memoryStoreAddressByteStore(
				item.instruction(),
				arguments,
				stackArgument,
				&AbstractStackValue::complementedCurrentAddressByteIndex,
				valueMayBeExactComplementedCurrentAddress
			) :
			std::optional<AddressByteStore>{};
		std::vector<AddressByteStore> const operationWritesCurrentAddressStackBytesToMemory =
			item.type() == Operation ?
			memoryStoreAddressStackByteStores(
				item.instruction(),
				arguments,
				stackArgument,
				currentAddressStackBytes
			) :
			std::vector<AddressByteStore>{};
		std::vector<AddressByteStore> const operationWritesComplementedCurrentAddressStackBytesToMemory =
			item.type() == Operation ?
			memoryStoreAddressStackByteStores(
				item.instruction(),
				arguments,
				stackArgument,
				complementedCurrentAddressStackBytes
			) :
			std::vector<AddressByteStore>{};
		bool const operationLoadsCurrentAddressFromBytes =
			item.type() == Operation &&
			item.instruction() == Instruction::MLOAD &&
			arguments >= 1 &&
			addressByteMapMayLoadWord(state.memoryCurrentAddressBytes, stackArgument(0));
		bool const operationLoadsComplementedCurrentAddressFromBytes =
			item.type() == Operation &&
			item.instruction() == Instruction::MLOAD &&
			arguments >= 1 &&
			addressByteMapMayLoadWord(state.memoryComplementedCurrentAddressBytes, stackArgument(0));
			AbstractStackValue identityPreservedMetadata;
			if (item.type() == Operation)
				propagateIdentityPreservedMetadata(item.instruction(), arguments, stackArgument, identityPreservedMetadata);
			std::optional<size_t> isZeroOfValueBelow;
			if (
				item.type() == Operation &&
				item.instruction() == Instruction::ISZERO &&
				arguments == 1
			)
				isZeroOfValueBelow = stackArgument(0).duplicateSourceBelowTop;
			bool callMayWriteForeignMemoryToStorage = false;
			bool callMayWriteLocalTagToStorage = false;
			bool callMayWriteForeignReturnDataToMemory = false;
			bool callMayWriteForeignToReturnData = false;
			bool callMayWriteLocalTagReturnDataToMemory = false;
			bool callMayWriteLocalTagToReturnData = false;
			bool callMayWriteCurrentAddressReturnDataToMemory = false;
			bool callMayWriteCurrentAddressToReturnData = false;
			bool callMayWriteComplementedCurrentAddressReturnDataToMemory = false;
			bool callMayWriteComplementedCurrentAddressToReturnData = false;
			bool callMayWriteExactCurrentAddressReturnDataToMemory = false;
			bool callMayWriteExactCurrentAddressToReturnData = false;
			bool callMayWriteExactComplementedCurrentAddressReturnDataToMemory = false;
			bool callMayWriteExactComplementedCurrentAddressToReturnData = false;
			bool callMayWriteCurrentAddressToStorage = false;
			bool callMayWriteComplementedCurrentAddressToStorage = false;
			bool callMayWriteExactCurrentAddressToStorage = false;
			bool callMayWriteExactComplementedCurrentAddressToStorage = false;
			bool callMayOverwriteReturnData = false;
		if (item.type() == Operation)
		{
			Instruction instruction = item.instruction();
			std::optional<size_t> addressParameter = callAddressParameter(instruction);
			bool const callMayExecuteCurrentCode =
				addressParameter &&
				*addressParameter < arguments &&
				valueMayBeExactCurrentAddress(stackArgument(*addressParameter));
				std::optional<size_t> inputLengthParameter = callInputLengthParameter(instruction);
				bool const callInputMayContainForeign =
					state.memoryMayContainForeignTag &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				bool const callInputMayContainLocalTag =
					state.memoryMayContainLocalTag &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				bool const createMayExecuteInitCode =
					isCreateInstruction(instruction) &&
					inputLengthParameter &&
					*inputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*inputLengthParameter));
				if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode) && callInputMayContainForeign)
					callMayWriteForeignMemoryToStorage = true;
				if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode) && callInputMayContainLocalTag)
					callMayWriteLocalTagToStorage = true;
				if (externalInstructionMayReenterAndWriteCurrentStorage(instruction, createMayExecuteInitCode))
				{
					callMayWriteForeignMemoryToStorage = true;
					callMayWriteLocalTagToStorage = true;
					callMayWriteCurrentAddressToStorage = true;
					callMayWriteComplementedCurrentAddressToStorage = true;
					callMayWriteExactCurrentAddressToStorage = true;
					callMayWriteExactComplementedCurrentAddressToStorage = true;
				}

				callMayOverwriteReturnData = callMayWriteReturnDataToMemory(instruction);
				if (callMayOverwriteReturnData)
				{
					bool const callMayExposeForeignToReturnData =
						callMayExecuteCurrentCode ||
						(instruction == Instruction::DELEGATECALL && (state.storageMayContainForeignTag || callInputMayContainForeign)) ||
						(isCreateInstruction(instruction) && callInputMayContainForeign);
					bool const callMayExposeLocalTagToReturnData =
						callMayExecuteCurrentCode ||
						(instruction == Instruction::DELEGATECALL && (state.storageMayContainLocalTag || callInputMayContainLocalTag)) ||
						(isCreateInstruction(instruction) && callInputMayContainLocalTag);
					bool const callMayExposeCurrentAddressToReturnData = true;
					bool const callMayExposeComplementedCurrentAddressToReturnData = true;
					bool const callMayExposeExactCurrentAddressToReturnData = true;
					bool const callMayExposeExactComplementedCurrentAddressToReturnData = true;
					callMayWriteForeignToReturnData = callMayExposeForeignToReturnData;
					callMayWriteLocalTagToReturnData = callMayExposeLocalTagToReturnData;
					callMayWriteCurrentAddressToReturnData = callMayExposeCurrentAddressToReturnData;
					callMayWriteComplementedCurrentAddressToReturnData =
						callMayExposeComplementedCurrentAddressToReturnData;
					callMayWriteExactCurrentAddressToReturnData = callMayExposeExactCurrentAddressToReturnData;
					callMayWriteExactComplementedCurrentAddressToReturnData =
						callMayExposeExactComplementedCurrentAddressToReturnData;

				std::optional<size_t> outputLengthParameter = callOutputLengthParameter(instruction);
				if (
					callMayExposeForeignToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteForeignReturnDataToMemory = true;
				if (
					callMayExposeLocalTagToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteLocalTagReturnDataToMemory = true;
				if (
					callMayExposeCurrentAddressToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteCurrentAddressReturnDataToMemory = true;
				if (
					callMayExposeExactCurrentAddressToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteExactCurrentAddressReturnDataToMemory = true;
				if (
					callMayExposeComplementedCurrentAddressToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteComplementedCurrentAddressReturnDataToMemory = true;
				if (
					callMayExposeExactComplementedCurrentAddressToReturnData &&
					outputLengthParameter &&
					*outputLengthParameter < arguments &&
					valueMayBeNonZero(stackArgument(*outputLengthParameter))
				)
					callMayWriteExactComplementedCurrentAddressReturnDataToMemory = true;
			}
			if (callMayExecuteCurrentCode && callMayWriteCurrentStorage(instruction))
				callMayWriteForeignMemoryToStorage = true;
			if (callMayExecuteCurrentCode && callMayWriteCurrentStorage(instruction))
				callMayWriteLocalTagToStorage = true;
			if (callWithInputMayWriteCurrentStorage(instruction, callMayExecuteCurrentCode))
			{
				callMayWriteCurrentAddressToStorage = true;
				callMayWriteComplementedCurrentAddressToStorage = true;
				callMayWriteExactCurrentAddressToStorage = true;
				callMayWriteExactComplementedCurrentAddressToStorage = true;
			}
		}

		AddressByteMapInvalidation addressByteMapInvalidation;
		if (item.type() == Operation)
			addressByteMapInvalidation = invalidateAddressByteMapsForMemoryWrite(
				item.instruction(),
				arguments,
				stackArgument,
				state.memoryCurrentAddressBytes,
				state.memoryComplementedCurrentAddressBytes
			);

		std::vector<AbstractStackValue> verbatimInputStack;
		if (item.type() == VerbatimBytecode)
			verbatimInputStack.assign(
				state.stack.end() - static_cast<std::vector<AbstractStackValue>::difference_type>(arguments),
				state.stack.end()
			);

		state.stack.erase(state.stack.end() - static_cast<std::vector<AbstractStackValue>::difference_type>(arguments), state.stack.end());
		{
			for (AbstractStackValue& value: state.stack)
				value.duplicateSourceBelowTop.reset();
		}

			VerbatimTaintInfo verbatimTaint;
			if (item.type() == VerbatimBytecode)
				verbatimTaint = verbatimTaintInfo(item.verbatimData());
			VerbatimCallEffects verbatimCalls;
			if (item.type() == VerbatimBytecode)
				verbatimCalls = analyzeVerbatimCallEffects(
					item.verbatimData(),
					verbatimInputStack,
					state.memoryMayContainForeignTag,
					state.storageMayContainForeignTag,
					state.memoryMayContainLocalTag,
					state.storageMayContainLocalTag,
					state.returnDataMayContainForeignTag,
					state.returnDataMayContainLocalTag,
					state.returnDataMayContainCurrentAddress,
					state.returnDataMayContainComplementedCurrentAddress,
					state.memoryMayContainCurrentAddress,
					state.storageMayContainCurrentAddress,
					state.memoryMayContainComplementedCurrentAddress,
					state.storageMayContainComplementedCurrentAddress,
					state.returnDataMayContainExactCurrentAddress,
					state.returnDataMayContainExactComplementedCurrentAddress,
					state.memoryMayContainExactCurrentAddress,
					state.storageMayContainExactCurrentAddress,
					state.memoryMayContainExactComplementedCurrentAddress,
					state.storageMayContainExactComplementedCurrentAddress,
					state.memoryCurrentAddressBytes,
					state.memoryComplementedCurrentAddressBytes,
					_codeCopyTaintRanges
				);
			CodeCopyReadTaints const verbatimCopiedTaints =
				item.type() == VerbatimBytecode ?
				verbatimMayCopyTaints(item.verbatimData(), std::move(verbatimInputStack), _codeCopyTaintRanges) :
				CodeCopyReadTaints{};
			if (_copiedForeignTags)
				_copiedForeignTags->insert(verbatimCopiedTaints.foreignTags.begin(), verbatimCopiedTaints.foreignTags.end());
		bool const verbatimReadsForeign =
			item.type() == VerbatimBytecode &&
			(
				(
					verbatimTaint.readsMemory &&
					(
						state.memoryMayContainForeignTag ||
						verbatimCopiedTaints.mayReadForeignReference ||
						verbatimCalls.mayWriteForeignToMemory
					)
					) ||
					(
						verbatimTaint.readsStorage &&
						(state.storageMayContainForeignTag || verbatimCalls.mayWriteForeignToStorage)
					)
				);
			bool const verbatimReadsLocalTag =
				item.type() == VerbatimBytecode &&
				(
					(
						verbatimTaint.readsMemory &&
						(
							state.memoryMayContainLocalTag ||
							verbatimCopiedTaints.mayReadLocalTag ||
							verbatimCalls.mayWriteLocalTagToMemory
						)
					) ||
					(
						verbatimTaint.readsStorage &&
						(state.storageMayContainLocalTag || verbatimCalls.mayWriteLocalTagToStorage)
					)
				);
			if (item.type() == Operation)
			{
				if (operationWritesForeignToMemory)
					state.memoryMayContainForeignTag = true;
				if (operationWritesLocalTagToMemory)
					state.memoryMayContainLocalTag = true;
				if (operationWritesCurrentAddressToMemory)
					state.memoryMayContainCurrentAddress = true;
				if (operationWritesExactCurrentAddressWordToMemory)
				{
					if (
						recordAddressWordStore(
							state.memoryCurrentAddressBytes,
							*operationWritesExactCurrentAddressWordToMemory
						)
					)
						state.memoryMayContainExactCurrentAddress = true;
				}
				if (
					operationWritesCurrentAddressByteToMemory &&
					recordAddressByteStore(state.memoryCurrentAddressBytes, *operationWritesCurrentAddressByteToMemory)
				)
				{
					state.memoryMayContainCurrentAddress = true;
					state.memoryMayContainExactCurrentAddress = true;
				}
				if (recordAddressByteStores(state.memoryCurrentAddressBytes, operationWritesCurrentAddressStackBytesToMemory))
				{
					state.memoryMayContainCurrentAddress = true;
					state.memoryMayContainExactCurrentAddress = true;
				}
				if (operationWritesComplementedCurrentAddressToMemory)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (operationWritesExactComplementedCurrentAddressWordToMemory)
				{
					if (
						recordAddressWordStore(
							state.memoryComplementedCurrentAddressBytes,
							*operationWritesExactComplementedCurrentAddressWordToMemory
						)
					)
						state.memoryMayContainExactComplementedCurrentAddress = true;
				}
				if (
					operationWritesComplementedCurrentAddressByteToMemory &&
					recordAddressByteStore(
						state.memoryComplementedCurrentAddressBytes,
						*operationWritesComplementedCurrentAddressByteToMemory
					)
				)
				{
					state.memoryMayContainComplementedCurrentAddress = true;
					state.memoryMayContainExactComplementedCurrentAddress = true;
				}
				if (
					recordAddressByteStores(
						state.memoryComplementedCurrentAddressBytes,
						operationWritesComplementedCurrentAddressStackBytesToMemory
					)
				)
				{
					state.memoryMayContainComplementedCurrentAddress = true;
					state.memoryMayContainExactComplementedCurrentAddress = true;
				}
				if (codeCopyReadTaints.mayReadForeignReference)
					state.memoryMayContainForeignTag = true;
				if (codeCopyReadTaints.mayReadLocalTag)
					state.memoryMayContainLocalTag = true;
				if (codeCopyReadTaints.mayReadCurrentAddress)
					state.memoryMayContainCurrentAddress = true;
				if (codeCopyReadTaints.mayReadCurrentAddress)
					state.memoryMayContainExactCurrentAddress = true;
				if (codeCopyReadTaints.mayReadComplementedCurrentAddress)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (codeCopyReadTaints.mayReadComplementedCurrentAddress)
					state.memoryMayContainExactComplementedCurrentAddress = true;
				if (returnDataCopyReadsForeignReference)
					state.memoryMayContainForeignTag = true;
				if (callMayWriteForeignReturnDataToMemory)
					state.memoryMayContainForeignTag = true;
				if (returnDataCopyReadsLocalTag)
					state.memoryMayContainLocalTag = true;
				if (callMayWriteLocalTagReturnDataToMemory)
					state.memoryMayContainLocalTag = true;
				if (returnDataCopyReadsCurrentAddress)
					state.memoryMayContainCurrentAddress = true;
				if (callMayWriteCurrentAddressReturnDataToMemory)
					state.memoryMayContainCurrentAddress = true;
				if (addressByteMapInvalidation.currentAddressPrecisionLost)
				{
					state.memoryMayContainCurrentAddress = true;
					state.memoryMayContainExactCurrentAddress = true;
				}
				if (returnDataCopyReadsExactCurrentAddress)
					state.memoryMayContainExactCurrentAddress = true;
				if (callMayWriteExactCurrentAddressReturnDataToMemory)
					state.memoryMayContainExactCurrentAddress = true;
				if (returnDataCopyReadsComplementedCurrentAddress)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (callMayWriteComplementedCurrentAddressReturnDataToMemory)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (addressByteMapInvalidation.complementedCurrentAddressPrecisionLost)
				{
					state.memoryMayContainComplementedCurrentAddress = true;
					state.memoryMayContainExactComplementedCurrentAddress = true;
				}
				if (returnDataCopyReadsExactComplementedCurrentAddress)
					state.memoryMayContainExactComplementedCurrentAddress = true;
				if (callMayWriteExactComplementedCurrentAddressReturnDataToMemory)
					state.memoryMayContainExactComplementedCurrentAddress = true;
				if (operationWritesForeignToStorage)
					state.storageMayContainForeignTag = true;
				if (operationWritesLocalTagToStorage)
					state.storageMayContainLocalTag = true;
				if (operationWritesCurrentAddressToStorage)
					state.storageMayContainCurrentAddress = true;
				if (operationWritesExactCurrentAddressToStorage)
					state.storageMayContainExactCurrentAddress = true;
				if (operationWritesComplementedCurrentAddressToStorage)
					state.storageMayContainComplementedCurrentAddress = true;
				if (operationWritesExactComplementedCurrentAddressToStorage)
					state.storageMayContainExactComplementedCurrentAddress = true;
				if (callMayWriteForeignMemoryToStorage)
					state.storageMayContainForeignTag = true;
				if (callMayWriteLocalTagToStorage)
					state.storageMayContainLocalTag = true;
				if (callMayWriteCurrentAddressToStorage)
					state.storageMayContainCurrentAddress = true;
				if (callMayWriteExactCurrentAddressToStorage)
					state.storageMayContainExactCurrentAddress = true;
				if (callMayWriteComplementedCurrentAddressToStorage)
					state.storageMayContainComplementedCurrentAddress = true;
				if (callMayWriteExactComplementedCurrentAddressToStorage)
					state.storageMayContainExactComplementedCurrentAddress = true;
				if (callMayOverwriteReturnData)
				{
					state.returnDataMayContainForeignTag = callMayWriteForeignToReturnData;
					state.returnDataMayContainLocalTag = callMayWriteLocalTagToReturnData;
					state.returnDataMayContainCurrentAddress = callMayWriteCurrentAddressToReturnData;
					state.returnDataMayContainComplementedCurrentAddress =
						callMayWriteComplementedCurrentAddressToReturnData;
					state.returnDataMayContainExactCurrentAddress = callMayWriteExactCurrentAddressToReturnData;
					state.returnDataMayContainExactComplementedCurrentAddress =
						callMayWriteExactComplementedCurrentAddressToReturnData;
				}
			}
			else if (item.type() == AssignImmutable)
			{
				if (assignImmutableWritesMemory)
				{
					if (!state.memoryCurrentAddressBytes.empty())
					{
						state.memoryMayContainCurrentAddress = true;
						state.memoryMayContainExactCurrentAddress = true;
					}
					if (!state.memoryComplementedCurrentAddressBytes.empty())
					{
						state.memoryMayContainComplementedCurrentAddress = true;
						state.memoryMayContainExactComplementedCurrentAddress = true;
					}
					clearAddressByteMaps(state.memoryCurrentAddressBytes, state.memoryComplementedCurrentAddressBytes);
				}
				if (assignImmutableMayWriteForeignToMemory)
					state.memoryMayContainForeignTag = true;
				if (assignImmutableMayWriteLocalTagToMemory)
					state.memoryMayContainLocalTag = true;
				if (assignImmutableMayWriteCurrentAddressToMemory)
					state.memoryMayContainCurrentAddress = true;
				if (assignImmutableMayWriteComplementedCurrentAddressToMemory)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (assignImmutableMayWriteExactCurrentAddressToMemory)
					state.memoryMayContainExactCurrentAddress = true;
				if (assignImmutableMayWriteExactComplementedCurrentAddressToMemory)
					state.memoryMayContainExactComplementedCurrentAddress = true;
			}
			else if (item.type() == VerbatimBytecode)
			{
				if (
					verbatimCopiedTaints.mayReadForeignReference ||
					verbatimCalls.mayWriteForeignToMemory
				)
					state.memoryMayContainForeignTag = true;
				if (
					verbatimCopiedTaints.mayReadLocalTag ||
					verbatimCalls.mayWriteLocalTagToMemory
				)
					state.memoryMayContainLocalTag = true;
				if (verbatimCopiedTaints.mayReadCurrentAddress || verbatimCalls.mayWriteCurrentAddressToMemory)
					state.memoryMayContainCurrentAddress = true;
				if (verbatimCopiedTaints.mayReadCurrentAddress || verbatimCalls.mayWriteExactCurrentAddressToMemory)
					state.memoryMayContainExactCurrentAddress = true;
				if (
					verbatimCopiedTaints.mayReadComplementedCurrentAddress ||
					verbatimCalls.mayWriteComplementedCurrentAddressToMemory
				)
					state.memoryMayContainComplementedCurrentAddress = true;
				if (
					verbatimCopiedTaints.mayReadComplementedCurrentAddress ||
					verbatimCalls.mayWriteExactComplementedCurrentAddressToMemory
				)
					state.memoryMayContainExactComplementedCurrentAddress = true;
				if (verbatimCalls.memoryAddressByteMapsKnown)
				{
					state.memoryCurrentAddressBytes = std::move(verbatimCalls.memoryCurrentAddressBytes);
					state.memoryComplementedCurrentAddressBytes =
						std::move(verbatimCalls.memoryComplementedCurrentAddressBytes);
				}
				if (verbatimCalls.mayWriteForeignToStorage)
					state.storageMayContainForeignTag = true;
				if (verbatimCalls.mayWriteLocalTagToStorage)
					state.storageMayContainLocalTag = true;
				if (verbatimCalls.mayWriteCurrentAddressToStorage)
					state.storageMayContainCurrentAddress = true;
				if (verbatimCalls.mayWriteExactCurrentAddressToStorage)
					state.storageMayContainExactCurrentAddress = true;
				if (verbatimCalls.mayWriteComplementedCurrentAddressToStorage)
					state.storageMayContainComplementedCurrentAddress = true;
				if (verbatimCalls.mayWriteExactComplementedCurrentAddressToStorage)
					state.storageMayContainExactComplementedCurrentAddress = true;
				state.returnDataMayContainForeignTag = verbatimCalls.returnDataMayContainForeignTag;
				state.returnDataMayContainLocalTag = verbatimCalls.returnDataMayContainLocalTag;
				state.returnDataMayContainCurrentAddress = verbatimCalls.returnDataMayContainCurrentAddress;
				state.returnDataMayContainComplementedCurrentAddress =
					verbatimCalls.returnDataMayContainComplementedCurrentAddress;
				state.returnDataMayContainExactCurrentAddress = verbatimCalls.returnDataMayContainExactCurrentAddress;
				state.returnDataMayContainExactComplementedCurrentAddress =
					verbatimCalls.returnDataMayContainExactComplementedCurrentAddress;
			}

			if (isJump)
		{
			if (jumpMayBeTaken)
			{
				AbstractStackState targetState = state;
				if (
					item.getJumpType() == AssemblyItem::JumpType::OutOfFunction &&
					_validateFunctionFrames &&
					!targetState.functionFrames.empty()
				)
					targetState.functionFrames.pop_back();
				if (jumpCondition)
					refineIsZeroTestedValueToZero(targetState, *jumpCondition);
				targetState.enteredViaFunctionJump =
					_validateFunctionFrames &&
					item.getJumpType() == AssemblyItem::JumpType::IntoFunction;
				targetState.enteringFunction =
					_validateFunctionFrames &&
					item.getJumpType() == AssemblyItem::JumpType::IntoFunction &&
					!targetState.functionFrames.empty();
				if (conditionalJumpHasDynamicCondition || jumpTarget.localTags.size() > 1)
					targetState.impreciseControlFlow = true;
				struct StackUse
				{
					size_t required = 0;
					ptrdiff_t delta = 0;
				};
				auto instructionStackUse = [](AssemblyItem const& _item)
				{
					if (_item.type() == Operation)
					{
						Instruction const instruction = _item.instruction();
						if (isDupInstruction(instruction))
							return StackUse{static_cast<size_t>(getDupNumber(instruction)), 1};
						if (isSwapInstruction(instruction))
							return StackUse{static_cast<size_t>(getSwapNumber(instruction)) + 1, 0};
					}
					return StackUse{
						_item.arguments(),
						static_cast<ptrdiff_t>(_item.returnValues()) - static_cast<ptrdiff_t>(_item.arguments())
					};
				};
				auto requiredStackDepthAtEntry = [&](size_t _targetIndex)
				{
					size_t required = 0;
					ptrdiff_t height = 0;
					size_t const end = std::min(_items.size(), _targetIndex + 64);
					for (size_t index = _targetIndex; index < end; ++index)
					{
						AssemblyItem const& targetItem = _items[index];
						if (targetItem.type() == Tag)
							continue;
						StackUse const stackUse = instructionStackUse(targetItem);
						if (height < static_cast<ptrdiff_t>(stackUse.required))
							required = std::max(
								required,
								static_cast<size_t>(static_cast<ptrdiff_t>(stackUse.required) - height)
							);
						height += stackUse.delta;
						if (
							targetItem.type() == Operation &&
							(
								targetItem.instruction() == Instruction::JUMP ||
								(
									targetItem.instruction() != Instruction::JUMPI &&
									terminatesLinearControlFlow(targetItem.instruction())
								)
							)
						)
							break;
					}
					return required;
				};
				auto impreciseTargetWouldUnderflow = [&](size_t _targetIndex)
				{
					return
						_pruneImpreciseInvalidStates &&
						targetState.impreciseControlFlow &&
						!targetState.unknownBelow &&
						targetState.stack.size() < requiredStackDepthAtEntry(_targetIndex);
				};
				auto stateCarriesAddressTaint = [](AbstractStackState const& _state)
				{
					if (
						_state.returnDataMayContainCurrentAddress ||
						_state.returnDataMayContainComplementedCurrentAddress ||
						_state.memoryMayContainCurrentAddress ||
						_state.storageMayContainCurrentAddress ||
						_state.memoryMayContainComplementedCurrentAddress ||
						_state.storageMayContainComplementedCurrentAddress
					)
						return true;
					return ranges::any_of(_state.stack, [](AbstractStackValue const& _value) {
						return
							valueMayBeCurrentAddressDerived(_value) ||
							valueMayBeComplementedCurrentAddressDerived(_value);
					});
				};
				bool const skipImpreciseLocalFanout =
					_pruneImpreciseInvalidStates &&
					targetState.impreciseControlFlow &&
					(targetState.unknownBelow || !stateCarriesAddressTaint(targetState)) &&
					jumpTarget.localTags.size() > c_maxEarlyValidationLocalJumpTargets;
				if (!skipImpreciseLocalFanout)
				{
					for (size_t tagId: jumpTarget.localTags)
					{
						if (auto tagIndex = util::valueOrNullptr(tagToItemIndex, tagId))
							if (!impreciseTargetWouldUnderflow(*tagIndex))
								mergeStateAt(*tagIndex, targetState);
					}
				}
				if (literalJumpTargetItemIndex)
					if (!impreciseTargetWouldUnderflow(*literalJumpTargetItemIndex))
						mergeStateAt(*literalJumpTargetItemIndex, std::move(targetState));
			}
			if (conditionalJumpMayFallThrough)
			{
				if (conditionalJumpHasDynamicCondition)
					state.impreciseControlFlow = true;
				continueWith(std::move(state));
			}
			continue;
		}

			if (item.type() == Operation && terminatesLinearControlFlow(item.instruction()))
			{
				continue;
			}

			bool foreignOutput = foreignInput;
			if (item.type() == Operation)
			{
				Instruction instruction = item.instruction();
				foreignOutput =
					(operationOutputCanPropagateStackInputValue && foreignInput) ||
					(instruction == Instruction::MLOAD && state.memoryMayContainForeignTag) ||
					(instruction == Instruction::SLOAD && state.storageMayContainForeignTag);
			}
				else if (item.type() == VerbatimBytecode)
				{
					foreignOutput =
						foreignOutput ||
						verbatimReadsForeign ||
						verbatimCalls.outputMayBeForeignTag;
				}
			bool unknownLocalTagOutput = false;
			if (item.type() == Operation)
			{
				Instruction instruction = item.instruction();
					bool const preservesKnownLocalTag =
						!identityPreservedMetadata.localTags.empty() &&
						!identityPreservedMetadata.mayBeUnknownLocalTag;
				unknownLocalTagOutput =
					(
						operationOutputCanPropagateStackInputValue &&
						localTagDerivedInput &&
						!preservesKnownLocalTag
					) ||
					(instruction == Instruction::MLOAD && state.memoryMayContainLocalTag) ||
					(instruction == Instruction::SLOAD && state.storageMayContainLocalTag);
			}
				else if (item.type() == VerbatimBytecode)
				{
					unknownLocalTagOutput =
						verbatimCalls.outputMayBeUnknownLocalTag ||
						verbatimReadsLocalTag;
				}
			bool currentAddressOutput = false;
		bool currentAddressDerivedOutput = false;
		if (item.type() == Operation)
		{
			Instruction instruction = item.instruction();
			currentAddressOutput =
				(instruction == Instruction::MLOAD && (state.memoryMayContainExactCurrentAddress || operationLoadsCurrentAddressFromBytes)) ||
				(instruction == Instruction::SLOAD && state.storageMayContainExactCurrentAddress);
			currentAddressDerivedOutput =
				(instruction == Instruction::MLOAD && state.memoryMayContainCurrentAddress) ||
				(instruction == Instruction::SLOAD && state.storageMayContainCurrentAddress);
		}
			else if (item.type() == VerbatimBytecode)
			{
				currentAddressOutput =
					verbatimCalls.outputMayBeCurrentAddress ||
					currentAddressInput ||
				(
					verbatimTaint.readsMemory &&
					(state.memoryMayContainCurrentAddress || verbatimCalls.mayWriteCurrentAddressToMemory)
					) ||
					(
							verbatimTaint.readsStorage &&
						(state.storageMayContainCurrentAddress || verbatimCalls.mayWriteCurrentAddressToStorage)
						);
				currentAddressDerivedOutput = currentAddressOutput;
			}
		bool complementedCurrentAddressOutput = false;
		bool complementedCurrentAddressDerivedOutput = false;
		if (item.type() == Operation)
		{
			Instruction instruction = item.instruction();
			complementedCurrentAddressOutput =
				(
					instruction == Instruction::MLOAD &&
					(state.memoryMayContainExactComplementedCurrentAddress || operationLoadsComplementedCurrentAddressFromBytes)
				) ||
				(instruction == Instruction::SLOAD && state.storageMayContainExactComplementedCurrentAddress);
			complementedCurrentAddressDerivedOutput =
				(instruction == Instruction::MLOAD && state.memoryMayContainComplementedCurrentAddress) ||
				(instruction == Instruction::SLOAD && state.storageMayContainComplementedCurrentAddress);
		}
			else if (item.type() == VerbatimBytecode)
			{
				complementedCurrentAddressOutput =
					verbatimCalls.outputMayBeComplementedCurrentAddress ||
					complementedCurrentAddressInput ||
				(
					verbatimTaint.readsMemory &&
					(
						state.memoryMayContainComplementedCurrentAddress ||
						verbatimCalls.mayWriteComplementedCurrentAddressToMemory
					)
				) ||
					(
						verbatimTaint.readsStorage &&
						(
							state.storageMayContainComplementedCurrentAddress ||
							verbatimCalls.mayWriteComplementedCurrentAddressToStorage
						)
					);
				complementedCurrentAddressDerivedOutput = complementedCurrentAddressOutput;
			}
			AbstractStackValue outputValue;
			if (item.type() == Operation)
			{
				outputValue.mayBeForeignTag = foreignOutput;
				outputValue.mayBeExternalJumpTarget =
					operationOutputCanPropagateStackInputValue && externalJumpTargetInput;
				outputValue.mayBeUnknownLocalTag = unknownLocalTagOutput;
					if (operationCurrentAddressOffsetValue)
					{
						outputValue.currentAddressOffset = *operationCurrentAddressOffsetValue;
						if (*operationCurrentAddressOffsetValue == 0)
							outputValue.markMayBeCurrentAddress();
					}
					if (
						!operationPossibleCurrentAddressOffsetsValue.empty() &&
						!(
							outputValue.currentAddressOffset &&
							operationPossibleCurrentAddressOffsetsValue.size() == 1 &&
							operationPossibleCurrentAddressOffsetsValue.count(*outputValue.currentAddressOffset)
						)
					)
					{
						if (operationPossibleCurrentAddressOffsetsValue.count(u512(0)))
							outputValue.mayBeCurrentAddress = true;
						outputValue.mayBeCurrentAddressDerived = true;
						outputValue.possibleCurrentAddressOffsets = operationPossibleCurrentAddressOffsetsValue;
					}
					if (operationCurrentAddressXorMaskValue)
					{
					outputValue.currentAddressXorMask = *operationCurrentAddressXorMaskValue;
					if (*operationCurrentAddressXorMaskValue == 0)
					{
						outputValue.markMayBeCurrentAddress();
					}
					if (*operationCurrentAddressXorMaskValue == ~u512(0))
					{
						outputValue.markMayBeComplementedCurrentAddress();
					}
				}
				if (operationComplementedCurrentAddressOffsetValue)
				{
					outputValue.complementedCurrentAddressOffset = *operationComplementedCurrentAddressOffsetValue;
					if (*operationComplementedCurrentAddressOffsetValue == 0)
						outputValue.markMayBeComplementedCurrentAddress();
				}
				if (currentAddressOutput || operationMayProduceCurrentAddressValue)
					outputValue.markMayBeCurrentAddress();
				if (currentAddressDerivedOutput || operationMayProduceCurrentAddressDerivedValue)
					outputValue.mayBeCurrentAddressDerived = true;
				if (complementedCurrentAddressOutput || operationMayProduceComplementedCurrentAddressValue)
					outputValue.markMayBeComplementedCurrentAddress();
				if (complementedCurrentAddressDerivedOutput || operationMayProduceComplementedCurrentAddressDerivedValue)
					outputValue.mayBeComplementedCurrentAddressDerived = true;
				if (operationCurrentAddressByteIndexValue)
				{
					setCurrentAddressLowByte(outputValue, *operationCurrentAddressByteIndexValue);
					markByteExtractedValue(outputValue);
				}
				if (operationComplementedCurrentAddressByteIndexValue)
				{
					setComplementedCurrentAddressLowByte(outputValue, *operationComplementedCurrentAddressByteIndexValue);
					markByteExtractedValue(outputValue);
				}
					if (operationInputIndependentLiteralValue)
						outputValue.literalValue = *operationInputIndependentLiteralValue;
					else if (item.instruction() == Instruction::PUSH0)
						outputValue.literalValue = u512(0);
				if (identityPreservedMetadata.mustBeExternalJumpTarget)
					outputValue.mustBeExternalJumpTarget = true;
				if (identityPreservedMetadata.mayBeUnknownLocalTag)
					outputValue.mayBeUnknownLocalTag = true;
				if (identityPreservedMetadata.mayBeUnknownCallerStackValue)
					outputValue.mayBeUnknownCallerStackValue = true;
				if (identityPreservedMetadata.currentAddressByteIndex)
					setCurrentAddressLowByte(outputValue, *identityPreservedMetadata.currentAddressByteIndex);
				if (identityPreservedMetadata.complementedCurrentAddressByteIndex)
					setComplementedCurrentAddressLowByte(
						outputValue,
						*identityPreservedMetadata.complementedCurrentAddressByteIndex
					);
				outputValue.possibleCurrentAddressOffsets.insert(
					identityPreservedMetadata.possibleCurrentAddressOffsets.begin(),
					identityPreservedMetadata.possibleCurrentAddressOffsets.end()
				);
				copyStackByteMetadata(identityPreservedMetadata, outputValue);
				if (identityPreservedMetadata.mayBeUnknownLeftShiftedLocalTag)
					outputValue.mayBeUnknownLeftShiftedLocalTag = true;
				if (identityPreservedMetadata.localTagLeftShift)
					outputValue.localTagLeftShift = identityPreservedMetadata.localTagLeftShift;
				outputValue.foreignTags.insert(
					identityPreservedMetadata.foreignTags.begin(),
					identityPreservedMetadata.foreignTags.end()
				);
				outputValue.localTags.insert(
					identityPreservedMetadata.localTags.begin(),
					identityPreservedMetadata.localTags.end()
			);
				outputValue.leftShiftedLocalTags.insert(
					identityPreservedMetadata.leftShiftedLocalTags.begin(),
					identityPreservedMetadata.leftShiftedLocalTags.end()
				);
				if (operationInputIndependentLiteralValue)
					outputValue = AbstractStackValue::literal(*operationInputIndependentLiteralValue);
				else
					outputValue.isZeroOfValueBelow = isZeroOfValueBelow;
			}
			if (item.type() == VerbatimBytecode)
			{
				outputValue.mayBeForeignTag = foreignOutput;
				outputValue.mayBeUnknownLocalTag = unknownLocalTagOutput;
				if (currentAddressOutput)
					outputValue.markMayBeCurrentAddress();
				if (currentAddressDerivedOutput)
					outputValue.mayBeCurrentAddressDerived = true;
				if (complementedCurrentAddressOutput)
					outputValue.markMayBeComplementedCurrentAddress();
				if (complementedCurrentAddressDerivedOutput)
					outputValue.mayBeComplementedCurrentAddressDerived = true;
		}
		if (item.type() == VerbatimBytecode && verbatimCalls.outputValues.size() == item.returnValues())
			state.stack.insert(state.stack.end(), verbatimCalls.outputValues.begin(), verbatimCalls.outputValues.end());
		else
			for (size_t i = 0; i < item.returnValues(); ++i)
				state.stack.push_back(outputValue);
		if (auto error = checkStackLimit())
			return error;
		continueWith(std::move(state));
	}

	return pendingError;
}

}

AssemblyItem const& Assembly::append(AssemblyItem _i)
{
	assertThrow(m_deposit >= 0, AssemblyException, "Stack underflow.");
	assertRepresentableAssemblyItemReferences(_i);
	m_deposit += static_cast<int>(_i.deposit());
	m_items.emplace_back(std::move(_i));
	if (!m_items.back().location().isValid() && m_currentSourceLocation.isValid())
		m_items.back().setLocation(m_currentSourceLocation);
	m_items.back().m_modifierDepth = m_currentModifierDepth;
	return m_items.back();
}

AssemblyItem const& Assembly::append(bytes const& _data)
{
	assertMutable();
	util::h256 h(util::keccak256(util::asString(_data)));
	AssemblyItem item(PushData, u512(u256(h)));
	auto [data, inserted] = m_data.emplace(h, _data);
	bool appended = false;
	ScopeGuard rollbackOnFailure([&, inserted = inserted]() {
		if (!appended && inserted)
			m_data.erase(h);
	});
	assertThrow(inserted || data->second == _data, AssemblyException, "Data hash mismatch.");
	AssemblyItem const& result = append(std::move(item));
	appended = true;
	return result;
}

void Assembly::appendLibraryAddress(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{PushLibraryAddress, u512(u256(h))};
	auto [library, inserted] = m_libraries.emplace(h, _identifier);
	bool appended = false;
	ScopeGuard rollbackOnFailure([&, inserted = inserted]() {
		if (!appended && inserted)
			m_libraries.erase(h);
	});
	assertThrow(inserted || library->second == _identifier, AssemblyException, "Library identifier hash mismatch.");
	append(std::move(item));
	appended = true;
}

void Assembly::appendImmutable(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{PushImmutable, u512(u256(h))};
	auto [immutable, inserted] = m_immutables.emplace(h, _identifier);
	bool appended = false;
	ScopeGuard rollbackOnFailure([&, inserted = inserted]() {
		if (!appended && inserted)
			m_immutables.erase(h);
	});
	assertThrow(inserted || immutable->second == _identifier, AssemblyException, "Immutable identifier hash mismatch.");
	append(std::move(item));
	appended = true;
}

void Assembly::appendImmutableAssignment(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{AssignImmutable, u512(u256(h))};
	auto [immutable, inserted] = m_immutables.emplace(h, _identifier);
	bool appended = false;
	ScopeGuard rollbackOnFailure([&, inserted = inserted]() {
		if (!appended && inserted)
			m_immutables.erase(h);
	});
	assertThrow(inserted || immutable->second == _identifier, AssemblyException, "Immutable identifier hash mismatch.");
	append(std::move(item));
	appended = true;
}

AssemblyItem Assembly::appendJump()
{
	assertMutable();
	AssemblyItems originalItems = m_items;
	unsigned const originalUsedTags = m_usedTags;
	int const originalDeposit = m_deposit;
	bool jumpAppended = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (!jumpAppended)
		{
			m_items = std::move(originalItems);
			m_usedTags = originalUsedTags;
			m_deposit = originalDeposit;
		}
	});

	AssemblyItem ret = append(newPushTag());
	append(Instruction::JUMP);
	jumpAppended = true;
	return ret;
}

AssemblyItem Assembly::appendJumpI()
{
	assertMutable();
	AssemblyItems originalItems = m_items;
	unsigned const originalUsedTags = m_usedTags;
	int const originalDeposit = m_deposit;
	bool jumpAppended = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (!jumpAppended)
		{
			m_items = std::move(originalItems);
			m_usedTags = originalUsedTags;
			m_deposit = originalDeposit;
		}
	});

	AssemblyItem ret = append(newPushTag());
	append(Instruction::JUMPI);
	jumpAppended = true;
	return ret;
}

AssemblyItem Assembly::appendJump(AssemblyItem const& _tag)
{
	assertMutable();
	AssemblyItems originalItems = m_items;
	unsigned const originalUsedTags = m_usedTags;
	int const originalDeposit = m_deposit;
	bool jumpAppended = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (!jumpAppended)
		{
			m_items = std::move(originalItems);
			m_usedTags = originalUsedTags;
			m_deposit = originalDeposit;
		}
	});

	AssemblyItem ret = append(_tag.pushTag());
	append(Instruction::JUMP);
	jumpAppended = true;
	return ret;
}

AssemblyItem Assembly::appendJumpI(AssemblyItem const& _tag)
{
	assertMutable();
	AssemblyItems originalItems = m_items;
	unsigned const originalUsedTags = m_usedTags;
	int const originalDeposit = m_deposit;
	bool jumpAppended = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (!jumpAppended)
		{
			m_items = std::move(originalItems);
			m_usedTags = originalUsedTags;
			m_deposit = originalDeposit;
		}
	});

	AssemblyItem ret = append(_tag.pushTag());
	append(Instruction::JUMPI);
	jumpAppended = true;
	return ret;
}

bool Assembly::declaresTag(size_t _tagId) const
{
	for (AssemblyItem const& item: m_items)
		if (item.type() == Tag)
		{
			assertSerializableAssemblyItem(item);
			if (static_cast<size_t>(item.data()) == _tagId)
				return true;
		}

	return false;
}

void Assembly::assertUniqueTagDeclarations() const
{
	std::set<size_t> tags;
	for (AssemblyItem const& item: m_items)
		if (item.type() == Tag)
		{
			assertSerializableAssemblyItem(item);
			assertThrow(
				tags.insert(static_cast<size_t>(item.data())).second,
				AssemblyException,
				"Duplicate tag position."
			);
		}
}

void Assembly::assertValidDataSection() const
{
	for (auto const& [dataHash, dataBytes]: m_data)
		assertThrow(
			dataHash == h256(util::keccak256(util::asString(dataBytes))),
			AssemblyException,
			"Data hash mismatch."
		);
	auto validateIdentifiers = [](std::map<h256, std::string> const& _identifiers, char const* _kind, bool _allowEmpty)
	{
		for (auto const& [hash, identifier]: _identifiers)
		{
			assertThrow(_allowEmpty || !identifier.empty(), AssemblyException, fmt::format("Empty {} identifier.", _kind));
			assertThrow(
				hash == h256(util::keccak256(identifier)),
				AssemblyException,
				fmt::format("{} identifier hash mismatch.", _kind)
			);
		}
	};
	validateIdentifiers(m_libraries, "library", true);
	validateIdentifiers(m_immutables, "immutable", false);
}

void Assembly::assertValidNamedTagMetadata() const
{
	std::set<size_t> namedTagIds;
	for (auto const& [name, tagInfo]: m_namedTags)
	{
		assertThrow(!name.empty(), AssemblyException, "Empty named tag.");
		assertThrow(tagInfo.id != 0, AssemblyException, "Invalid tag position.");
		assertThrow(
			tagInfo.id < std::numeric_limits<unsigned>::max(),
			AssemblyException,
			"Tag id out of bounds."
		);
		assertThrow(
			validFunctionStackSlots(tagInfo.params, tagInfo.returns),
			AssemblyException,
			"Function metadata stack slots are out of bounds."
		);
		assertThrow(
			m_tagReplacements || declaresTag(tagInfo.id),
			AssemblyException,
			"Function metadata references tag without position."
		);
		assertThrow(
			namedTagIds.insert(tagInfo.id).second,
			AssemblyException,
			"Duplicate function metadata."
		);
	}
}

void Assembly::assertResolvableItemReferences(AssemblyItem const& _item) const
{
	assertSerializableAssemblyItem(_item);
	switch (_item.type())
	{
	case PushData:
		assertThrow(
			m_data.count(h256(checkedHashReference(_item, "Data"))) != 0,
			AssemblyException,
			"Reference to non-existing data."
		);
		break;
	case PushSub:
	case PushSubSize:
		assertThrow(_item.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "Subassembly id out of bounds.");
		(void)subAssemblyById(static_cast<size_t>(_item.data()));
		break;
	case PushLibraryAddress:
		(void)requiredIdentifier(
			m_libraries,
			h256(checkedHashReference(_item, "Library")),
			"library"
		);
		break;
	case PushImmutable:
	case AssignImmutable:
		(void)requiredIdentifier(
			m_immutables,
			h256(checkedHashReference(_item, "Immutable")),
			"immutable"
		);
		break;
	case PushTag:
	{
		auto [subId, tagId] = _item.splitForeignPushTag();
		if (subId == std::numeric_limits<size_t>::max())
			assertThrow(
				tagId == 0 || declaresTag(tagId),
				AssemblyException,
				"Reference to tag without position."
			);
		else
			assertThrow(
				subAssemblyById(subId)->declaresTag(tagId),
				AssemblyException,
				"Reference to tag without position."
			);
		break;
	}
	default:
		break;
	}
}

AssemblyItem Assembly::newSub(AssemblyPointer const& _sub)
{
	assertMutable();
	AssemblyItem sub(PushSub, m_subs.size());
	m_subs.push_back(_sub);
	return sub;
}

AssemblyPointer Assembly::clone() const
{
	auto cloned = std::make_shared<Assembly>(*this);
	cloned->m_subs.clear();
	cloned->m_subs.reserve(m_subs.size());
	for (auto const& sub: m_subs)
	{
		assertThrow(sub, AssemblyException, "Invalid sub-assembly.");
		cloned->m_subs.push_back(sub->clone());
	}
	return cloned;
}

AssemblyItem Assembly::appendSubroutine(AssemblyPointer const& _assembly)
{
	size_t const originalSubCount = m_subs.size();
	bool subroutineAppended = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (!subroutineAppended)
			m_subs.resize(originalSubCount);
	});

	AssemblyItem sub = newSub(_assembly);
	append(newPushSubSize(size_t(sub.data())));
	subroutineAppended = true;
	return sub;
}

size_t Assembly::codeSize(
	unsigned _tagSize,
	unsigned _dataRefSize,
	unsigned _programSizeRefSize,
	Precision _precision
) const
{
	size_t ret = 0;
	std::set<h256> const referencedHashes = referencedDataHashes(m_items);
	for (AssemblyItem const& item: m_items)
	{
		switch (item.type())
		{
		case Push:
			ret = checkedAddSize(ret, 1 + numberEncodingSize(item.data()));
			break;
		case PushTag:
			ret = checkedAddSize(ret, 1 + _tagSize);
			break;
		case PushData:
		case PushSub:
			ret = checkedAddSize(ret, 1 + _dataRefSize);
			if (item.type() == PushSub)
			{
				assertThrow(item.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "");
				(void)subAssemblyById(static_cast<size_t>(item.data()));
			}
			break;
		case PushProgramSize:
			ret = checkedAddSize(ret, 1 + _programSizeRefSize);
			break;
		case PushSubSize:
			{
				assertThrow(item.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "");
				size_t subSize = subAssemblyById(static_cast<size_t>(item.data()))->assemble(
					{},
					{},
					allowsDeployTimeAddressInSubAssembly(static_cast<size_t>(item.data()))
				).bytecode.size();
				ret = checkedAddSize(ret, 1 + std::max<unsigned>(1, numberEncodingSize(subSize)));
				break;
			}
		default:
			ret = checkedAddSize(ret, item.bytesRequired(_tagSize, _precision));
			break;
		}
	}

	if (!m_subs.empty() || !m_data.empty() || !m_auxiliaryData.empty())
		ret = checkedAddSize(ret, 1);

	ret = checkedAddSize(ret, referencedDataSize(m_data, referencedHashes));

	return ret;
}

size_t Assembly::legacyCodeSizeLowerBound(unsigned _subTagSize) const
{
	std::set<h256> const referencedHashes = referencedDataHashes(m_items);
	for (unsigned tagSize = _subTagSize; true; ++tagSize)
	{
		size_t ret = 1;
		ret = checkedAddSize(ret, referencedDataSize(m_data, referencedHashes));

		for (AssemblyItem const& i: m_items)
			ret = checkedAddSize(
				ret,
				i.type() == Push ? 1 + numberEncodingSize(i.data()) : i.bytesRequired(tagSize, Precision::Approximate)
			);
		if (numberEncodingSize(ret) <= tagSize)
			return ret;
	}
}

void Assembly::importAssemblyItemsFromJSON(Json::Value const& _code, std::vector<std::string> const& _sourceList)
{
	solRequire(m_items.empty(), AssemblyImportException, "Assembly items can only be imported into an empty assembly.");
	solRequire(_code.isArray(), AssemblyImportException, "Supplied JSON is not an array.");

	AssemblyItems originalItems = m_items;
	unsigned const originalUsedTags = m_usedTags;
	int const originalDeposit = m_deposit;
	auto originalNamedTags = m_namedTags;
	auto originalLibraries = m_libraries;
	auto originalImmutables = m_immutables;
	auto originalSharedSourceNames = m_sharedSourceNames;
	bool importSucceeded = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (importSucceeded)
			return;
		m_items = std::move(originalItems);
		m_usedTags = originalUsedTags;
		m_deposit = originalDeposit;
		m_namedTags = std::move(originalNamedTags);
		m_libraries = std::move(originalLibraries);
		m_immutables = std::move(originalImmutables);
		m_sharedSourceNames = std::move(originalSharedSourceNames);
	});

	for (auto jsonItemIter = std::begin(_code); jsonItemIter != std::end(_code); ++jsonItemIter)
	{
		AssemblyItem const& newItem = m_items.emplace_back(createAssemblyItemFromJSON(*jsonItemIter, _sourceList));
		if (newItem == Instruction::JUMPDEST)
			hypThrow(AssemblyImportException, "JUMPDEST instruction without a tag");
		if (newItem.type() == AssemblyItemType::Tag)
		{
			++jsonItemIter;
			if (jsonItemIter != std::end(_code) && createAssemblyItemFromJSON(*jsonItemIter, _sourceList) != Instruction::JUMPDEST)
				hypThrow(AssemblyImportException, "JUMPDEST expected after tag.");
		}
	}
	importSucceeded = true;
}

AssemblyItem Assembly::createAssemblyItemFromJSON(Json::Value const& _json, std::vector<std::string> const& _sourceList)
{
	solRequire(_json.isObject(), AssemblyImportException, "Supplied JSON is not an object.");
	static std::set<std::string> const validMembers{
		"name",
		"begin",
		"end",
		"source",
		"value",
		"modifierDepth",
		"jumpType",
		"functionName",
		"parameterSlots",
		"returnSlots",
		"arguments",
		"returnValues",
		"id"
	};
	for (std::string const& member: _json.getMemberNames())
		solRequire(
			validMembers.count(member),
			AssemblyImportException,
			fmt::format(
				"Unknown member '{}'. Valid members are: {}.",
				diagnosticValue(member),
				hyperion::util::joinHumanReadable(validMembers, ", ")
			)
		);
	solRequire(isOfType<std::string>(_json["name"]), AssemblyImportException, "Member 'name' missing or not of type string.");
	solRequire(isOfTypeIfExists<int>(_json, "begin"), AssemblyImportException, "Optional member 'begin' not of type int.");
	solRequire(isOfTypeIfExists<int>(_json, "end"), AssemblyImportException, "Optional member 'end' not of type int.");
	solRequire(isOfTypeIfExists<int>(_json, "source"), AssemblyImportException, "Optional member 'source' not of type int.");
	solRequire(isOfTypeIfExists<std::string>(_json, "value"), AssemblyImportException, "Optional member 'value' not of type string.");
	solRequire(isOfTypeIfExists<int>(_json, "modifierDepth"), AssemblyImportException, "Optional member 'modifierDepth' not of type int.");
	solRequire(isOfTypeIfExists<std::string>(_json, "jumpType"), AssemblyImportException, "Optional member 'jumpType' not of type string.");
	solRequire(isOfTypeIfExists<std::string>(_json, "functionName"), AssemblyImportException, "Optional member 'functionName' not of type string.");
	solRequire(isOfTypeIfExists<Json::UInt64>(_json, "parameterSlots"), AssemblyImportException, "Optional member 'parameterSlots' not of type unsigned integer.");
	solRequire(isOfTypeIfExists<Json::UInt64>(_json, "returnSlots"), AssemblyImportException, "Optional member 'returnSlots' not of type unsigned integer.");
	solRequire(isOfTypeIfExists<Json::UInt64>(_json, "arguments"), AssemblyImportException, "Optional member 'arguments' not of type unsigned integer.");
	solRequire(isOfTypeIfExists<Json::UInt64>(_json, "returnValues"), AssemblyImportException, "Optional member 'returnValues' not of type unsigned integer.");
	solRequire(isOfTypeIfExists<Json::UInt64>(_json, "id"), AssemblyImportException, "Optional member 'id' not of type unsigned integer.");

	std::string name = get<std::string>(_json["name"]);
	solRequire(!name.empty(), AssemblyImportException, "Member 'name' is empty.");
	std::string const diagnosticName = diagnosticValue(name);
	bool const hasFunctionName = _json.isMember("functionName");
	bool const hasParameterSlots = _json.isMember("parameterSlots");
	bool const hasReturnSlots = _json.isMember("returnSlots");
	bool const hasSourceID = _json.isMember("id");
	bool const hasFunctionMetadata = hasFunctionName || hasParameterSlots || hasReturnSlots || hasSourceID;
	bool const hasVerbatimArguments = _json.isMember("arguments");
	bool const hasVerbatimReturnValues = _json.isMember("returnValues");
	bool const hasVerbatimStackEffect = hasVerbatimArguments || hasVerbatimReturnValues;
	solRequire(
		!hasVerbatimStackEffect || name == "VERBATIM",
		AssemblyImportException,
		"VERBATIM stack effect metadata is only valid on VERBATIM items."
	);
	solRequire(
		hasVerbatimArguments == hasVerbatimReturnValues,
		AssemblyImportException,
		"VERBATIM stack effect metadata must define both 'arguments' and 'returnValues'."
	);

	SourceLocation location;
	location.start = getOrDefault<int>(_json["begin"], -1);
	location.end = getOrDefault<int>(_json["end"], -1);
	int srcIndex = getOrDefault<int>(_json["source"], -1);
	int modifierDepthValue = getOrDefault<int>(_json["modifierDepth"], 0);
	size_t modifierDepth = static_cast<size_t>(modifierDepthValue);
	std::string value = getOrDefault<std::string>(_json["value"], "");
	std::string jumpType = getOrDefault<std::string>(_json["jumpType"], "");

	auto updateUsedTags = [&](bigint const& data)
	{
		solRequire(
			data >= 0 && data < std::numeric_limits<unsigned>::max(),
			AssemblyImportException,
			"Tag value out of bounds."
		);
		unsigned tagID = static_cast<unsigned>(data);
		m_usedTags = std::max(m_usedTags, tagID + 1);
		return u512(tagID);
	};

	auto parsePushTag = [&](std::string const& _value, std::string const& _field)
	{
		bigint data = parseAssemblyInteger(_value, _field);
		solRequire(data >= 0 && data < (bigint(1) << VMWordBits), AssemblyImportException, "Tag value out of bounds.");
		if (data < std::numeric_limits<unsigned>::max())
			return updateUsedTags(data);

		bigint const subAssembly = data >> 64;
		bigint const tag = data & ((bigint(1) << 64) - 1);
		solRequire(
			subAssembly > 0 &&
			subAssembly <= bigint(std::numeric_limits<size_t>::max()) &&
			tag >= 0 &&
			tag < std::numeric_limits<unsigned>::max(),
			AssemblyImportException,
			"Tag value out of bounds."
		);
		return u512(data);
	};

	auto storeImmutableHash = [&](std::string const& _immutableName) -> h256
	{
		h256 hash(util::keccak256(_immutableName));
		auto [immutable, inserted] = m_immutables.emplace(hash, _immutableName);
		solRequire(inserted || immutable->second == _immutableName, AssemblyImportException, "Immutable identifier hash mismatch.");
		return hash;
	};

	auto storeLibraryHash = [&](std::string const& _libraryName) -> h256
	{
		h256 hash(util::keccak256(_libraryName));
		auto [library, inserted] = m_libraries.emplace(hash, _libraryName);
		solRequire(inserted || library->second == _libraryName, AssemblyImportException, "Library identifier hash mismatch.");
		return hash;
	};

	auto requireValueDefinedForInstruction = [&](std::string const& _name, std::string const& _value)
	{
		solRequire(
			!_value.empty(),
			AssemblyImportException,
			"Member 'value' is missing for instruction '" + diagnosticValue(_name) + "', but the instruction needs a value."
		);
	};

	auto requireValueUndefinedForInstruction = [&](std::string const& _name, std::string const& _value)
	{
		solRequire(
			_value.empty(),
			AssemblyImportException,
			"Member 'value' defined for instruction '" + diagnosticValue(_name) + "', but the instruction does not need a value."
		);
	};

	auto readSizeT = [&](std::string const& _field) -> size_t
	{
		Json::UInt64 const value = get<Json::UInt64>(_json[_field]);
		solRequire(
			value <= static_cast<Json::UInt64>(std::numeric_limits<size_t>::max()),
			AssemblyImportException,
			"Optional member '" + _field + "' is out of bounds."
		);
		return static_cast<size_t>(value);
	};

	solRequire(srcIndex >= -1, AssemblyImportException, "Source index out of bounds.");
	if (srcIndex != -1)
		solRequire(static_cast<size_t>(srcIndex) < _sourceList.size(), AssemblyImportException, "Source index out of bounds.");
	if (srcIndex != -1)
		location.sourceName = sharedSourceName(_sourceList[static_cast<size_t>(srcIndex)]);

	AssemblyItem result(0);

	if (c_instructions.count(name))
	{
		AssemblyItem item{c_instructions.at(name), location};
		if (!jumpType.empty())
		{
			if (item.instruction() == Instruction::JUMP || item.instruction() == Instruction::JUMPI)
			{
				std::optional<AssemblyItem::JumpType> parsedJumpType = AssemblyItem::parseJumpType(jumpType);
				if (!parsedJumpType.has_value())
					hypThrow(AssemblyImportException, "Invalid jump type.");
				item.setJumpType(parsedJumpType.value());
			}
				else
					hypThrow(
						AssemblyImportException,
						"Member 'jumpType' set on instruction different from JUMP or JUMPI (was set on instruction '" + diagnosticName + "')"
					);
		}
		requireValueUndefinedForInstruction(name, value);
		result = item;
	}
	else
	{
		solRequire(
			jumpType.empty(),
			AssemblyImportException,
			"Member 'jumpType' set on instruction different from JUMP or JUMPI (was set on instruction '" + diagnosticName + "')"
		);
		if (name == "PUSH")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::Push, parseAssemblyHexWord(value, "Member 'value' for instruction '" + name + "'")};
		}
		else if (name == "PUSH [ErrorTag]")
		{
			requireValueUndefinedForInstruction(name, value);
			result = {AssemblyItemType::PushTag, 0};
		}
		else if (name == "PUSH [tag]")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushTag, parsePushTag(value, "Member 'value' for instruction '" + name + "'")};
		}
		else if (name == "PUSH [$]")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushSub, parseAssemblyHexSizeT(value, "Member 'value' for instruction '" + name + "'")};
		}
		else if (name == "PUSH #[$]")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushSubSize, parseAssemblyHexSizeT(value, "Member 'value' for instruction '" + name + "'")};
		}
		else if (name == "PUSHSIZE")
		{
			requireValueUndefinedForInstruction(name, value);
			result = {AssemblyItemType::PushProgramSize, 0};
		}
		else if (name == "PUSHLIB")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushLibraryAddress, u512(u256(storeLibraryHash(value)))};
		}
		else if (name == "PUSHDEPLOYADDRESS")
		{
			requireValueUndefinedForInstruction(name, value);
			result = {AssemblyItemType::PushDeployTimeAddress, 0};
		}
		else if (name == "PUSHIMMUTABLE")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushImmutable, u512(u256(storeImmutableHash(value)))};
		}
		else if (name == "ASSIGNIMMUTABLE")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::AssignImmutable, u512(u256(storeImmutableHash(value)))};
		}
		else if (name == "tag")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::Tag, updateUsedTags(parseAssemblyInteger(value, "Member 'value' for instruction '" + name + "'"))};
		}
		else if (name == "PUSH data")
		{
			requireValueDefinedForInstruction(name, value);
			result = {AssemblyItemType::PushData, parseAssemblyHexDataReference(value, "Member 'value' for instruction '" + name + "'")};
		}
		else if (name == "VERBATIM")
		{
			solRequire(
				_json.isMember("value"),
				AssemblyImportException,
				"Member 'value' is missing for instruction '" + name + "', but the instruction needs a value."
			);
			bytes verbatimData = fromHex(value);
			size_t arguments = 0;
			size_t returnValues = 0;
			if (hasVerbatimStackEffect)
			{
				arguments = readSizeT("arguments");
				returnValues = readSizeT("returnValues");
			}
			AssemblyItem item(std::move(verbatimData), arguments, returnValues);
			result = item;
		}
		else
			hypThrow(AssemblyImportException, "Invalid opcode: " + diagnosticName);
	}
	result.setLocation(location);
	result.m_modifierDepth = modifierDepth;
	if (hasFunctionMetadata)
	{
		solRequire(
			result.type() == AssemblyItemType::Tag,
			AssemblyImportException,
			"Function metadata is only valid on tag items."
		);
		solRequire(
			hasFunctionName && hasParameterSlots && hasReturnSlots,
			AssemblyImportException,
			"Function metadata must define 'functionName', 'parameterSlots' and 'returnSlots'."
		);
		std::string functionName = get<std::string>(_json["functionName"]);
		solRequire(!functionName.empty(), AssemblyImportException, "Function name is empty.");
		solRequire(
			result.data() <= std::numeric_limits<size_t>::max(),
			AssemblyImportException,
			"Tag value out of bounds."
		);
		std::optional<size_t> sourceID;
		if (hasSourceID)
			sourceID = readSizeT("id");
		size_t const parameterSlots = readSizeT("parameterSlots");
		size_t const returnSlots = readSizeT("returnSlots");
		auto [iter, inserted] = m_namedTags.emplace(
			std::move(functionName),
			NamedTagInfo{
				static_cast<size_t>(result.data()),
				std::move(sourceID),
				parameterSlots,
				returnSlots
			}
		);
		(void)iter;
		solRequire(inserted, AssemblyImportException, "Duplicate function metadata.");
	}
	return result;
}

namespace
{

std::string locationFromSources(StringMap const& _sourceCodes, SourceLocation const& _location)
{
	if (!_location.hasText() || _sourceCodes.empty())
		return {};

	auto it = _sourceCodes.find(*_location.sourceName);
	if (it == _sourceCodes.end())
		return {};

	return CharStream::singleLineSnippet(it->second, _location);
}

class Functionalizer
{
public:
	Functionalizer (std::ostream& _out, std::string const& _prefix, StringMap const& _sourceCodes, Assembly const& _assembly):
		m_out(_out), m_prefix(_prefix), m_sourceCodes(_sourceCodes), m_assembly(_assembly)
	{}

	void feed(AssemblyItem const& _item, DebugInfoSelection const& _debugInfoSelection)
	{
		assertRepresentableAssemblyItemReferences(_item);
		if (_item.location().isValid() && _item.location() != m_location)
		{
			flush();
			m_location = _item.location();
			printLocation(_debugInfoSelection);
		}

		std::string expression = _item.toAssemblyText(m_assembly);

		if (!(
			_item.canBeFunctional() &&
			_item.returnValues() <= 1 &&
			_item.arguments() <= m_pending.size()
		))
		{
			flush();
			m_out << m_prefix << (_item.type() == Tag ? "" : "  ") << expression << std::endl;
			return;
		}
		if (_item.arguments() > 0)
		{
			expression += "(";
			for (size_t i = 0; i < _item.arguments(); ++i)
			{
				expression += m_pending.back();
				m_pending.pop_back();
				if (i + 1 < _item.arguments())
					expression += ", ";
			}
			expression += ")";
		}

		m_pending.push_back(expression);
		if (_item.returnValues() != 1)
			flush();
	}

	void flush()
	{
		for (std::string const& expression: m_pending)
			m_out << m_prefix << "  " << expression << std::endl;
		m_pending.clear();
	}

	void printLocation(DebugInfoSelection const& _debugInfoSelection)
	{
		if (!m_location.isValid() || (!_debugInfoSelection.location && !_debugInfoSelection.snippet))
			return;

		m_out << m_prefix << "    /*";

		if (_debugInfoSelection.location)
		{
			if (m_location.sourceName)
				m_out << " " + escapeAndQuoteString(*m_location.sourceName);
			if (m_location.hasText())
				m_out << ":" << std::to_string(m_location.start) + ":" + std::to_string(m_location.end);
		}

		if (_debugInfoSelection.snippet)
		{
			if (_debugInfoSelection.location)
				m_out << "  ";

			m_out << locationFromSources(m_sourceCodes, m_location);
		}

		m_out << " */" << std::endl;
	}

private:
	strings m_pending;
	SourceLocation m_location;

	std::ostream& m_out;
	std::string const& m_prefix;
	StringMap const& m_sourceCodes;
	Assembly const& m_assembly;
};

}

void Assembly::assemblyStream(
	std::ostream& _out,
	DebugInfoSelection const& _debugInfoSelection,
	std::string const& _prefix,
	StringMap const& _sourceCodes
) const
{
	assertThrow(!m_invalid, AssemblyException, "Attempted to output invalid Assembly object.");
	Functionalizer f(_out, _prefix, _sourceCodes, *this);

	for (auto const& i: m_items)
		f.feed(i, _debugInfoSelection);
	f.flush();

	if (!m_data.empty() || !m_subs.empty() || !m_auxiliaryData.empty())
	{
		_out << _prefix << "stop" << std::endl;
		for (auto const& i: m_data)
			_out << _prefix << "data_" << toHex(u256(i.first)) << " " << util::toHex(i.second) << std::endl;

		for (size_t i = 0; i < m_subs.size(); ++i)
		{
			_out << std::endl << _prefix << "sub_" << i << ": assembly {\n";
			m_subs[i]->assemblyStream(_out, _debugInfoSelection, _prefix + "    ", _sourceCodes);
			_out << _prefix << "}" << std::endl;
		}
	}

	if (m_auxiliaryData.size() > 0)
		_out << std::endl << _prefix << "auxdata: 0x" << util::toHex(m_auxiliaryData) << std::endl;
}

std::string Assembly::assemblyString(
	DebugInfoSelection const& _debugInfoSelection,
	StringMap const& _sourceCodes
) const
{
	std::ostringstream tmp;
	assemblyStream(tmp, _debugInfoSelection, "", _sourceCodes);
	return tmp.str();
}

Json::Value Assembly::assemblyJSON(std::map<std::string, unsigned> const& _sourceIndices, bool _includeSourceList) const
{
	return assemblyJSON(_sourceIndices, _includeSourceList, true);
}

Json::Value Assembly::assemblyJSON(
	std::map<std::string, unsigned> const& _sourceIndices,
	bool _includeSourceList,
	bool _isRoot
) const
{
	assertThrow(!m_invalid, AssemblyException, "Attempted to output invalid Assembly object.");
	Json::Value root;
	root[".code"] = Json::arrayValue;
	if (m_creation != _isRoot)
		root[".creation"] = m_creation;
	Json::Value& code = root[".code"];
	auto setSourceLocation = [&](Json::Value& _jsonItem, SourceLocation const& _location)
	{
		int sourceIndex = -1;
		int begin = -1;
		int end = -1;
		if (_location.sourceName)
		{
			auto iter = _sourceIndices.find(*_location.sourceName);
			if (iter != _sourceIndices.end())
			{
				assertThrow(
					iter->second <= static_cast<unsigned>(std::numeric_limits<int>::max()),
					AssemblyException,
					"Source index out of bounds."
				);
				sourceIndex = static_cast<int>(iter->second);
				if (_location.hasText())
				{
					begin = _location.start;
					end = _location.end;
				}
			}
		}
		_jsonItem["begin"] = begin;
		_jsonItem["end"] = end;
		_jsonItem["source"] = sourceIndex;
	};
	auto toJsonUInt64 = [](size_t _value) -> Json::UInt64
	{
		assertThrow(
			_value <= static_cast<size_t>(std::numeric_limits<Json::UInt64>::max()),
			AssemblyException,
			"Numeric value out of bounds."
		);
		return static_cast<Json::UInt64>(_value);
	};
	for (AssemblyItem const& item: m_items)
	{
		assertRepresentableAssemblyItemReferences(item);
		if (
			item.type() == AssemblyItemType::PushData ||
			item.type() == AssemblyItemType::PushLibraryAddress ||
			item.type() == AssemblyItemType::PushImmutable ||
			item.type() == AssemblyItemType::AssignImmutable
		)
			(void)checkedHashReference(item, "Hash");
		auto [name, data] = item.nameAndData();
		Json::Value jsonItem;
		jsonItem["name"] = name;
		setSourceLocation(jsonItem, item.location());
		if (item.m_modifierDepth != 0)
		{
			assertThrow(
				item.m_modifierDepth <= static_cast<size_t>(std::numeric_limits<int>::max()),
				AssemblyException,
				"Modifier depth out of bounds."
			);
			jsonItem["modifierDepth"] = static_cast<int>(item.m_modifierDepth);
		}
		std::string jumpType = item.getJumpTypeAsString();
		if (!jumpType.empty())
			jsonItem["jumpType"] = jumpType;
		if (item.type() == AssemblyItemType::Tag)
		{
			assertThrow(
				item.data() <= std::numeric_limits<size_t>::max(),
				AssemblyException,
				"Tag value out of bounds."
			);
			size_t const tagID = static_cast<size_t>(item.data());
			for (auto const& [functionName, tagInfo]: m_namedTags)
				if (tagInfo.id == tagID)
				{
					jsonItem["functionName"] = functionName;
					jsonItem["parameterSlots"] = toJsonUInt64(tagInfo.params);
					jsonItem["returnSlots"] = toJsonUInt64(tagInfo.returns);
					if (tagInfo.sourceID)
						jsonItem["id"] = toJsonUInt64(*tagInfo.sourceID);
					break;
				}
		}
		if (name == "PUSHLIB")
			data = requiredIdentifier(m_libraries, h256(data), "library");
		else if (name == "PUSHIMMUTABLE" || name == "ASSIGNIMMUTABLE")
			data = requiredIdentifier(m_immutables, h256(data), "immutable");
		if (!data.empty() || item.type() == AssemblyItemType::VerbatimBytecode)
			jsonItem["value"] = data;
		code.append(std::move(jsonItem));

		if (item.type() == AssemblyItemType::Tag)
		{
			Json::Value jumpdest;
			jumpdest["name"] = "JUMPDEST";
			setSourceLocation(jumpdest, item.location());
			if (item.m_modifierDepth != 0)
			{
				assertThrow(
					item.m_modifierDepth <= static_cast<size_t>(std::numeric_limits<int>::max()),
					AssemblyException,
					"Modifier depth out of bounds."
				);
				jumpdest["modifierDepth"] = static_cast<int>(item.m_modifierDepth);
			}
			code.append(std::move(jumpdest));
		}
	}
	if (_includeSourceList)
	{
		root["sourceList"] = Json::arrayValue;
		Json::Value& jsonSourceList = root["sourceList"];
		std::map<unsigned, std::string> sourcesByIndex;
		for (auto const& [name, index]: _sourceIndices)
		{
			assertThrow(
				index <= static_cast<unsigned>(std::numeric_limits<int>::max()),
				AssemblyException,
				"Source index out of bounds."
			);
			bool inserted = sourcesByIndex.emplace(index, name).second;
			assertThrow(inserted, AssemblyException, "Duplicate source index.");
		}
		for (size_t index = 0; index < sourcesByIndex.size(); ++index)
			assertThrow(
				sourcesByIndex.count(static_cast<unsigned>(index)) != 0,
				AssemblyException,
				"Source indices are not contiguous."
			);
		for (auto const& [index, name]: sourcesByIndex)
			jsonSourceList[index] = name;
	}

	if (!m_data.empty() || !m_subs.empty())
	{
		root[".data"] = Json::objectValue;
		Json::Value& data = root[".data"];
		for (auto const& i: m_data)
			data[util::toHex(toBigEndian((u256)i.first), util::HexPrefix::DontAdd, util::HexCase::Upper)] = util::toHex(i.second);

		for (size_t i = 0; i < m_subs.size(); ++i)
		{
			std::stringstream hexStr;
			hexStr << std::hex << i;
			data[hexStr.str()] = m_subs[i]->assemblyJSON(
				_sourceIndices,
				/*_includeSourceList = */false,
				/*_isRoot = */false
			);
		}
	}

		if (!m_auxiliaryData.empty())
			root[".auxdata"] = util::toHex(m_auxiliaryData);
		if (!m_deployTimeAddressSubAssemblies.empty())
		{
			root[".deployTimeAddressSubAssemblies"] = Json::arrayValue;
			Json::Value& deployTimeAddressSubAssemblies = root[".deployTimeAddressSubAssemblies"];
			for (size_t subIdPath: m_deployTimeAddressSubAssemblies)
			{
				deployTimeAddressSubAssemblies.append(formatAssemblyHexSizeT(subIdPath));
			}
		}

		return root;
	}

std::pair<std::shared_ptr<Assembly>, std::vector<std::string>> Assembly::fromJSON(
	Json::Value const& _json,
	std::vector<std::string> const& _sourceList,
	size_t _level
)
{
	solRequire(
		_sourceList.empty(),
		AssemblyImportException,
		"Source list parameter may only be provided for nested assembly JSON imports."
	);
	solRequire(
		_level == 0,
		AssemblyImportException,
		"Assembly JSON import level parameter must be zero for root imports."
	);
	return fromJSONInternal(_json, _sourceList, _level);
}

std::pair<std::shared_ptr<Assembly>, std::vector<std::string>> Assembly::fromJSONInternal(
	Json::Value const& _json,
	std::vector<std::string> const& _sourceList,
	size_t _level
)
{
	solRequire(_json.isObject(), AssemblyImportException, "Supplied JSON is not an object.");
	solRequire(_level < c_maxAssemblyJSONNesting, AssemblyImportException, "Assembly JSON nesting is too deep.");
	solRequire(
		_level != 0 || _sourceList.empty(),
		AssemblyImportException,
		"Source list parameter may only be provided for nested assembly JSON imports."
	);
	static std::set<std::string> const validMembers{
		".code",
		".creation",
		".data",
		".auxdata",
		".deployTimeAddressSubAssemblies",
		"sourceList"
	};
	for (std::string const& attribute: _json.getMemberNames())
		solRequire(
			validMembers.count(attribute),
			AssemblyImportException,
			"Unknown attribute '" + diagnosticValue(attribute) + "'."
		);

	bool creation = _level == 0;
	if (_json.isMember(".creation"))
	{
		solRequire(_json[".creation"].isBool(), AssemblyImportException, "Optional member '.creation' is not a boolean.");
		creation = _json[".creation"].asBool();
	}

	std::set<size_t> deployTimeAddressSubAssemblies;
	if (_level == 0)
	{
		if (_json.isMember("sourceList"))
		{
			solRequire(_json["sourceList"].isArray(), AssemblyImportException, "Optional member 'sourceList' is not an array.");
			for (Json::Value const& sourceName: _json["sourceList"])
				solRequire(sourceName.isString(), AssemblyImportException, "The 'sourceList' array contains an item that is not a string.");
		}
	}
	else
	{
		solRequire(
			!_json.isMember("sourceList"),
			AssemblyImportException,
			"Member 'sourceList' may only be present in the root JSON object."
		);
	}
	if (_json.isMember(".deployTimeAddressSubAssemblies"))
	{
		Json::Value const& deployTimeAddressSubAssemblyJSON = _json[".deployTimeAddressSubAssemblies"];
		solRequire(
			deployTimeAddressSubAssemblyJSON.isArray(),
			AssemblyImportException,
			"Optional member '.deployTimeAddressSubAssemblies' is not an array."
		);
		for (Json::Value const& subIdPath: deployTimeAddressSubAssemblyJSON)
		{
			solRequire(
				subIdPath.isString(),
				AssemblyImportException,
				"The '.deployTimeAddressSubAssemblies' array contains an item that is not a string."
			);
			u512 const parsedSubIdPath = parseAssemblyHexSizeT(
				subIdPath.asString(),
				"An item in '.deployTimeAddressSubAssemblies'"
			);
			deployTimeAddressSubAssemblies.insert(static_cast<size_t>(parsedSubIdPath));
		}
	}

	auto result = std::make_shared<Assembly>(QRVMVersion{}, creation /* _creation */, "" /* _name */);
	std::vector<std::string> parsedSourceList;
	if (_json.isMember("sourceList"))
	{
		hypAssert(_level == 0);
		hypAssert(_sourceList.empty());
		std::set<std::string> sourceNames;
		for (Json::Value const& sourceName: _json["sourceList"])
		{
			std::string parsedSourceName = sourceName.asString();
			solRequire(
				sourceNames.insert(parsedSourceName).second,
				AssemblyImportException,
				"Items in 'sourceList' array are not unique."
			);
			parsedSourceList.emplace_back(std::move(parsedSourceName));
		}
	}

	solRequire(_json.isMember(".code"), AssemblyImportException, "Member '.code' is missing.");
	solRequire(_json[".code"].isArray(), AssemblyImportException, "Member '.code' is not an array.");
	for (Json::Value const& codeItem: _json[".code"])
		solRequire(codeItem.isObject(), AssemblyImportException, "The '.code' array contains an item that is not an object.");

	result->importAssemblyItemsFromJSON(_json[".code"], _level == 0 ? parsedSourceList : _sourceList);

	if (_json.isMember(".auxdata"))
	{
		solRequire(_json[".auxdata"].isString(), AssemblyImportException, "Optional member '.auxdata' is not a string.");
		result->m_auxiliaryData = parseAssemblyHexBytes(_json[".auxdata"].asString(), "Optional member '.auxdata'", false);
	}

	if (_json.isMember(".data"))
	{
		solRequire(_json[".data"].isObject(), AssemblyImportException, "Optional member '.data' is not an object.");
		Json::Value const& data = _json[".data"];
		std::map<size_t, std::shared_ptr<Assembly>> subAssemblies;
		for (Json::ValueConstIterator dataIter = data.begin(); dataIter != data.end(); dataIter++)
		{
			hypAssert(dataIter.key().isString());
			std::string dataItemID = dataIter.key().asString();
			std::string const dataItemDescription = assemblyDataKeyDescription(dataItemID);
			Json::Value const& dataItem = data[dataItemID];
			if (dataItem.isString())
			{
				bytes dataBytes = parseAssemblyHexBytes(
					dataItem.asString(),
					"The value for " + dataItemDescription,
					true
				);
				h256 const dataHash = parseAssemblyDataHash(dataItemID, dataItemDescription);
				result->m_data[dataHash] = std::move(dataBytes);
			}
			else if (dataItem.isObject())
			{
				size_t index{};
				bigint parsedDataItemID = parseAssemblyHexInteger(
					dataItemID,
					"The " + dataItemDescription,
					sizeof(size_t) * 2
				);
				solRequire(
					parsedDataItemID >= 0 && parsedDataItemID <= std::numeric_limits<size_t>::max(),
					AssemblyImportException,
					"The " + dataItemDescription + " is out of the supported integer range."
				);
				index = static_cast<size_t>(parsedDataItemID);

				auto [subAssembly, emptySourceList] = Assembly::fromJSONInternal(
					dataItem,
					_level == 0 ? parsedSourceList : _sourceList,
					_level + 1
				);
				hypAssert(subAssembly);
				hypAssert(emptySourceList.empty());
				solRequire(
					subAssemblies.emplace(index, std::move(subAssembly)).second,
					AssemblyImportException,
					"The " + dataItemDescription + " duplicates an existing subassembly index."
				);
			}
			else
				hypThrow(AssemblyImportException, "The value of " + dataItemDescription + " is neither a hex string nor an object.");
		}

		if (!subAssemblies.empty())
			solRequire(
				ranges::max(subAssemblies | ranges::views::keys) == subAssemblies.size() - 1,
				AssemblyImportException,
				fmt::format(
					"Invalid subassembly indices in '.data'. Not all numbers between 0 and {} are present.",
					subAssemblies.size() - 1
				)
			);

		result->m_subs = subAssemblies | ranges::views::values | ranges::to<std::vector>;
	}

	for (size_t subIdPath: deployTimeAddressSubAssemblies)
		result->m_deployTimeAddressSubAssemblies.insert(subIdPath);

	if (_level == 0)
	{
		result->encodeAllPossibleSubPathsInAssemblyTree();
	}

	return std::make_pair(result, _level == 0 ? parsedSourceList : std::vector<std::string>{});
}

void Assembly::encodeAllPossibleSubPathsInAssemblyTree(std::vector<size_t> _pathFromRoot, std::vector<Assembly*> _assembliesOnPath)
{
	_assembliesOnPath.push_back(this);
	for (_pathFromRoot.push_back(0); _pathFromRoot.back() < m_subs.size(); ++_pathFromRoot.back())
	{
		for (size_t distanceFromRoot = 0; distanceFromRoot < _assembliesOnPath.size(); ++distanceFromRoot)
			_assembliesOnPath[distanceFromRoot]->encodeSubPath(
				_pathFromRoot | ranges::views::drop_exactly(distanceFromRoot) | ranges::to<std::vector>
			);

		auto const& sub = m_subs[_pathFromRoot.back()];
		assertThrow(sub, AssemblyException, "Invalid sub-assembly.");
		sub->encodeAllPossibleSubPathsInAssemblyTree(_pathFromRoot, _assembliesOnPath);
	}
}

std::shared_ptr<std::string const> Assembly::sharedSourceName(std::string const& _name) const
{
	auto [sourceName, inserted] = m_sharedSourceNames.emplace(_name, nullptr);
	(void)inserted;
	if (!sourceName->second)
		sourceName->second = std::make_shared<std::string const>(sourceName->first);
	return sourceName->second;
}

void Assembly::validateSubReferences() const
{
	std::map<Assembly const*, std::set<size_t>> declaredTagCache;
	auto declaredTags = [&](Assembly const& _assembly) -> std::set<size_t> const&
	{
		auto [cacheEntry, inserted] = declaredTagCache.emplace(&_assembly, std::set<size_t>{});
		if (!inserted)
			return cacheEntry->second;

		for (AssemblyItem const& item: _assembly.m_items)
			if (item.type() == Tag)
			{
				solRequire(item.data() <= std::numeric_limits<size_t>::max(), AssemblyImportException, "Tag value out of bounds.");
				auto [subId, tagId] = item.splitForeignPushTag();
				solRequire(subId == std::numeric_limits<size_t>::max(), AssemblyImportException, "Foreign tag.");
				bool tagInserted = cacheEntry->second.insert(tagId).second;
				solRequire(tagInserted, AssemblyImportException, "Duplicate tag position.");
			}
		return cacheEntry->second;
	};
	std::set<size_t> const& localTags = declaredTags(*this);

	auto resolveSubId = [&](size_t _subId) -> Assembly const*
	{
		std::vector<size_t> subIds;
		if (_subId < m_subs.size())
			subIds = {_subId};
		else
		{
			auto subIdPathIt = find_if(
				m_subPaths.begin(),
				m_subPaths.end(),
				[_subId](auto const& subId) { return subId.second == _subId; }
			);
			solRequire(subIdPathIt != m_subPaths.end(), AssemblyImportException, "Reference to non-existing subassembly.");
			subIds = subIdPathIt->first;
		}

		Assembly const* currentAssembly = this;
		for (size_t currentSubId: subIds)
		{
			solRequire(
				currentSubId < currentAssembly->m_subs.size() && currentAssembly->m_subs[currentSubId],
				AssemblyImportException,
				"Reference to non-existing subassembly."
			);
			currentAssembly = currentAssembly->m_subs[currentSubId].get();
		}
		solRequire(currentAssembly != this, AssemblyImportException, "Reference to non-existing subassembly.");
		return currentAssembly;
	};

	for (AssemblyItem const& item: m_items)
	{
		if (item.type() == PushData)
		{
			solRequire(
				item.data() < (u512(1) << (h256::size * 8)),
				AssemblyImportException,
				"Data reference out of bounds."
			);
			solRequire(
				m_data.count(h256(u256(item.data()))) != 0,
				AssemblyImportException,
				"Reference to non-existing data."
			);
		}
		else if (item.type() == PushSub || item.type() == PushSubSize)
		{
			solRequire(item.data() <= std::numeric_limits<size_t>::max(), AssemblyImportException, "Subassembly id out of bounds.");
			(void)resolveSubId(static_cast<size_t>(item.data()));
		}
		else if (item.type() == PushTag)
		{
			auto [subId, tagId] = item.splitForeignPushTag();
			if (subId == std::numeric_limits<size_t>::max())
			{
				solRequire(tagId == 0 || localTags.count(tagId), AssemblyImportException, "Reference to tag without position.");
			}
			else
			{
				Assembly const* subAssembly = resolveSubId(subId);
				std::set<size_t> const& subTags = declaredTags(*subAssembly);
				solRequire(tagId != 0 && subTags.count(tagId), AssemblyImportException, "Reference to tag without position.");
			}
		}
	}

	for (auto const& sub: m_subs)
	{
		solRequire(sub, AssemblyImportException, "Reference to non-existing subassembly.");
		sub->validateSubReferences();
	}
}

bool Assembly::containsAssembly(Assembly const* _assembly, std::set<Assembly const*>& _visited) const
{
	assertThrow(_assembly, AssemblyException, "Invalid sub-assembly.");
	if (this == _assembly)
		return true;
	if (!_visited.insert(this).second)
		return false;

	for (auto const& sub: m_subs)
	{
		assertThrow(sub, AssemblyException, "Invalid sub-assembly.");
		if (sub->containsAssembly(_assembly, _visited))
			return true;
	}

	return false;
}

void Assembly::assertValidSubAssemblyTree() const
{
	std::set<Assembly const*> assembliesOnPath;
	std::set<Assembly const*> assembliesInTree;
	assertValidSubAssemblyTree(assembliesOnPath, assembliesInTree, 0);
}

void Assembly::assertValidSubAssemblyTree(
	std::set<Assembly const*>& _assembliesOnPath,
	std::set<Assembly const*>& _assembliesInTree,
	size_t _depth
) const
{
	assertThrow(
		_depth < c_maxAssemblyJSONNesting,
		AssemblyException,
		"Sub-assembly tree is too deep."
	);
	assertThrow(
		_assembliesOnPath.insert(this).second,
		AssemblyException,
		"Sub-assembly cycle."
	);
	assertThrow(
		_assembliesInTree.insert(this).second,
		AssemblyException,
		"Shared sub-assembly."
	);
	for (auto const& sub: m_subs)
	{
		assertThrow(sub, AssemblyException, "Invalid sub-assembly.");
		sub->assertValidSubAssemblyTree(_assembliesOnPath, _assembliesInTree, _depth + 1);
	}
	assertValidSubPathMap();
	_assembliesOnPath.erase(this);
}

std::optional<std::vector<size_t>> Assembly::canonicalSubPath(size_t _subObjectId) const
{
	if (_subObjectId < m_subs.size())
	{
		if (m_subs[_subObjectId])
			return std::vector<size_t>{_subObjectId};
		return std::nullopt;
	}

	size_t ordinal = 0;
	std::vector<size_t> currentPath;
	std::optional<std::vector<size_t>> result;
	auto visitSubPaths = [&](auto&& _visitSubPaths, Assembly const& _assembly) -> void
	{
		if (result)
			return;
		for (size_t subId = 0; subId < _assembly.m_subs.size(); ++subId)
		{
			AssemblyPointer const& sub = _assembly.m_subs[subId];
			if (!sub)
				return;
			currentPath.push_back(subId);
			if (currentPath.size() > 1)
			{
				size_t const objectId = std::numeric_limits<size_t>::max() - ordinal - 1;
				if (objectId == _subObjectId)
				{
					result = currentPath;
					return;
				}
				++ordinal;
			}
			_visitSubPaths(_visitSubPaths, *sub);
			currentPath.pop_back();
			if (result)
				return;
		}
	};
	visitSubPaths(visitSubPaths, *this);
	return result;
}

std::optional<size_t> Assembly::canonicalSubPathId(std::vector<size_t> const& _subPath) const
{
	if (_subPath.empty())
		return std::nullopt;
	if (_subPath.size() == 1)
	{
		if (_subPath.front() < m_subs.size() && m_subs[_subPath.front()])
			return _subPath.front();
		return std::nullopt;
	}

	size_t ordinal = 0;
	std::vector<size_t> currentPath;
	std::optional<size_t> result;
	auto visitSubPaths = [&](auto&& _visitSubPaths, Assembly const& _assembly) -> void
	{
		if (result)
			return;
		for (size_t subId = 0; subId < _assembly.m_subs.size(); ++subId)
		{
			AssemblyPointer const& sub = _assembly.m_subs[subId];
			if (!sub)
				return;
			currentPath.push_back(subId);
			if (currentPath.size() > 1)
			{
				size_t const objectId = std::numeric_limits<size_t>::max() - ordinal - 1;
				if (currentPath == _subPath)
				{
					result = objectId;
					return;
				}
				++ordinal;
			}
			_visitSubPaths(_visitSubPaths, *sub);
			currentPath.pop_back();
			if (result)
				return;
		}
	};
	visitSubPaths(visitSubPaths, *this);
	return result;
}

void Assembly::assertValidSubPathMap() const
{
	std::set<size_t> encodedSubPaths;
	for (auto const& [subPath, encodedSubPath]: m_subPaths)
	{
		assertThrow(subPath.size() > 1, AssemblyException, "Invalid sub-assembly path.");
		assertThrow(subPath.size() < c_maxAssemblyJSONNesting, AssemblyException, "Sub-assembly path is too deep.");
		assertThrow(encodedSubPath != std::numeric_limits<size_t>::max(), AssemblyException, "Invalid sub-assembly path id.");
		assertThrow(encodedSubPath >= m_subs.size(), AssemblyException, "Sub-assembly path id collides with a direct sub-assembly.");
		assertThrow(encodedSubPaths.insert(encodedSubPath).second, AssemblyException, "Duplicate sub-assembly path id.");
		std::optional<size_t> canonicalId = canonicalSubPathId(subPath);
		assertThrow(canonicalId, AssemblyException, "Invalid sub-assembly path.");
		assertThrow(encodedSubPath == *canonicalId, AssemblyException, "Invalid sub-assembly path id.");
	}
}

AssemblyItem Assembly::namedTag(std::string const& _name, size_t _params, size_t _returns, std::optional<uint64_t> _sourceID)
{
	assertMutable();
	assertThrow(!_name.empty(), AssemblyException, "Empty named tag.");
	std::optional<size_t> sourceID;
	if (_sourceID)
	{
		if constexpr (std::numeric_limits<size_t>::max() < std::numeric_limits<uint64_t>::max())
			assertThrow(
				*_sourceID <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
				AssemblyException,
				"Source id out of bounds."
			);
		sourceID = static_cast<size_t>(*_sourceID);
	}
	if (auto namedTag = m_namedTags.find(_name); namedTag != m_namedTags.end())
	{
		assertThrow(namedTag->second.params == _params, AssemblyException, "");
		assertThrow(namedTag->second.returns == _returns, AssemblyException, "");
		assertThrow(namedTag->second.sourceID == sourceID, AssemblyException, "");
		return AssemblyItem{Tag, namedTag->second.id};
	}

	assertThrow(m_usedTags < 0xffffffff, AssemblyException, "");
	NamedTagInfo tagInfo{m_usedTags, sourceID, _params, _returns};
	AssemblyItem tag{Tag, tagInfo.id};
	auto [namedTag, inserted] = m_namedTags.emplace(_name, tagInfo);
	assertThrow(inserted, AssemblyException, "");
	++m_usedTags;
	return tag;
}

AssemblyItem Assembly::newPushLibraryAddress(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{PushLibraryAddress, u512(u256(h))};
	auto [library, inserted] = m_libraries.emplace(h, _identifier);
	assertThrow(inserted || library->second == _identifier, AssemblyException, "Library identifier hash mismatch.");
	return item;
}

AssemblyItem Assembly::newPushImmutable(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{PushImmutable, u512(u256(h))};
	auto [immutable, inserted] = m_immutables.emplace(h, _identifier);
	assertThrow(inserted || immutable->second == _identifier, AssemblyException, "Immutable identifier hash mismatch.");
	return item;
}

AssemblyItem Assembly::newImmutableAssignment(std::string const& _identifier)
{
	assertMutable();
	h256 h(util::keccak256(_identifier));
	AssemblyItem item{AssignImmutable, u512(u256(h))};
	auto [immutable, inserted] = m_immutables.emplace(h, _identifier);
	assertThrow(inserted || immutable->second == _identifier, AssemblyException, "Immutable identifier hash mismatch.");
	return item;
}

Assembly& Assembly::optimise(OptimiserSettings const& _settings)
{
	assertThrow(!m_invalid, OptimizerException, "Attempted to optimize invalid Assembly object.");
	optimiseInternal(_settings, {});
	return *this;
}

std::map<u512, u512> const& Assembly::optimiseInternal(
	OptimiserSettings const& _settings,
	std::set<size_t> _tagsReferencedFromOutside,
	SubAssemblyTagReferences _subTagsReferencedFromOutside,
	std::set<size_t> _copiedTagsReferencedFromOutside,
	SubAssemblyTagReferences _copiedSubTagsReferencedFromOutside
)
{
	assertThrow(!m_invalid, OptimizerException, "Attempted to optimize invalid Assembly object.");
	if (m_tagReplacements)
		return *m_tagReplacements;

	struct OptimiseRollbackState
	{
		Assembly* assembly;
		AssemblyItems items;
		std::optional<std::map<h256, bytes>> data;
		unsigned usedTags;
		int deposit;
		std::optional<std::map<u512, u512>> tagReplacements;
		std::optional<OptimiserSettings> optimiserSettings;
		std::optional<std::set<size_t>> tagsReferencedFromOutside;
		std::optional<SubAssemblyTagReferences> subTagsReferencedFromOutside;
		std::optional<std::set<size_t>> copiedTagsReferencedFromOutside;
		std::optional<SubAssemblyTagReferences> copiedSubTagsReferencedFromOutside;
		LinkerObject assembledObject;
		std::map<size_t, size_t> tagPositionsInBytecode;
		std::map<size_t, size_t> literalJumpTargetsInBytecode;
		CodeCopyTaintRanges codeCopyTaintRangesInBytecode;
		bool assembled;
		size_t assembledInitialStackHeight;
	};
	std::vector<OptimiseRollbackState> rollbackStates;
	std::set<Assembly*> snapshottedAssemblies;
	auto snapshotForRollback = [&](auto&& _snapshotForRollback, Assembly& _assembly) -> void
	{
		if (!snapshottedAssemblies.insert(&_assembly).second)
			return;
		rollbackStates.push_back({
			&_assembly,
			_assembly.m_items,
			_settings.runConstantOptimiser ? std::optional<std::map<h256, bytes>>{_assembly.m_data} : std::nullopt,
			_assembly.m_usedTags,
			_assembly.m_deposit,
			_assembly.m_tagReplacements,
			_assembly.m_optimiserSettings,
			_assembly.m_tagsReferencedFromOutside,
			_assembly.m_subTagsReferencedFromOutside,
			_assembly.m_copiedTagsReferencedFromOutside,
			_assembly.m_copiedSubTagsReferencedFromOutside,
			_assembly.m_assembledObject,
			_assembly.m_tagPositionsInBytecode,
			_assembly.m_literalJumpTargetsInBytecode,
			_assembly.m_codeCopyTaintRangesInBytecode,
			_assembly.m_assembled,
			_assembly.m_assembledInitialStackHeight
		});
		for (auto const& subAssembly: _assembly.m_subs)
			if (subAssembly)
				_snapshotForRollback(_snapshotForRollback, *subAssembly);
	};
	snapshotForRollback(snapshotForRollback, *this);
	bool optimisationSucceeded = false;
	ScopeGuard rollbackOnFailure([&]() {
		if (optimisationSucceeded)
			return;
		for (auto state = rollbackStates.rbegin(); state != rollbackStates.rend(); ++state)
		{
			state->assembly->m_items = std::move(state->items);
			if (state->data)
				state->assembly->m_data = std::move(*state->data);
			state->assembly->m_usedTags = state->usedTags;
			state->assembly->m_deposit = state->deposit;
			state->assembly->m_tagReplacements = std::move(state->tagReplacements);
			state->assembly->m_optimiserSettings = std::move(state->optimiserSettings);
			state->assembly->m_tagsReferencedFromOutside = std::move(state->tagsReferencedFromOutside);
			state->assembly->m_subTagsReferencedFromOutside = std::move(state->subTagsReferencedFromOutside);
			state->assembly->m_copiedTagsReferencedFromOutside = std::move(state->copiedTagsReferencedFromOutside);
			state->assembly->m_copiedSubTagsReferencedFromOutside =
				std::move(state->copiedSubTagsReferencedFromOutside);
			state->assembly->m_assembledObject = std::move(state->assembledObject);
			state->assembly->m_tagPositionsInBytecode = std::move(state->tagPositionsInBytecode);
			state->assembly->m_literalJumpTargetsInBytecode = std::move(state->literalJumpTargetsInBytecode);
			state->assembly->m_codeCopyTaintRangesInBytecode = std::move(state->codeCopyTaintRangesInBytecode);
			state->assembly->m_assembled = state->assembled;
			state->assembly->m_assembledInitialStackHeight = state->assembledInitialStackHeight;
		}
	});
	auto clearAssemblyCacheForOptimisation = [](Assembly& _assembly)
	{
		_assembly.m_assembledObject = LinkerObject{};
		_assembly.m_tagPositionsInBytecode.clear();
		_assembly.m_literalJumpTargetsInBytecode.clear();
		_assembly.m_codeCopyTaintRangesInBytecode.clear();
		_assembly.m_assembled = false;
		_assembly.m_assembledInitialStackHeight = 0;
		for (AssemblyItem const& item: _assembly.m_items)
		{
			item.clearImmutableOccurrences();
			item.clearPushedValue();
		}
	};
	clearAssemblyCacheForOptimisation(*this);

	std::set<size_t> const initialTagsReferencedFromOutside = _tagsReferencedFromOutside;
	SubAssemblyTagReferences const initialSubTagsReferencedFromOutside = _subTagsReferencedFromOutside;
	std::set<size_t> const initialCopiedTagsReferencedFromOutside = _copiedTagsReferencedFromOutside;
	SubAssemblyTagReferences const initialCopiedSubTagsReferencedFromOutside = _copiedSubTagsReferencedFromOutside;

	auto addAllForeignTagReferences = [&]()
	{
		for (AssemblyItem const& item: m_items)
			if (item.type() == PushTag)
			{
				auto [subId, tagId] = item.splitForeignPushTag();
				if (subId != std::numeric_limits<size_t>::max())
					_subTagsReferencedFromOutside[decodeSubPath(subId)].insert(tagId);
			}
	};
	// Foreign tag immediates have to stay resolvable even if they are in code
	// that a later optimisation could remove. Direct subassembly references are
	// handled by JumpdestRemover::referencedTags(); encoded nested paths need the
	// same conservative treatment here before subassemblies are optimised.
	addAllForeignTagReferences();

	// Run optimisation for sub-assemblies.
	for (size_t subId = 0; subId < m_subs.size(); ++subId)
	{
		assertThrow(m_subs[subId], OptimizerException, "Invalid sub-assembly.");
		OptimiserSettings settings = _settings;
		Assembly& sub = *m_subs[subId];
		std::set<size_t> tagsReferencedFromOutside = JumpdestRemover::referencedTags(m_items, subId);
		SubAssemblyTagReferences subTagsReferencedFromOutside;
		std::set<size_t> copiedTagsReferencedFromOutside;
		SubAssemblyTagReferences copiedSubTagsReferencedFromOutside;
		for (auto const& [subPath, tagIds]: _subTagsReferencedFromOutside)
		{
			if (subPath.empty() || subPath.front() != subId)
				continue;
			if (subPath.size() == 1)
				tagsReferencedFromOutside.insert(tagIds.begin(), tagIds.end());
			else
			{
				std::vector<size_t> childSubPath(std::next(subPath.begin()), subPath.end());
				subTagsReferencedFromOutside[childSubPath].insert(tagIds.begin(), tagIds.end());
			}
		}
		for (auto const& [subPath, tagIds]: _copiedSubTagsReferencedFromOutside)
		{
			if (subPath.empty() || subPath.front() != subId)
				continue;
			if (subPath.size() == 1)
				copiedTagsReferencedFromOutside.insert(tagIds.begin(), tagIds.end());
			else
			{
				std::vector<size_t> childSubPath(std::next(subPath.begin()), subPath.end());
				copiedSubTagsReferencedFromOutside[childSubPath].insert(tagIds.begin(), tagIds.end());
			}
		}
		std::map<u512, u512> const& subTagReplacements = sub.optimiseInternal(
			settings,
			std::move(tagsReferencedFromOutside),
			std::move(subTagsReferencedFromOutside),
			std::move(copiedTagsReferencedFromOutside),
			std::move(copiedSubTagsReferencedFromOutside)
		);
		// Apply the replacements (can be empty).
		BlockDeduplicator::applyTagReplacement(m_items, subTagReplacements, subId);
	}

	std::set<size_t> tagsProtectedFromRemoval = _tagsReferencedFromOutside;

	std::map<u512, u512> tagReplacements;
	// Iterate until no new optimisation possibilities are found.
	for (unsigned count = 1; count > 0;)
	{
		count = 0;

		if (_settings.runInliner)
			Inliner{
				m_items,
				tagsProtectedFromRemoval,
				_settings.expectedExecutionsPerDeployment,
				isCreation(),
				_settings.qrvmVersion
			}.optimise();

		if (_settings.runJumpdestRemover)
		{
			JumpdestRemover jumpdestOpt{m_items};
			if (jumpdestOpt.optimise(tagsProtectedFromRemoval))
				count++;
		}

		if (_settings.runPeephole)
		{
			PeepholeOptimiser peepOpt{m_items};
			while (peepOpt.optimise())
			{
				count++;
				assertThrow(count < 64000, OptimizerException, "Peephole optimizer seems to be stuck.");
			}
		}

		// This only modifies PushTags, we have to run again to actually remove code.
		if (_settings.runDeduplicate)
		{
			BlockDeduplicator deduplicator{m_items, &tagsProtectedFromRemoval};
			if (deduplicator.deduplicate())
			{
				for (auto const& replacement: deduplicator.replacedTags())
				{
					assertThrow(
						replacement.first <= std::numeric_limits<size_t>::max() && replacement.second <= std::numeric_limits<size_t>::max(),
						OptimizerException,
						"Invalid tag replacement."
					);
					size_t const replacedTag = static_cast<size_t>(replacement.first);
					size_t const replacementTag = static_cast<size_t>(replacement.second);
					if (tagsProtectedFromRemoval.count(replacedTag) || tagsProtectedFromRemoval.count(replacementTag))
						continue;
					tagReplacements[replacement.first] = replacement.second;
				}
				count++;
			}
		}

		if (_settings.runCSE)
		{
			// Control flow graph optimization has been here before but is disabled because it
			// assumes we only jump to tags that are pushed. This is not the case anymore with
			// function types that can be stored in storage.
			AssemblyItems optimisedItems;

			bool usesMSize = ranges::any_of(m_items, [](AssemblyItem const& _i) {
				return _i == AssemblyItem{Instruction::MSIZE} || _i.type() == VerbatimBytecode;
			});

			auto iter = m_items.begin();
			while (iter != m_items.end())
			{
				KnownState emptyState;
				CommonSubexpressionEliminator eliminator{emptyState};
				auto orig = iter;
				iter = eliminator.feedItems(iter, m_items.end(), usesMSize);
				bool shouldReplace = false;
				AssemblyItems optimisedChunk;
				try
				{
					optimisedChunk = eliminator.getOptimizedItems();
					shouldReplace = (optimisedChunk.size() < static_cast<size_t>(iter - orig));
				}
				catch (StackTooDeepException const&)
				{
					// This might happen if the opcode reconstruction is not as efficient
					// as the hand-crafted code.
				}
				catch (ItemNotAvailableException const&)
				{
					// This might happen if e.g. associativity and commutativity rules
					// reorganise the expression tree, but not all leaves are available.
				}

				if (shouldReplace)
				{
					count++;
					optimisedItems += optimisedChunk;
				}
				else
					copy(orig, iter, back_inserter(optimisedItems));
			}
			if (optimisedItems.size() < m_items.size())
			{
				m_items = std::move(optimisedItems);
				count++;
			}
		}
	}

	if (_settings.runConstantOptimiser)
		ConstantOptimisationMethod::optimiseConstants(
			isCreation(),
			isCreation() ? 1 : _settings.expectedExecutionsPerDeployment,
			_settings.qrvmVersion,
			*this
		);

	m_deposit = recomputeStackDeposit(m_items);
	m_tagReplacements = std::move(tagReplacements);
	m_optimiserSettings = _settings;
	m_tagsReferencedFromOutside = initialTagsReferencedFromOutside;
	m_subTagsReferencedFromOutside = initialSubTagsReferencedFromOutside;
	m_copiedTagsReferencedFromOutside = initialCopiedTagsReferencedFromOutside;
	m_copiedSubTagsReferencedFromOutside = initialCopiedSubTagsReferencedFromOutside;
	optimisationSucceeded = true;
	return *m_tagReplacements;
}

LinkerObject const& Assembly::assemble() const
{
	return assemble({}, {}, false);
}

LinkerObject const& Assembly::assembleWithInitialStackHeight(size_t _initialStackHeight) const
{
	return assemble({}, {}, false, {}, {}, _initialStackHeight);
}

LinkerObject const& Assembly::assembleDeployTimeAddressTemplate() const
{
	return assemble({}, {}, true);
}

void Assembly::markDeployTimeAddressSubAssembly(size_t _subIdPath)
{
	assertMutable();
	m_deployTimeAddressSubAssemblies.insert(_subIdPath);
}

void Assembly::markImmutableValidationSubAssembly(size_t _subIdPath)
{
	assertMutable();
	m_immutableValidationSubAssemblies.insert(_subIdPath);
}

LinkerObject const& Assembly::assemble(
	std::set<size_t> _tagsReferencedFromOutside,
	SubAssemblyTagReferences _subTagsReferencedFromOutside,
	bool _allowDeployTimeAddress,
	std::set<size_t> _copiedTagsReferencedFromOutside,
	SubAssemblyTagReferences _copiedSubTagsReferencedFromOutside,
	size_t _initialStackHeight
) const
{
	assertThrow(!m_invalid, AssemblyException, "Attempted to assemble invalid Assembly object.");
	(void)_allowDeployTimeAddress;
	if (m_tagsReferencedFromOutside)
		_tagsReferencedFromOutside.insert(m_tagsReferencedFromOutside->begin(), m_tagsReferencedFromOutside->end());
	if (m_subTagsReferencedFromOutside)
		for (auto const& [subPath, tagIds]: *m_subTagsReferencedFromOutside)
			_subTagsReferencedFromOutside[subPath].insert(tagIds.begin(), tagIds.end());
	if (m_copiedTagsReferencedFromOutside)
		_copiedTagsReferencedFromOutside.insert(
			m_copiedTagsReferencedFromOutside->begin(),
			m_copiedTagsReferencedFromOutside->end()
		);
	if (m_copiedSubTagsReferencedFromOutside)
		for (auto const& [subPath, tagIds]: *m_copiedSubTagsReferencedFromOutside)
			_copiedSubTagsReferencedFromOutside[subPath].insert(tagIds.begin(), tagIds.end());

	auto referencesForSubAssembly = [&](size_t _subIdPath)
	{
		std::vector<size_t> subPath = decodeSubPath(_subIdPath);
		std::set<size_t> tagsReferencedFromOutside;
		SubAssemblyTagReferences subTagsReferencedFromOutside;
		auto collectReferences = [&](
			SubAssemblyTagReferences const& _sourceReferences,
			std::set<size_t>& _tagsReferencedFromOutside,
			SubAssemblyTagReferences& _subTagsReferencedFromOutside
		)
		{
			for (auto const& [referencedSubPath, tagIds]: _sourceReferences)
			{
				if (referencedSubPath.size() < subPath.size())
					continue;
				if (!std::equal(subPath.begin(), subPath.end(), referencedSubPath.begin()))
					continue;
				if (referencedSubPath.size() == subPath.size())
					_tagsReferencedFromOutside.insert(tagIds.begin(), tagIds.end());
				else
				{
					std::vector<size_t> childSubPath(
						referencedSubPath.begin() + static_cast<std::vector<size_t>::difference_type>(subPath.size()),
						referencedSubPath.end()
					);
					_subTagsReferencedFromOutside[std::move(childSubPath)].insert(tagIds.begin(), tagIds.end());
				}
			}
		};
		collectReferences(_subTagsReferencedFromOutside, tagsReferencedFromOutside, subTagsReferencedFromOutside);
		std::set<size_t> copiedTagsReferencedFromOutside;
		SubAssemblyTagReferences copiedSubTagsReferencedFromOutside;
		collectReferences(
			_copiedSubTagsReferencedFromOutside,
			copiedTagsReferencedFromOutside,
			copiedSubTagsReferencedFromOutside
		);
		return std::make_tuple(
			std::move(tagsReferencedFromOutside),
			std::move(subTagsReferencedFromOutside),
			std::move(copiedTagsReferencedFromOutside),
			std::move(copiedSubTagsReferencedFromOutside)
		);
	};

	auto assembleExternallyReferencedSubAssemblies = [&]()
	{
		std::set<size_t> directSubAssembliesWithExternalReferences;
		for (auto const& [subPath, tagIds]: _subTagsReferencedFromOutside)
		{
			(void)tagIds;
			if (!subPath.empty())
				directSubAssembliesWithExternalReferences.insert(subPath.front());
		}
		for (auto const& [subPath, tagIds]: _copiedSubTagsReferencedFromOutside)
		{
			(void)tagIds;
			if (!subPath.empty())
				directSubAssembliesWithExternalReferences.insert(subPath.front());
		}
		for (size_t subId: directSubAssembliesWithExternalReferences)
		{
			assertThrow(subId < m_subs.size() && m_subs[subId], AssemblyException, "Reference to non-existing subassembly.");
			auto [
				tagsReferencedFromOutside,
				subTagsReferencedFromOutside,
				copiedTagsReferencedFromOutside,
				copiedSubTagsReferencedFromOutside
			] = referencesForSubAssembly(subId);
			m_subs[subId]->assemble(
				std::move(tagsReferencedFromOutside),
				std::move(subTagsReferencedFromOutside),
				allowsDeployTimeAddressInSubAssembly(subId),
				std::move(copiedTagsReferencedFromOutside),
				std::move(copiedSubTagsReferencedFromOutside)
			);
		}
	};

	bool assemblySucceeded = false;
	std::vector<AssemblyItem const*> itemsWithImmutableOccurrences;
	std::vector<AssemblyItem const*> itemsWithPushedValues;
	std::set<Assembly const*> assembliesAlreadyAssembled;
	std::vector<Assembly const*> subAssembliesInTree;
	auto collectSubAssemblies = [&](auto&& _collectSubAssemblies, Assembly const* _assembly) -> void
	{
		for (auto const& subAssembly: _assembly->m_subs)
			if (subAssembly)
			{
				Assembly const* subAssemblyPtr = subAssembly.get();
				subAssembliesInTree.push_back(subAssemblyPtr);
				if (subAssemblyPtr->m_assembled)
					assembliesAlreadyAssembled.insert(subAssemblyPtr);
				_collectSubAssemblies(_collectSubAssemblies, subAssemblyPtr);
		}
	};
	collectSubAssemblies(collectSubAssemblies, this);
	[[maybe_unused]] auto assertValidCachedAssemblyMetadata = [this, &referencesForSubAssembly, &subAssembliesInTree]()
	{
		auto assertCachedByteRange = [this](size_t _offset, size_t _size, char const* _message)
		{
			assertThrow(
				_size > 0 &&
				_offset <= m_assembledObject.bytecode.size() &&
				_size <= m_assembledObject.bytecode.size() - _offset,
				AssemblyException,
				_message
			);
		};
		auto cachedSubAssemblyById = [this](size_t _subId, char const* _message) -> Assembly const*
		{
			std::optional<std::vector<size_t>> subPath = canonicalSubPath(_subId);
			assertThrow(subPath, AssemblyException, _message);

			Assembly const* currentAssembly = this;
			for (size_t currentSubId: *subPath)
			{
				assertThrow(
					currentSubId < currentAssembly->m_subs.size() &&
					currentAssembly->m_subs[currentSubId],
					AssemblyException,
					_message
				);
				currentAssembly = currentAssembly->m_subs[currentSubId].get();
			}
			assertThrow(currentAssembly != this, AssemblyException, _message);
			return currentAssembly;
		};
		auto assertCachedTaintByteRanges = [&](ByteRanges const& _ranges, char const* _message)
		{
			for (auto const& [offset, size]: _ranges)
				assertCachedByteRange(offset, size, _message);
		};
		auto cachedLibraryReferenceIsDeclared = [&](std::string const& _identifier)
		{
			h256 const libraryHash = h256(util::keccak256(_identifier));
			auto assemblyDeclaresLibrary = [&](Assembly const* _assembly)
			{
				if (!_assembly->m_libraries.count(libraryHash))
					return false;
				assertThrow(
					requiredIdentifier(_assembly->m_libraries, libraryHash, "library") == _identifier,
					AssemblyException,
					"Invalid cached link reference."
				);
				return true;
			};
			if (assemblyDeclaresLibrary(this))
				return true;
			return ranges::any_of(subAssembliesInTree, assemblyDeclaresLibrary);
		};

		auto readCachedSizeArgument = [&](size_t _offset, size_t _size, char const* _message)
		{
			size_t value = 0;
			for (size_t index = 0; index < _size; ++index)
			{
				uint8_t byte = m_assembledObject.bytecode[_offset + index];
				assertThrow(
					value <= (std::numeric_limits<size_t>::max() - byte) / 256,
					AssemblyException,
					_message
				);
				value = value * 256 + byte;
			}
			return value;
		};
		auto cachedPushArgument = [&](size_t _itemStart, char const* _message)
		{
			assertCachedByteRange(_itemStart, 1, _message);
			Instruction instruction = static_cast<Instruction>(m_assembledObject.bytecode[_itemStart]);
			assertThrow(isPushInstruction(instruction), AssemblyException, _message);
			InstructionInfo const& info = instructionInfo(instruction);
			assertThrow(info.additional > 0, AssemblyException, _message);
			size_t argumentOffset = checkedAddSize(_itemStart, 1);
			size_t argumentSize = static_cast<size_t>(info.additional);
			assertCachedByteRange(argumentOffset, argumentSize, _message);
			return std::make_pair(argumentOffset, argumentSize);
		};
		auto cachedItemStart = [&](size_t _itemIndex, char const* _message)
		{
			std::optional<size_t> itemStart;
			for (auto const& [bytecodeOffset, cachedItemIndex]: m_literalJumpTargetsInBytecode)
				if (cachedItemIndex == _itemIndex)
				{
					itemStart = bytecodeOffset;
					break;
				}
			assertThrow(itemStart, AssemblyException, _message);
			return *itemStart;
		};
		std::map<size_t, std::string> expectedLinkReferences;
		auto addExpectedLinkReference = [&](size_t _bytecodeOffset, std::string const& _identifier)
		{
			assertCachedByteRange(_bytecodeOffset, hyperion::AddressBytes, "Invalid cached link reference.");
			auto [expectedLink, inserted] = expectedLinkReferences.emplace(_bytecodeOffset, _identifier);
			assertThrow(
				inserted || expectedLink->second == _identifier,
				AssemblyException,
				"Invalid cached link reference."
			);
		};
		auto assertExpectedCachedByteTaintRange = [](
			ByteRanges const& _ranges,
			size_t _offset,
			size_t _size
		)
		{
			assertThrow(
				std::find(_ranges.begin(), _ranges.end(), std::make_pair(_offset, _size)) != _ranges.end(),
				AssemblyException,
				"Invalid cached code-copy taint range."
			);
		};
		auto assertExpectedCachedForeignTagTaintRange = [](
			CodeCopyTaintForeignTagRanges const& _ranges,
			size_t _offset,
			size_t _size,
			size_t _subId,
			size_t _tagId
		)
		{
			assertThrow(
				std::find(_ranges.begin(), _ranges.end(), std::make_tuple(_offset, _size, _subId, _tagId)) != _ranges.end(),
				AssemblyException,
				"Invalid cached code-copy taint range."
			);
		};
		auto assertExpectedCachedLocalTagTaintRange = [](
			CodeCopyTaintLocalTagRanges const& _ranges,
			size_t _offset,
			size_t _size,
			size_t _tagId
		)
		{
			assertThrow(
				std::find(_ranges.begin(), _ranges.end(), std::make_tuple(_offset, _size, _tagId)) != _ranges.end(),
				AssemblyException,
				"Invalid cached code-copy taint range."
			);
		};
		ByteRanges expectedCachedForeignReferences;
		CodeCopyTaintForeignTagRanges expectedCachedForeignTags;
		ByteRanges expectedCachedLocalTags;
		CodeCopyTaintLocalTagRanges expectedCachedLocalTagReferences;
		ByteRanges expectedCachedCurrentAddresses;
		ByteRanges expectedCachedComplementedCurrentAddresses;
		auto recordExpectedCachedByteTaintRange = [](
			ByteRanges& _expectedRanges,
			size_t _offset,
			size_t _size
		)
		{
			_expectedRanges.emplace_back(_offset, _size);
		};
		auto recordExpectedCachedForeignTagTaintRange = [](
			CodeCopyTaintForeignTagRanges& _expectedRanges,
			size_t _offset,
			size_t _size,
			size_t _subId,
			size_t _tagId
		)
		{
			_expectedRanges.emplace_back(_offset, _size, _subId, _tagId);
		};
		auto recordExpectedCachedLocalTagTaintRange = [](
			CodeCopyTaintLocalTagRanges& _expectedRanges,
			size_t _offset,
			size_t _size,
			size_t _tagId
		)
		{
			_expectedRanges.emplace_back(_offset, _size, _tagId);
		};
		auto assertExactCachedTaintRanges = [](auto _cachedRanges, auto _expectedRanges)
		{
			std::sort(_cachedRanges.begin(), _cachedRanges.end());
			std::sort(_expectedRanges.begin(), _expectedRanges.end());
			assertThrow(
				_cachedRanges == _expectedRanges,
				AssemblyException,
				"Invalid cached code-copy taint range."
			);
		};
		auto assertCachedBytes = [&](size_t _offset, bytes const& _expectedBytes, char const* _message)
		{
			if (_expectedBytes.empty())
				return;
			assertThrow(
				_offset <= m_assembledObject.bytecode.size() &&
				_expectedBytes.size() <= m_assembledObject.bytecode.size() - _offset,
				AssemblyException,
				_message
			);
			assertThrow(
				std::equal(
					_expectedBytes.begin(),
					_expectedBytes.end(),
					m_assembledObject.bytecode.begin() + static_cast<ptrdiff_t>(_offset)
				),
				AssemblyException,
				_message
			);
		};
		auto assertCachedZeroBytes = [&](size_t _offset, size_t _size, char const* _message)
		{
			assertCachedByteRange(_offset, _size, _message);
			assertThrow(
				std::all_of(
					m_assembledObject.bytecode.begin() + static_cast<ptrdiff_t>(_offset),
					m_assembledObject.bytecode.begin() + static_cast<ptrdiff_t>(checkedAddSize(_offset, _size)),
					[](uint8_t _byte) { return _byte == 0; }
				),
				AssemblyException,
				_message
			);
		};
		size_t cachedLocalBytecodeEnd = 0;
		std::vector<std::optional<std::pair<size_t, size_t>>> expectedCachedLocalItemRanges(m_items.size());
		auto recordCachedLocalItemRange = [&](
			size_t _itemIndex,
			size_t _itemStart,
			size_t _itemEnd,
			char const* _message
		)
		{
			assertThrow(_itemStart <= _itemEnd, AssemblyException, _message);
			auto& range = expectedCachedLocalItemRanges.at(_itemIndex);
			std::pair<size_t, size_t> const itemRange{_itemStart, _itemEnd};
			assertThrow(!range || *range == itemRange, AssemblyException, _message);
			range = itemRange;
			cachedLocalBytecodeEnd = std::max(cachedLocalBytecodeEnd, _itemEnd);
		};
		auto assertContiguousCachedLocalItemRanges = [&]()
		{
			size_t expectedItemStart = 0;
			for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			{
				if (item.type() == VerbatimBytecode && item.verbatimData().empty())
					continue;
				auto const& itemRange = expectedCachedLocalItemRanges.at(static_cast<size_t>(itemIndex));
				assertThrow(
					itemRange &&
					itemRange->first == expectedItemStart &&
					itemRange->second >= itemRange->first,
					AssemblyException,
					"Invalid cached bytecode."
				);
				expectedItemStart = itemRange->second;
			}
			assertThrow(
				cachedLocalBytecodeEnd == expectedItemStart,
				AssemblyException,
				"Invalid cached bytecode."
			);
		};
		std::map<size_t, size_t> expectedLiteralJumpTargets;
		auto recordExpectedLiteralJumpTarget = [&](size_t _bytecodeOffset, size_t _itemIndex)
		{
			auto [expectedLiteralJumpTarget, inserted] = expectedLiteralJumpTargets.emplace(_bytecodeOffset, _itemIndex);
			assertThrow(
				inserted || expectedLiteralJumpTarget->second == _itemIndex,
				AssemblyException,
				"Invalid cached literal jump target."
			);
		};
		struct CachedSubAssemblyEmission
		{
			Assembly const* assembly = nullptr;
			size_t subIdPath = 0;
			size_t bytecodeOffset = 0;
		};
		std::vector<CachedSubAssemblyEmission> cachedSubAssemblyEmissions;
		for (auto const& [bytecodeOffset, identifier]: m_assembledObject.linkReferences)
		{
			assertCachedByteRange(bytecodeOffset, hyperion::AddressBytes, "Invalid cached link reference.");
			assertThrow(!identifier.empty(), AssemblyException, "Invalid cached link reference.");
			assertThrow(cachedLibraryReferenceIsDeclared(identifier), AssemblyException, "Invalid cached link reference.");
		}
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			if (item.type() == PushLibraryAddress)
			{
				u256 const libraryHash = checkedHashReference(item, "Library");
				std::string const& identifier = requiredIdentifier(m_libraries, h256(libraryHash), "library");
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached link reference.");
				assertCachedByteRange(itemStart, 1, "Invalid cached link reference.");
				assertThrow(
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(pushInstruction(hyperion::AddressBytes)),
					AssemblyException,
					"Invalid cached link reference."
				);
				size_t const argumentOffset = checkedAddSize(itemStart, 1);
				assertCachedByteRange(argumentOffset, hyperion::AddressBytes, "Invalid cached link reference.");
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, hyperion::AddressBytes),
					"Invalid cached link reference."
				);
				addExpectedLinkReference(argumentOffset, identifier);
				auto linkReference = m_assembledObject.linkReferences.find(argumentOffset);
				assertThrow(
					linkReference != m_assembledObject.linkReferences.end() &&
					linkReference->second == identifier,
					AssemblyException,
					"Invalid cached link reference."
				);
			}
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			if (item.type() == PushSub)
			{
				assertThrow(item.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "Invalid cached link reference.");
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached link reference.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached link reference.");
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached link reference."
				);
				size_t subAssemblyOffset = readCachedSizeArgument(argumentOffset, argumentSize, "Invalid cached link reference.");
				size_t const subIdPath = static_cast<size_t>(item.data());
				Assembly const* subAssembly = cachedSubAssemblyById(subIdPath, "Invalid cached link reference.");
				assertThrow(subAssembly->m_assembled, AssemblyException, "Invalid cached link reference.");
				auto [
					tagsReferencedFromOutside,
					subTagsReferencedFromOutside,
					copiedTagsReferencedFromOutside,
					copiedSubTagsReferencedFromOutside
				] = referencesForSubAssembly(subIdPath);
				LinkerObject const& subObject = subAssembly->assemble(
					std::move(tagsReferencedFromOutside),
					std::move(subTagsReferencedFromOutside),
					allowsDeployTimeAddressInSubAssembly(subIdPath),
					std::move(copiedTagsReferencedFromOutside),
					std::move(copiedSubTagsReferencedFromOutside)
				);
				assertThrow(
					subAssemblyOffset <= m_assembledObject.bytecode.size() &&
					subObject.bytecode.size() <= m_assembledObject.bytecode.size() - subAssemblyOffset,
					AssemblyException,
					"Invalid cached link reference."
				);
				assertThrow(
					std::equal(
						subObject.bytecode.begin(),
						subObject.bytecode.end(),
						m_assembledObject.bytecode.begin() + static_cast<ptrdiff_t>(subAssemblyOffset)
					),
					AssemblyException,
					"Invalid cached link reference."
				);
				for (auto const& [subBytecodeOffset, identifier]: subObject.linkReferences)
					addExpectedLinkReference(checkedAddSize(subBytecodeOffset, subAssemblyOffset), identifier);
				cachedSubAssemblyEmissions.push_back({
					subAssembly,
					subIdPath,
					subAssemblyOffset
				});
			}
		std::map<LinkerObject, size_t> cachedSubObjectOffsets;
		for (CachedSubAssemblyEmission const& emission: cachedSubAssemblyEmissions)
		{
			assertThrow(emission.assembly, AssemblyException, "Invalid cached sub-assembly reference.");
			auto [subObjectOffset, inserted] = cachedSubObjectOffsets.emplace(
				emission.assembly->m_assembledObject,
				emission.bytecodeOffset
			);
			assertThrow(
				inserted || subObjectOffset->second == emission.bytecodeOffset,
				AssemblyException,
				"Invalid cached sub-assembly reference."
			);
		}
		assertThrow(
			m_assembledObject.linkReferences.size() == expectedLinkReferences.size(),
			AssemblyException,
			"Invalid cached link reference."
		);
		for (auto const& [bytecodeOffset, identifier]: expectedLinkReferences)
		{
			auto linkReference = m_assembledObject.linkReferences.find(bytecodeOffset);
			assertThrow(
				linkReference != m_assembledObject.linkReferences.end() &&
				linkReference->second == identifier,
				AssemblyException,
				"Invalid cached link reference."
			);
		}
		std::map<u256, std::pair<std::string, std::vector<size_t>>> immutableReferencesBySub;
		std::map<LinkerObject, size_t> referencedSubObjects;
		for (CachedSubAssemblyEmission const& emission: cachedSubAssemblyEmissions)
		{
			assertThrow(emission.assembly, AssemblyException, "Invalid cached immutable assignment.");
			LinkerObject const& subObject = emission.assembly->m_assembledObject;
			if (referencedSubObjects.emplace(subObject, 0).second && !subObject.immutableReferences.empty())
			{
				assertThrow(
					immutableReferencesBySub.empty(),
					AssemblyException,
					"More than one sub-assembly references immutables."
				);
				immutableReferencesBySub = subObject.immutableReferences;
			}
		}
		bool setsImmutables = false;
		bool pushesImmutables = false;
		std::set<u256> assignedImmutables;
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			if (item.type() == AssignImmutable)
			{
				u256 const immutableHash = checkedHashReference(item, "Immutable");
				(void)requiredIdentifier(m_immutables, h256(immutableHash), "immutable");
				auto immutableReferences = immutableReferencesBySub.find(immutableHash);
				size_t const occurrences =
					immutableReferences == immutableReferencesBySub.end() ?
					0 :
					immutableReferences->second.second.size();
				assertThrow(
					item.immutableOccurrences() && *item.immutableOccurrences() == occurrences,
					AssemblyException,
					"Invalid cached immutable assignment."
				);
				if (occurrences != 0)
				{
					assertThrow(
						assignedImmutables.emplace(immutableHash).second,
						AssemblyException,
						"Immutable assigned more than once."
					);
				}
				bytes expectedAssignImmutableBytecode;
				if (immutableReferences == immutableReferencesBySub.end())
				{
					expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::POP));
					expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::POP));
				}
				else
				{
					std::vector<size_t> const& offsets = immutableReferences->second.second;
					for (size_t offsetIndex = 0; offsetIndex < offsets.size(); ++offsetIndex)
					{
						if (offsetIndex != offsets.size() - 1)
						{
							expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::DUP2));
							expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::DUP2));
						}
						bytes offsetBytes = toCompactBigEndian(u256(offsets[offsetIndex]));
						expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(
							pushInstruction(static_cast<unsigned>(offsetBytes.size()))
						));
						expectedAssignImmutableBytecode += offsetBytes;
						expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::ADD));
						expectedAssignImmutableBytecode.push_back(static_cast<uint8_t>(Instruction::MSTORE));
					}
					immutableReferencesBySub.erase(immutableReferences);
				}
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached immutable assignment.");
				assertCachedBytes(
					itemStart,
					expectedAssignImmutableBytecode,
					"Invalid cached immutable assignment."
				);
				recordExpectedLiteralJumpTarget(
					itemStart,
					static_cast<size_t>(itemIndex)
				);
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, expectedAssignImmutableBytecode.size()),
					"Invalid cached immutable assignment."
				);
				setsImmutables = true;
			}
			else if (item.type() == PushImmutable)
				pushesImmutables = true;
		if (setsImmutables || pushesImmutables)
			assertThrow(
				setsImmutables != pushesImmutables,
				AssemblyException,
				"Cannot push and assign immutables in the same assembly subroutine."
			);
		assertThrow(
			immutableReferencesBySub.empty(),
			AssemblyException,
			"Invalid cached immutable assignment."
		);
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			if (item.type() == PushSubSize || item.type() == PushProgramSize)
			{
				size_t const itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached pushed value.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached pushed value.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached pushed value."
				);
				size_t const cachedValue = readCachedSizeArgument(argumentOffset, argumentSize, "Invalid cached pushed value.");
				size_t expectedValue = m_assembledObject.bytecode.size();
				if (item.type() == PushSubSize)
				{
					assertThrow(item.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "Invalid cached pushed value.");
					size_t const subIdPath = static_cast<size_t>(item.data());
					Assembly const* subAssembly = cachedSubAssemblyById(subIdPath, "Invalid cached pushed value.");
					auto [
						tagsReferencedFromOutside,
						subTagsReferencedFromOutside,
						copiedTagsReferencedFromOutside,
						copiedSubTagsReferencedFromOutside
					] = referencesForSubAssembly(subIdPath);
					expectedValue = subAssembly->assemble(
						std::move(tagsReferencedFromOutside),
						std::move(subTagsReferencedFromOutside),
						allowsDeployTimeAddressInSubAssembly(subIdPath),
						std::move(copiedTagsReferencedFromOutside),
						std::move(copiedSubTagsReferencedFromOutside)
					).bytecode.size();
				}
				assertThrow(
					item.pushedValue() && *item.pushedValue() == u512(expectedValue) && cachedValue == expectedValue,
					AssemblyException,
					"Invalid cached pushed value."
				);
			}
		std::set<h256> referencedDataHashes;
		for (AssemblyItem const& item: m_items)
			if (item.type() == PushData)
				referencedDataHashes.insert(h256(checkedHashReference(item, "Data")));
		size_t referencedDataSize = 0;
		for (auto const& [dataHash, dataBytes]: m_data)
			if (referencedDataHashes.count(dataHash))
				referencedDataSize = checkedAddSize(referencedDataSize, dataBytes.size());
		assertThrow(
			m_auxiliaryData.size() <= m_assembledObject.bytecode.size() &&
			referencedDataSize <= m_assembledObject.bytecode.size() - m_auxiliaryData.size(),
			AssemblyException,
			"Invalid cached data reference."
		);
		size_t const auxiliaryDataOffset = m_assembledObject.bytecode.size() - m_auxiliaryData.size();
		size_t const dataSectionOffset = auxiliaryDataOffset - referencedDataSize;
		size_t expectedDataOffset = dataSectionOffset;
		std::map<h256, size_t> expectedDataOffsets;
		for (auto const& [dataHash, dataBytes]: m_data)
			if (referencedDataHashes.count(dataHash))
			{
				expectedDataOffsets[dataHash] = expectedDataOffset;
				assertCachedBytes(expectedDataOffset, dataBytes, "Invalid cached data reference.");
				expectedDataOffset = checkedAddSize(expectedDataOffset, dataBytes.size());
			}
		assertThrow(
			expectedDataOffset == auxiliaryDataOffset,
			AssemblyException,
			"Invalid cached data reference."
		);
		assertCachedBytes(auxiliaryDataOffset, m_auxiliaryData, "Invalid cached auxiliary data.");
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
		{
			switch (item.type())
			{
			case Operation:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedByteRange(itemStart, 1, "Invalid cached bytecode.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, 1),
					"Invalid cached bytecode."
				);
				assertThrow(
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(item.instruction()),
					AssemblyException,
					"Invalid cached bytecode."
				);
				break;
			}
			case Push:
			{
				unsigned bytesRequired = numberEncodingSize(item.data());
				bytes expectedBytecode{static_cast<uint8_t>(pushInstruction(bytesRequired))};
				if (bytesRequired > 0)
				{
					expectedBytecode.resize(1 + bytesRequired);
					bytesRef argumentBytes(expectedBytecode.data() + 1, bytesRequired);
					toBigEndian(item.data(), argumentBytes);
				}
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedBytes(
					itemStart,
					expectedBytecode,
					"Invalid cached bytecode."
				);
				recordExpectedLiteralJumpTarget(
					itemStart,
					static_cast<size_t>(itemIndex)
				);
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, expectedBytecode.size()),
					"Invalid cached bytecode."
				);
				break;
			}
			case VerbatimBytecode:
				if (item.verbatimData().empty())
					break;
				{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedBytes(
					itemStart,
					item.verbatimData(),
					"Invalid cached bytecode."
				);
				recordExpectedLiteralJumpTarget(
					itemStart,
					static_cast<size_t>(itemIndex)
				);
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, item.verbatimData().size()),
					"Invalid cached bytecode."
				);
				break;
				}
			case Tag:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedByteRange(itemStart, 1, "Invalid cached bytecode.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, 1),
					"Invalid cached bytecode."
				);
				assertThrow(
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(Instruction::JUMPDEST),
					AssemblyException,
					"Invalid cached bytecode."
				);
				break;
			}
			case PushLibraryAddress:
			case PushDeployTimeAddress:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedByteRange(itemStart, 1, "Invalid cached bytecode.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, 1 + hyperion::AddressBytes),
					"Invalid cached bytecode."
				);
				assertThrow(
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(pushInstruction(hyperion::AddressBytes)),
					AssemblyException,
					"Invalid cached bytecode."
				);
				assertCachedZeroBytes(checkedAddSize(itemStart, 1), hyperion::AddressBytes, "Invalid cached bytecode.");
				break;
			}
			case PushImmutable:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached bytecode.");
				assertCachedByteRange(itemStart, 1, "Invalid cached bytecode.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(itemStart, 1 + VMWordBytes),
					"Invalid cached bytecode."
				);
				assertThrow(
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(pushInstruction(VMWordBytes)),
					AssemblyException,
					"Invalid cached bytecode."
				);
				assertCachedZeroBytes(checkedAddSize(itemStart, 1), VMWordBytes, "Invalid cached bytecode.");
				break;
			}
			default:
				break;
			}
		}
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
		{
			switch (item.type())
			{
			case PushTag:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached code-copy taint range.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached code-copy taint range.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached code-copy taint range."
				);
				size_t const cachedTagPosition = readCachedSizeArgument(
					argumentOffset,
					argumentSize,
					"Invalid cached tag reference."
				);
				auto [subId, tagId] = item.splitForeignPushTag();
				if (subId != std::numeric_limits<size_t>::max())
				{
					Assembly const* subAssembly = cachedSubAssemblyById(subId, "Invalid cached tag reference.");
					auto [
						tagsReferencedFromOutside,
						subTagsReferencedFromOutside,
						copiedTagsReferencedFromOutside,
						copiedSubTagsReferencedFromOutside
					] = referencesForSubAssembly(subId);
					LinkerObject const& subObject = subAssembly->assemble(
						std::move(tagsReferencedFromOutside),
						std::move(subTagsReferencedFromOutside),
						allowsDeployTimeAddressInSubAssembly(subId),
						std::move(copiedTagsReferencedFromOutside),
						std::move(copiedSubTagsReferencedFromOutside)
					);
					auto const subObjectOffset = cachedSubObjectOffsets.find(subObject);
					assertThrow(
						subObjectOffset != cachedSubObjectOffsets.end(),
						AssemblyException,
						"Invalid cached tag reference."
					);
					auto const tagPosition = subAssembly->m_tagPositionsInBytecode.find(tagId);
					assertThrow(
						tagPosition != subAssembly->m_tagPositionsInBytecode.end(),
						AssemblyException,
						"Invalid cached tag reference."
					);
					assertThrow(
						cachedTagPosition == checkedAddSize(subObjectOffset->second, tagPosition->second),
						AssemblyException,
						"Invalid cached tag reference."
					);
					recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, argumentOffset, argumentSize);
					assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.foreignReferences, argumentOffset, argumentSize);
					recordExpectedCachedForeignTagTaintRange(expectedCachedForeignTags, argumentOffset, argumentSize, subId, tagId);
					assertExpectedCachedForeignTagTaintRange(
						m_codeCopyTaintRangesInBytecode.foreignTags,
						argumentOffset,
						argumentSize,
						subId,
						tagId
					);
				}
				else
				{
					auto const tagPosition = m_tagPositionsInBytecode.find(tagId);
					assertThrow(
						tagPosition != m_tagPositionsInBytecode.end() &&
						cachedTagPosition == tagPosition->second,
						AssemblyException,
						"Invalid cached tag reference."
					);
					recordExpectedCachedByteTaintRange(expectedCachedLocalTags, argumentOffset, argumentSize);
					assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.localTags, argumentOffset, argumentSize);
					recordExpectedCachedLocalTagTaintRange(expectedCachedLocalTagReferences, argumentOffset, argumentSize, tagId);
					assertExpectedCachedLocalTagTaintRange(
						m_codeCopyTaintRangesInBytecode.localTagReferences,
						argumentOffset,
						argumentSize,
						tagId
					);
				}
				break;
			}
			case PushData:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached data reference.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached data reference.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached data reference."
				);
				size_t const dataOffset = readCachedSizeArgument(argumentOffset, argumentSize, "Invalid cached data reference.");
				h256 const dataHash = h256(checkedHashReference(item, "Data"));
				auto const dataItem = m_data.find(dataHash);
				assertThrow(dataItem != m_data.end(), AssemblyException, "Invalid cached data reference.");
				auto const canonicalDataOffset = expectedDataOffsets.find(dataHash);
				assertThrow(
					canonicalDataOffset != expectedDataOffsets.end() &&
					dataOffset == canonicalDataOffset->second,
					AssemblyException,
					"Invalid cached data reference."
				);
				bytes const& dataBytes = dataItem->second;
				assertThrow(
					dataOffset <= m_assembledObject.bytecode.size() &&
					dataBytes.size() <= m_assembledObject.bytecode.size() - dataOffset,
					AssemblyException,
					"Invalid cached data reference."
				);
				assertThrow(
					std::equal(
						dataBytes.begin(),
						dataBytes.end(),
						m_assembledObject.bytecode.begin() + static_cast<ptrdiff_t>(dataOffset)
					),
					AssemblyException,
					"Invalid cached data reference."
				);
				recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.foreignReferences, argumentOffset, argumentSize);
				break;
			}
			case PushSub:
			case PushLibraryAddress:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached code-copy taint range.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached code-copy taint range.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached code-copy taint range."
				);
				recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.foreignReferences, argumentOffset, argumentSize);
				break;
			}
			case PushImmutable:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached code-copy taint range.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached code-copy taint range.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached code-copy taint range."
				);
				recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.foreignReferences, argumentOffset, argumentSize);
				recordExpectedCachedByteTaintRange(expectedCachedCurrentAddresses, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.currentAddresses, argumentOffset, argumentSize);
				recordExpectedCachedByteTaintRange(expectedCachedComplementedCurrentAddresses, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(
					m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses,
					argumentOffset,
					argumentSize
				);
				break;
			}
			case PushDeployTimeAddress:
			{
				size_t itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached code-copy taint range.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached code-copy taint range.");
				recordExpectedLiteralJumpTarget(itemStart, static_cast<size_t>(itemIndex));
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached code-copy taint range."
				);
				recordExpectedCachedByteTaintRange(expectedCachedCurrentAddresses, argumentOffset, argumentSize);
				assertExpectedCachedByteTaintRange(m_codeCopyTaintRangesInBytecode.currentAddresses, argumentOffset, argumentSize);
				break;
			}
			default:
				break;
				}
			}
			assertContiguousCachedLocalItemRanges();
			size_t firstAppendedSectionOffset = dataSectionOffset;
			for (auto const& [subObject, subObjectOffset]: cachedSubObjectOffsets)
			{
				(void)subObject;
				firstAppendedSectionOffset = std::min(firstAppendedSectionOffset, subObjectOffset);
			}
			if (!m_subs.empty() || !m_data.empty() || !m_auxiliaryData.empty())
			{
				assertCachedByteRange(cachedLocalBytecodeEnd, 1, "Invalid cached bytecode.");
				assertThrow(
					m_assembledObject.bytecode[cachedLocalBytecodeEnd] == static_cast<uint8_t>(Instruction::INVALID) &&
					checkedAddSize(cachedLocalBytecodeEnd, 1) == firstAppendedSectionOffset,
					AssemblyException,
					"Invalid cached bytecode."
				);
				size_t expectedSubObjectOffset = checkedAddSize(cachedLocalBytecodeEnd, 1);
				std::map<LinkerObject, size_t> expectedSubObjectOffsets;
				std::vector<size_t> expectedSubObjectIds;
				for (AssemblyItem const& item: m_items)
					if (item.type() == PushSub)
					{
						assertThrow(
							item.data() <= std::numeric_limits<size_t>::max(),
							AssemblyException,
							"Invalid cached sub-assembly reference."
						);
						expectedSubObjectIds.push_back(static_cast<size_t>(item.data()));
					}
				std::sort(expectedSubObjectIds.begin(), expectedSubObjectIds.end());
				for (size_t subIdPath: expectedSubObjectIds)
				{
					Assembly const* subAssembly = cachedSubAssemblyById(subIdPath, "Invalid cached sub-assembly reference.");
					auto [
						tagsReferencedFromOutside,
						subTagsReferencedFromOutside,
						copiedTagsReferencedFromOutside,
						copiedSubTagsReferencedFromOutside
					] = referencesForSubAssembly(subIdPath);
					LinkerObject const& subObject = subAssembly->assemble(
						std::move(tagsReferencedFromOutside),
						std::move(subTagsReferencedFromOutside),
						allowsDeployTimeAddressInSubAssembly(subIdPath),
						std::move(copiedTagsReferencedFromOutside),
						std::move(copiedSubTagsReferencedFromOutside)
					);
					if (expectedSubObjectOffsets.count(subObject))
						continue;
					expectedSubObjectOffsets[subObject] = expectedSubObjectOffset;
					expectedSubObjectOffset = checkedAddSize(expectedSubObjectOffset, subObject.bytecode.size());
				}
				auto subObjectOffsetsMatch = [](
					std::map<LinkerObject, size_t> const& _left,
					std::map<LinkerObject, size_t> const& _right
				)
				{
					if (_left.size() != _right.size())
						return false;
					auto left = _left.begin();
					auto right = _right.begin();
					for (; left != _left.end(); ++left, ++right)
						if (
							left->second != right->second ||
							left->first < right->first ||
							right->first < left->first
						)
							return false;
					return true;
				};
				assertThrow(
					subObjectOffsetsMatch(expectedSubObjectOffsets, cachedSubObjectOffsets) &&
					expectedSubObjectOffset == dataSectionOffset,
					AssemblyException,
					"Invalid cached sub-assembly reference."
				);
			}
			else
				assertThrow(
					cachedLocalBytecodeEnd == m_assembledObject.bytecode.size(),
					AssemblyException,
					"Invalid cached bytecode."
				);
			for (CachedSubAssemblyEmission const& emission: cachedSubAssemblyEmissions)
			{
				assertThrow(emission.assembly, AssemblyException, "Invalid cached code-copy taint range.");
				std::vector<size_t> subAssemblyPath = decodeSubPath(emission.subIdPath);
				for (auto const& [offset, size]: emission.assembly->m_codeCopyTaintRangesInBytecode.foreignReferences)
				{
					size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
					recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, relocatedOffset, size);
					assertExpectedCachedByteTaintRange(
						m_codeCopyTaintRangesInBytecode.foreignReferences,
						relocatedOffset,
						size
					);
				}
				for (auto const& [offset, size, subId, tagId]: emission.assembly->m_codeCopyTaintRangesInBytecode.foreignTags)
				{
					std::vector<size_t> translatedPath = subAssemblyPath;
					std::vector<size_t> relativePath = emission.assembly->decodeSubPath(subId);
					translatedPath.insert(translatedPath.end(), relativePath.begin(), relativePath.end());
					if (std::optional<size_t> translatedSubId = subPathId(translatedPath))
					{
						size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
						recordExpectedCachedForeignTagTaintRange(
							expectedCachedForeignTags,
							relocatedOffset,
							size,
							*translatedSubId,
							tagId
						);
						assertExpectedCachedForeignTagTaintRange(
							m_codeCopyTaintRangesInBytecode.foreignTags,
							relocatedOffset,
							size,
							*translatedSubId,
							tagId
						);
					}
				}
				for (auto const& [offset, size]: emission.assembly->m_codeCopyTaintRangesInBytecode.localTags)
				{
					size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
					recordExpectedCachedByteTaintRange(expectedCachedLocalTags, relocatedOffset, size);
					assertExpectedCachedByteTaintRange(
						m_codeCopyTaintRangesInBytecode.localTags,
						relocatedOffset,
						size
					);
				}
				for (auto const& [offset, size, tagId]: emission.assembly->m_codeCopyTaintRangesInBytecode.localTagReferences)
				{
					size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
					recordExpectedCachedByteTaintRange(expectedCachedForeignReferences, relocatedOffset, size);
					assertExpectedCachedByteTaintRange(
						m_codeCopyTaintRangesInBytecode.foreignReferences,
						relocatedOffset,
						size
					);
					recordExpectedCachedForeignTagTaintRange(
						expectedCachedForeignTags,
						relocatedOffset,
						size,
						emission.subIdPath,
						tagId
					);
					assertExpectedCachedForeignTagTaintRange(
						m_codeCopyTaintRangesInBytecode.foreignTags,
						relocatedOffset,
						size,
						emission.subIdPath,
						tagId
					);
				}
				for (auto const& [offset, size]: emission.assembly->m_codeCopyTaintRangesInBytecode.currentAddresses)
				{
					size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
					recordExpectedCachedByteTaintRange(expectedCachedCurrentAddresses, relocatedOffset, size);
					assertExpectedCachedByteTaintRange(
						m_codeCopyTaintRangesInBytecode.currentAddresses,
						relocatedOffset,
						size
					);
				}
				for (auto const& [offset, size]: emission.assembly->m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses)
				{
					size_t const relocatedOffset = checkedAddSize(offset, emission.bytecodeOffset);
					recordExpectedCachedByteTaintRange(expectedCachedComplementedCurrentAddresses, relocatedOffset, size);
					assertExpectedCachedByteTaintRange(
						m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses,
						relocatedOffset,
						size
					);
				}
			}
			assertExactCachedTaintRanges(m_codeCopyTaintRangesInBytecode.foreignReferences, expectedCachedForeignReferences);
			assertExactCachedTaintRanges(m_codeCopyTaintRangesInBytecode.foreignTags, expectedCachedForeignTags);
			assertExactCachedTaintRanges(m_codeCopyTaintRangesInBytecode.localTags, expectedCachedLocalTags);
			assertExactCachedTaintRanges(m_codeCopyTaintRangesInBytecode.localTagReferences, expectedCachedLocalTagReferences);
			assertExactCachedTaintRanges(m_codeCopyTaintRangesInBytecode.currentAddresses, expectedCachedCurrentAddresses);
			assertExactCachedTaintRanges(
				m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses,
				expectedCachedComplementedCurrentAddresses
			);
		std::map<u256, std::pair<std::string, std::vector<size_t>>> expectedImmutableReferences;
		for (auto const& [itemIndex, item]: m_items | ranges::views::enumerate)
			if (item.type() == PushImmutable)
			{
				u256 const immutableHash = checkedHashReference(item, "Immutable");
				std::string const& identifier = requiredIdentifier(m_immutables, h256(immutableHash), "immutable");
				size_t const itemStart = cachedItemStart(static_cast<size_t>(itemIndex), "Invalid cached immutable reference.");
				auto [argumentOffset, argumentSize] = cachedPushArgument(itemStart, "Invalid cached immutable reference.");
				assertThrow(
					argumentSize == VMWordBytes &&
					m_assembledObject.bytecode[itemStart] == static_cast<uint8_t>(pushInstruction(VMWordBytes)),
					AssemblyException,
					"Invalid cached immutable reference."
				);
				recordCachedLocalItemRange(
					static_cast<size_t>(itemIndex),
					itemStart,
					checkedAddSize(argumentOffset, argumentSize),
					"Invalid cached immutable reference."
				);
				auto& expectedReference = expectedImmutableReferences[immutableHash];
				if (expectedReference.first.empty())
					expectedReference.first = identifier;
				assertThrow(
					expectedReference.first == identifier,
					AssemblyException,
					"Immutable identifier hash mismatch."
				);
				expectedReference.second.push_back(argumentOffset);
			}
		for (auto const& [immutableHash, immutableReference]: m_assembledObject.immutableReferences)
		{
			auto const& [identifier, bytecodeOffsets] = immutableReference;
			auto expectedReference = expectedImmutableReferences.find(immutableHash);
			assertThrow(
				expectedReference != expectedImmutableReferences.end() &&
				bytecodeOffsets == expectedReference->second.second,
				AssemblyException,
				"Invalid cached immutable reference."
			);
			assertThrow(
				expectedReference->second.first == identifier,
				AssemblyException,
				"Immutable identifier hash mismatch."
			);
			for (size_t bytecodeOffset: bytecodeOffsets)
			{
				assertCachedByteRange(bytecodeOffset, VMWordBytes, "Invalid cached immutable reference.");
				assertThrow(bytecodeOffset > 0, AssemblyException, "Invalid cached immutable reference.");
				auto const literalJumpTarget = m_literalJumpTargetsInBytecode.find(bytecodeOffset - 1);
				assertThrow(
					literalJumpTarget != m_literalJumpTargetsInBytecode.end() &&
					literalJumpTarget->second < m_items.size(),
					AssemblyException,
					"Invalid cached immutable reference."
				);
				AssemblyItem const& targetItem = m_items[literalJumpTarget->second];
				assertThrow(
					targetItem.type() == PushImmutable &&
					checkedHashReference(targetItem, "Immutable") == immutableHash &&
					m_assembledObject.bytecode[bytecodeOffset - 1] == static_cast<uint8_t>(pushInstruction(VMWordBytes)),
					AssemblyException,
					"Invalid cached immutable reference."
				);
			}
		}
		for (auto const& [immutableHash, expectedReferences]: expectedImmutableReferences)
		{
			assertThrow(
				m_assembledObject.immutableReferences.count(immutableHash) != 0 &&
				m_assembledObject.immutableReferences.at(immutableHash).second == expectedReferences.second,
				AssemblyException,
				"Invalid cached immutable reference."
			);
		}
			std::map<std::string, LinkerObject::FunctionDebugData> expectedFunctionDebugData;
			std::map<std::pair<size_t, std::string>, LinkerObject::FunctionDebugData> mergedSubFunctionDebugData;
			std::vector<CachedSubAssemblyEmission> sortedCachedSubAssemblyEmissions = cachedSubAssemblyEmissions;
			std::sort(
				sortedCachedSubAssemblyEmissions.begin(),
				sortedCachedSubAssemblyEmissions.end(),
				[](CachedSubAssemblyEmission const& _left, CachedSubAssemblyEmission const& _right)
				{
					return std::tie(_left.subIdPath, _left.bytecodeOffset, _left.assembly) <
						std::tie(_right.subIdPath, _right.bytecodeOffset, _right.assembly);
				}
			);
			for (CachedSubAssemblyEmission const& emission: sortedCachedSubAssemblyEmissions)
			{
				assertThrow(emission.assembly, AssemblyException, "Invalid cached function debug data.");
				for (auto const& [name, debugData]: emission.assembly->m_assembledObject.functionDebugData)
				{
					LinkerObject::FunctionDebugData relocatedDebugData = debugData;
					if (relocatedDebugData.bytecodeOffset)
						relocatedDebugData.bytecodeOffset = checkedAddSize(
							*relocatedDebugData.bytecodeOffset,
							emission.bytecodeOffset
						);
					relocatedDebugData.instructionIndex = std::nullopt;

					auto [mergedIt, merged] = mergedSubFunctionDebugData.emplace(
						std::make_pair(emission.bytecodeOffset, name),
						relocatedDebugData
					);
					if (!merged && equalFunctionDebugData(mergedIt->second, relocatedDebugData))
						continue;

					insertFunctionDebugData(expectedFunctionDebugData, name, relocatedDebugData);
				}
			}
			for (auto const& [name, tagInfo]: m_namedTags)
			{
				auto tagPosition = m_tagPositionsInBytecode.find(tagInfo.id);
				std::optional<size_t> position =
					tagPosition == m_tagPositionsInBytecode.end() ?
					std::nullopt :
					std::optional<size_t>{tagPosition->second};
				std::optional<size_t> tagIndex;
				for (auto&& [index, item]: m_items | ranges::views::enumerate)
					if (item.type() == Tag && static_cast<size_t>(item.data()) == tagInfo.id)
					{
						tagIndex = index;
						break;
					}
				insertFunctionDebugData(
					expectedFunctionDebugData,
					name,
					LinkerObject::FunctionDebugData{position, tagIndex, tagInfo.sourceID, tagInfo.params, tagInfo.returns}
				);
			}
			assertThrow(
				m_assembledObject.functionDebugData.size() == expectedFunctionDebugData.size(),
				AssemblyException,
				"Invalid cached function debug data."
			);
			for (auto const& [name, debugData]: expectedFunctionDebugData)
			{
				auto cachedDebugData = m_assembledObject.functionDebugData.find(name);
				assertThrow(
					cachedDebugData != m_assembledObject.functionDebugData.end() &&
					equalFunctionDebugData(cachedDebugData->second, debugData),
					AssemblyException,
					"Invalid cached function debug data."
				);
				}
				for (auto const& [name, debugData]: m_assembledObject.functionDebugData)
				{
					assertThrow(!name.empty(), AssemblyException, "Invalid cached function debug data.");
					assertThrow(
						validFunctionStackSlots(debugData.params, debugData.returns),
						AssemblyException,
						"Invalid cached function debug data."
					);
					if (debugData.bytecodeOffset)
					{
						assertCachedByteRange(*debugData.bytecodeOffset, 1, "Invalid cached function debug data.");
						assertThrow(
							m_assembledObject.bytecode[*debugData.bytecodeOffset] == static_cast<uint8_t>(Instruction::JUMPDEST),
							AssemblyException,
							"Invalid cached function debug data."
						);
					}
					if (debugData.instructionIndex)
					{
						assertThrow(
							*debugData.instructionIndex < m_items.size() &&
							m_items[*debugData.instructionIndex].type() == Tag,
							AssemblyException,
							"Invalid cached function debug data."
						);
						assertThrow(debugData.bytecodeOffset, AssemblyException, "Invalid cached function debug data.");
						auto const literalJumpTarget = m_literalJumpTargetsInBytecode.find(*debugData.bytecodeOffset);
						assertThrow(
							literalJumpTarget != m_literalJumpTargetsInBytecode.end() &&
							literalJumpTarget->second == *debugData.instructionIndex,
							AssemblyException,
							"Invalid cached function debug data."
						);
						AssemblyItem const& targetItem = m_items[*debugData.instructionIndex];
						bool matchesNamedTag = false;
						for (auto const& [localName, tagInfo]: m_namedTags)
						{
							(void)localName;
							if (
								targetItem.data() == tagInfo.id &&
								debugData.sourceID == tagInfo.sourceID &&
								debugData.params == tagInfo.params &&
								debugData.returns == tagInfo.returns
							)
							{
								matchesNamedTag = true;
								break;
							}
						}
						assertThrow(matchesNamedTag, AssemblyException, "Invalid cached function debug data.");
					}
				}
				for (auto const& [tagId, bytecodeOffset]: m_tagPositionsInBytecode)
				{
					assertThrow(
						bytecodeOffset < m_assembledObject.bytecode.size(),
						AssemblyException,
						"Invalid cached tag position."
					);
					assertThrow(
						tagId == 0 ||
						m_assembledObject.bytecode[bytecodeOffset] == static_cast<uint8_t>(Instruction::JUMPDEST),
						AssemblyException,
						"Invalid cached tag position."
					);
					if (tagId != 0)
					{
						auto const literalJumpTarget = m_literalJumpTargetsInBytecode.find(bytecodeOffset);
						assertThrow(
							literalJumpTarget != m_literalJumpTargetsInBytecode.end() &&
							literalJumpTarget->second < m_items.size(),
							AssemblyException,
							"Invalid cached tag position."
						);
						AssemblyItem const& targetItem = m_items[literalJumpTarget->second];
						assertThrow(
							targetItem.type() == Tag && targetItem.data() == tagId,
							AssemblyException,
							"Invalid cached tag position."
						);
					}
				}
		for (AssemblyItem const& item: m_items)
			if (item.type() == Tag)
				assertThrow(
					m_tagPositionsInBytecode.count(static_cast<size_t>(item.data())) != 0,
					AssemblyException,
					"Invalid cached tag position."
				);
		std::optional<size_t> expectedErrorTagPosition;
		for (auto const& [bytecodeOffset, itemIndex]: m_literalJumpTargetsInBytecode)
			if (itemIndex < m_items.size() && m_items[itemIndex].type() != Tag)
			{
				expectedErrorTagPosition = bytecodeOffset;
				break;
			}
		auto const errorTagPosition = m_tagPositionsInBytecode.find(0);
		if (expectedErrorTagPosition)
		{
			assertThrow(
				errorTagPosition != m_tagPositionsInBytecode.end() &&
				errorTagPosition->second == *expectedErrorTagPosition &&
				m_assembledObject.bytecode[*expectedErrorTagPosition] != static_cast<uint8_t>(Instruction::JUMPDEST),
				AssemblyException,
				"Invalid cached tag position."
			);
		}
		else
			assertThrow(
				errorTagPosition == m_tagPositionsInBytecode.end(),
				AssemblyException,
				"Invalid cached tag position."
			);
		assertThrow(
			m_literalJumpTargetsInBytecode == expectedLiteralJumpTargets,
			AssemblyException,
			"Invalid cached literal jump target."
		);
		for (auto const& [bytecodeOffset, itemIndex]: m_literalJumpTargetsInBytecode)
		{
			assertThrow(
				bytecodeOffset < m_assembledObject.bytecode.size() &&
				itemIndex < m_items.size(),
				AssemblyException,
				"Invalid cached literal jump target."
			);
			AssemblyItem const& targetItem = m_items[itemIndex];
			assertThrow(
				targetItem.type() != Tag ||
				m_assembledObject.bytecode[bytecodeOffset] == static_cast<uint8_t>(Instruction::JUMPDEST),
				AssemblyException,
				"Invalid cached literal jump target."
			);
			if (targetItem.type() == Tag)
			{
				auto const tagPosition = m_tagPositionsInBytecode.find(static_cast<size_t>(targetItem.data()));
				assertThrow(
					tagPosition != m_tagPositionsInBytecode.end() &&
					tagPosition->second == bytecodeOffset,
					AssemblyException,
					"Invalid cached literal jump target."
				);
			}
		}
		assertCachedTaintByteRanges(
			m_codeCopyTaintRangesInBytecode.foreignReferences,
			"Invalid cached code-copy taint range."
		);
		for (auto const& [offset, size, subId, tagId]: m_codeCopyTaintRangesInBytecode.foreignTags)
		{
			assertCachedByteRange(offset, size, "Invalid cached code-copy taint range.");
			if (tagId != 0)
				assertThrow(
					cachedSubAssemblyById(subId, "Invalid cached code-copy taint range.")->declaresTag(tagId),
					AssemblyException,
					"Invalid cached code-copy taint range."
				);
			else
				(void)cachedSubAssemblyById(subId, "Invalid cached code-copy taint range.");
		}
		assertCachedTaintByteRanges(
			m_codeCopyTaintRangesInBytecode.localTags,
			"Invalid cached code-copy taint range."
		);
		for (auto const& [offset, size, tagId]: m_codeCopyTaintRangesInBytecode.localTagReferences)
		{
			assertCachedByteRange(offset, size, "Invalid cached code-copy taint range.");
			assertThrow(
				tagId == 0 || declaresTag(tagId),
				AssemblyException,
				"Invalid cached code-copy taint range."
			);
		}
		assertCachedTaintByteRanges(
			m_codeCopyTaintRangesInBytecode.currentAddresses,
			"Invalid cached code-copy taint range."
		);
		assertCachedTaintByteRanges(
			m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses,
			"Invalid cached code-copy taint range."
		);
	};
	auto clearAssemblyCache = [](Assembly const* _assembly)
	{
		_assembly->m_assembledObject = LinkerObject{};
		_assembly->m_tagPositionsInBytecode.clear();
		_assembly->m_literalJumpTargetsInBytecode.clear();
		_assembly->m_codeCopyTaintRangesInBytecode.clear();
		_assembly->m_assembled = false;
		_assembly->m_assembledInitialStackHeight = 0;
		for (AssemblyItem const& item: _assembly->m_items)
		{
			item.clearImmutableOccurrences();
			item.clearPushedValue();
		}
	};
	ScopeGuard clearItemMetadataOnFailure([&]() {
		if (assemblySucceeded)
			return;
		for (Assembly const* assembly: subAssembliesInTree)
			if (!assembliesAlreadyAssembled.count(assembly))
				clearAssemblyCache(assembly);
		for (AssemblyItem const* item: itemsWithImmutableOccurrences)
			item->clearImmutableOccurrences();
		for (AssemblyItem const* item: itemsWithPushedValues)
			item->clearPushedValue();
	});

	// Return the already assembled object, if present.
	if (m_assembled)
	{
		(void)_initialStackHeight;
		assembleExternallyReferencedSubAssemblies();
		assemblySucceeded = true;
		return m_assembledObject;
	}
	// Otherwise ensure the object is actually clear.
	assertThrow(m_assembledObject.linkReferences.empty(), AssemblyException, "Unexpected link references.");

	LinkerObject ret;
	std::map<size_t, size_t> tagPositionsInBytecode;
	auto assembleSubAssembly = [&](size_t _subIdPath) -> LinkerObject const&
	{
		auto [
			tagsReferencedFromOutside,
			subTagsReferencedFromOutside,
			copiedTagsReferencedFromOutside,
			copiedSubTagsReferencedFromOutside
		] = referencesForSubAssembly(_subIdPath);
		return subAssemblyById(_subIdPath)->assemble(
			std::move(tagsReferencedFromOutside),
			std::move(subTagsReferencedFromOutside),
			allowsDeployTimeAddressInSubAssembly(_subIdPath),
			std::move(copiedTagsReferencedFromOutside),
			std::move(copiedSubTagsReferencedFromOutside)
		);
	};

	size_t maxSubTagPosition = 1;
	size_t subAssembliesSize = 0;
	std::map<u256, std::pair<std::string, std::vector<size_t>>> immutableReferencesBySub;
	auto collectImmutableReferences = [&](LinkerObject const& _linkerObject)
	{
		if (_linkerObject.immutableReferences.empty())
			return;
		assertThrow(
			immutableReferencesBySub.empty() || immutableReferencesBySub == _linkerObject.immutableReferences,
			AssemblyException,
			"More than one sub-assembly references immutables."
		);
		immutableReferencesBySub = _linkerObject.immutableReferences;
	};
	auto scanTagPositions = [&](Assembly const& _assembly)
	{
		for (auto const& tagPosition: _assembly.m_tagPositionsInBytecode)
		{
			size_t tagPos = tagPosition.second;
			if (tagPos != std::numeric_limits<size_t>::max() && tagPos > maxSubTagPosition)
				maxSubTagPosition = tagPos;
		}
	};
	for (AssemblyItem const& i: m_items)
		if (i.type() == PushTag)
		{
			auto [subId, tagId] = i.splitForeignPushTag();
			(void)tagId;
			if (subId != std::numeric_limits<size_t>::max())
			{
				Assembly const* subAssembly = subAssemblyById(subId);
				assembleSubAssembly(subId);
				scanTagPositions(*subAssembly);
			}
		}
	std::map<LinkerObject, size_t> referencedSubObjects;
	for (AssemblyItem const& i: m_items)
		if (i.type() == PushSub || i.type() == PushSubSize)
		{
			assertThrow(i.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "");
			Assembly const* subAssembly = subAssemblyById(static_cast<size_t>(i.data()));
			LinkerObject const& linkerObject = assembleSubAssembly(static_cast<size_t>(i.data()));
			scanTagPositions(*subAssembly);
			if (i.type() == PushSub && referencedSubObjects.emplace(linkerObject, 0).second)
			{
				subAssembliesSize = checkedAddSize(subAssembliesSize, linkerObject.bytecode.size());
				collectImmutableReferences(linkerObject);
			}
		}
	for (size_t subIdPath: m_immutableValidationSubAssemblies)
		collectImmutableReferences(assembleSubAssembly(subIdPath));

	bool setsImmutables = false;
	bool pushesImmutables = false;
	std::set<u256> assignedImmutables;

	for (auto const& i: m_items)
		if (i.type() == PushLibraryAddress)
		{
			u256 const libraryHash = checkedHashReference(i, "Library");
			(void)requiredIdentifier(m_libraries, h256(libraryHash), "library");
		}
		else if (i.type() == AssignImmutable)
		{
			u256 const immutableHash = checkedHashReference(i, "Immutable");
			(void)requiredIdentifier(m_immutables, h256(immutableHash), "immutable");
			auto const immutableReferences = immutableReferencesBySub.find(immutableHash);
			size_t const occurrences =
				immutableReferences == immutableReferencesBySub.end() ?
				0 :
				immutableReferences->second.second.size();
			if (occurrences != 0)
				assertThrow(
					assignedImmutables.emplace(immutableHash).second,
					AssemblyException,
					"Immutable assigned more than once."
				);
			i.setImmutableOccurrences(occurrences);
			itemsWithImmutableOccurrences.push_back(&i);
			setsImmutables = true;
		}
		else if (i.type() == PushImmutable)
		{
			u256 const immutableHash = checkedHashReference(i, "Immutable");
			(void)requiredIdentifier(m_immutables, h256(immutableHash), "immutable");
			pushesImmutables = true;
		}
	if (setsImmutables || pushesImmutables)
		assertThrow(
			setsImmutables != pushesImmutables,
			AssemblyException,
			"Cannot push and assign immutables in the same assembly subroutine."
		);
	assembleExternallyReferencedSubAssemblies();

		std::map<size_t, std::pair<size_t, size_t>> tagRef;
		std::multimap<h256, size_t> dataRef;
		std::multimap<size_t, size_t> subRef;
		std::vector<std::pair<size_t, AssemblyItem const*>> sizeRef; ///< Pointers to code locations where the size of the program is inserted
		bool pushesProgramSize = false;
		for (AssemblyItem const& item: m_items)
			if (item.type() == PushProgramSize)
			{
				pushesProgramSize = true;
				break;
			}

		unsigned legacyReferenceSizeSeed = std::max<unsigned>(1, numberEncodingSize(maxSubTagPosition));
		size_t legacyBytesRequiredForCode = legacyCodeSizeLowerBound(legacyReferenceSizeSeed);
		size_t legacyBytesRequiredIncludingSubAssemblies = checkedAddSize(legacyBytesRequiredForCode, subAssembliesSize);
		size_t legacyBytesRequiredIncludingData = checkedAddSize(legacyBytesRequiredIncludingSubAssemblies, m_auxiliaryData.size());
		size_t legacyProgramSizeReferenceBound = legacyBytesRequiredIncludingData;
		if (pushesProgramSize)
		{
			size_t allSubAssembliesSize = 0;
			for (size_t subIdPath = 0; subIdPath < m_subs.size(); ++subIdPath)
				allSubAssembliesSize = checkedAddSize(allSubAssembliesSize, assembleSubAssembly(subIdPath).bytecode.size());
			legacyProgramSizeReferenceBound = checkedAddSize(
				checkedAddSize(legacyBytesRequiredForCode, allSubAssembliesSize),
				m_auxiliaryData.size()
			);
		}

		unsigned minBytesPerTag = std::max<unsigned>(
			std::max<unsigned>(1, numberEncodingSize(maxSubTagPosition)),
			numberEncodingSize(legacyBytesRequiredIncludingSubAssemblies)
		);
		unsigned minBytesPerDataRef = std::max<unsigned>(1, numberEncodingSize(legacyBytesRequiredIncludingData));
		unsigned minBytesPerProgramSizeRef = std::max<unsigned>(minBytesPerDataRef, numberEncodingSize(legacyProgramSizeReferenceBound));
		unsigned bytesPerTag = minBytesPerTag;
		unsigned bytesPerDataRef = minBytesPerDataRef;
		unsigned bytesPerProgramSizeRef = minBytesPerProgramSizeRef;
		size_t bytesRequiredForCode = 0;
		size_t bytesRequiredIncludingSubAssemblies = 0;
		size_t bytesRequiredIncludingData = 0;
		while (true)
		{
			bytesRequiredForCode = codeSize(bytesPerTag, bytesPerDataRef, bytesPerProgramSizeRef, Precision::Precise);
			bytesRequiredIncludingSubAssemblies = checkedAddSize(bytesRequiredForCode, subAssembliesSize);
			bytesRequiredIncludingData = checkedAddSize(bytesRequiredIncludingSubAssemblies, m_auxiliaryData.size());

			unsigned requiredBytesPerTag = std::max<unsigned>(
				minBytesPerTag,
				numberEncodingSize(bytesRequiredIncludingSubAssemblies)
			);
			unsigned requiredBytesPerDataRef = std::max<unsigned>(minBytesPerDataRef, numberEncodingSize(bytesRequiredIncludingData));
			unsigned requiredBytesPerProgramSizeRef = std::max<unsigned>(
				minBytesPerProgramSizeRef,
				numberEncodingSize(bytesRequiredIncludingData)
			);
			assertThrow(
				requiredBytesPerTag <= 64 && requiredBytesPerDataRef <= 64 && requiredBytesPerProgramSizeRef <= 64,
				AssemblyException,
				"Assembly reference is too large."
			);
			if (
				requiredBytesPerTag == bytesPerTag &&
				requiredBytesPerDataRef == bytesPerDataRef &&
				requiredBytesPerProgramSizeRef == bytesPerProgramSizeRef
			)
				break;
			bytesPerTag = requiredBytesPerTag;
			bytesPerDataRef = requiredBytesPerDataRef;
			bytesPerProgramSizeRef = requiredBytesPerProgramSizeRef;
		}
		uint8_t tagPush = static_cast<uint8_t>(pushInstruction(bytesPerTag));
		uint8_t dataRefPush = static_cast<uint8_t>(pushInstruction(bytesPerDataRef));
		uint8_t programSizePush = static_cast<uint8_t>(pushInstruction(bytesPerProgramSizeRef));
		ret.bytecode.reserve(bytesRequiredIncludingData);
	std::map<size_t, size_t> literalJumpTargets;
	CodeCopyTaintRanges codeCopyTaintRanges;
	auto addCodeCopyTaintRange = [](ByteRanges& _ranges, size_t _offset, size_t _size)
	{
		if (_size > 0)
			_ranges.emplace_back(_offset, _size);
	};
		auto addForeignCodeCopyTaintRange = [&](size_t _offset, size_t _size)
		{
			addCodeCopyTaintRange(codeCopyTaintRanges.foreignReferences, _offset, _size);
		};
		auto addForeignTagCodeCopyTaintRange = [&](size_t _offset, size_t _size, size_t _subId, size_t _tagId)
		{
			if (_size > 0)
				codeCopyTaintRanges.foreignTags.emplace_back(_offset, _size, _subId, _tagId);
		};
		auto addLocalTagCodeCopyTaintRange = [&](size_t _offset, size_t _size, size_t _tagId)
		{
			addCodeCopyTaintRange(codeCopyTaintRanges.localTags, _offset, _size);
			if (_size > 0)
				codeCopyTaintRanges.localTagReferences.emplace_back(_offset, _size, _tagId);
		};
		auto addCurrentAddressCodeCopyTaintRange = [&](size_t _offset, size_t _size)
	{
		addCodeCopyTaintRange(codeCopyTaintRanges.currentAddresses, _offset, _size);
	};
	auto addComplementedCurrentAddressCodeCopyTaintRange = [&](size_t _offset, size_t _size)
	{
		addCodeCopyTaintRange(codeCopyTaintRanges.complementedCurrentAddresses, _offset, _size);
	};
	auto addSubCodeCopyTaintRanges = [&](Assembly const& _subAssembly, size_t _subIdPath, size_t _subAssemblyOffset)
		{
			std::vector<size_t> subAssemblyPath = decodeSubPath(_subIdPath);
			for (auto const& [offset, size]: _subAssembly.m_codeCopyTaintRangesInBytecode.foreignReferences)
				addForeignCodeCopyTaintRange(checkedAddSize(offset, _subAssemblyOffset), size);
			for (auto const& [offset, size, subId, tagId]: _subAssembly.m_codeCopyTaintRangesInBytecode.foreignTags)
			{
				std::vector<size_t> translatedPath = subAssemblyPath;
				std::vector<size_t> relativePath = _subAssembly.decodeSubPath(subId);
				translatedPath.insert(translatedPath.end(), relativePath.begin(), relativePath.end());
				if (std::optional<size_t> translatedSubId = subPathId(translatedPath))
					addForeignTagCodeCopyTaintRange(checkedAddSize(offset, _subAssemblyOffset), size, *translatedSubId, tagId);
			}
			for (auto const& [offset, size]: _subAssembly.m_codeCopyTaintRangesInBytecode.localTags)
				addCodeCopyTaintRange(codeCopyTaintRanges.localTags, checkedAddSize(offset, _subAssemblyOffset), size);
			for (auto const& [offset, size, tagId]: _subAssembly.m_codeCopyTaintRangesInBytecode.localTagReferences)
			{
				size_t const relocatedOffset = checkedAddSize(offset, _subAssemblyOffset);
				addForeignCodeCopyTaintRange(relocatedOffset, size);
				addForeignTagCodeCopyTaintRange(relocatedOffset, size, _subIdPath, tagId);
			}
			for (auto const& [offset, size]: _subAssembly.m_codeCopyTaintRangesInBytecode.currentAddresses)
				addCurrentAddressCodeCopyTaintRange(checkedAddSize(offset, _subAssemblyOffset), size);
		for (auto const& [offset, size]: _subAssembly.m_codeCopyTaintRangesInBytecode.complementedCurrentAddresses)
			addComplementedCurrentAddressCodeCopyTaintRange(checkedAddSize(offset, _subAssemblyOffset), size);
	};

	for (auto const& [itemIndex, i]: m_items | ranges::views::enumerate)
	{
		size_t const itemStart = ret.bytecode.size();

		switch (i.type())
		{
		case Operation:
			assertSerializableOperation(i);
			ret.bytecode.push_back(static_cast<uint8_t>(i.instruction()));
			break;
		case Push:
			{
				unsigned b = numberEncodingSize(i.data());
				ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(b)));
				if (b > 0)
				{
					ret.bytecode.resize(ret.bytecode.size() + b);
					bytesRef byr(&ret.bytecode.back() + 1 - b, b);
					toBigEndian(i.data(), byr);
				}
				break;
			}
			case PushTag:
			{
					ret.bytecode.push_back(tagPush);
					auto [subId, tagId] = i.splitForeignPushTag();
					tagRef[ret.bytecode.size()] = {subId, tagId};
					if (subId != std::numeric_limits<size_t>::max())
					{
						addForeignCodeCopyTaintRange(ret.bytecode.size(), bytesPerTag);
						addForeignTagCodeCopyTaintRange(ret.bytecode.size(), bytesPerTag, subId, tagId);
					}
					else
						addLocalTagCodeCopyTaintRange(ret.bytecode.size(), bytesPerTag, tagId);
					ret.bytecode.resize(ret.bytecode.size() + bytesPerTag);
					break;
			}
			case PushData:
				ret.bytecode.push_back(dataRefPush);
				dataRef.insert(std::make_pair(h256(checkedHashReference(i, "Data")), ret.bytecode.size()));
				addForeignCodeCopyTaintRange(ret.bytecode.size(), bytesPerDataRef);
				ret.bytecode.resize(ret.bytecode.size() + bytesPerDataRef);
				break;
			case PushSub:
				assertThrow(i.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "");
				ret.bytecode.push_back(dataRefPush);
				subRef.insert(std::make_pair(static_cast<size_t>(i.data()), ret.bytecode.size()));
				addForeignCodeCopyTaintRange(ret.bytecode.size(), bytesPerDataRef);
				ret.bytecode.resize(ret.bytecode.size() + bytesPerDataRef);
				break;
		case PushSubSize:
			{
				assertThrow(i.data() <= std::numeric_limits<size_t>::max(), AssemblyException, "");
				auto s = assembleSubAssembly(static_cast<size_t>(i.data())).bytecode.size();
				i.setPushedValue(u512(s));
				itemsWithPushedValues.push_back(&i);
				unsigned b = std::max<unsigned>(1, numberEncodingSize(s));
				ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(b)));
				ret.bytecode.resize(ret.bytecode.size() + b);
				bytesRef byr(&ret.bytecode.back() + 1 - b, b);
				toBigEndian(s, byr);
				break;
				}
				case PushProgramSize:
				{
					ret.bytecode.push_back(programSizePush);
					sizeRef.emplace_back(ret.bytecode.size(), &i);
					ret.bytecode.resize(ret.bytecode.size() + bytesPerProgramSizeRef);
					break;
				}
		case PushLibraryAddress:
		{
			u256 const libraryHash = checkedHashReference(i, "Library");
			ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(hyperion::AddressBytes)));
			ret.linkReferences[ret.bytecode.size()] =
				requiredIdentifier(m_libraries, h256(libraryHash), "library");
			addForeignCodeCopyTaintRange(ret.bytecode.size(), hyperion::AddressBytes);
			ret.bytecode.resize(ret.bytecode.size() + hyperion::AddressBytes);
			break;
		}
		case PushImmutable:
		{
			u256 const immutableHash = checkedHashReference(i, "Immutable");
			ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(VMWordBytes)));
			// Maps keccak back to the "identifier" std::string of that immutable.
			ret.immutableReferences[immutableHash].first =
				requiredIdentifier(m_immutables, h256(immutableHash), "immutable");
			// Record the bytecode offset of the PUSH argument.
			ret.immutableReferences[immutableHash].second.emplace_back(ret.bytecode.size());
			addForeignCodeCopyTaintRange(ret.bytecode.size(), VMWordBytes);
			addCurrentAddressCodeCopyTaintRange(ret.bytecode.size(), VMWordBytes);
			addComplementedCurrentAddressCodeCopyTaintRange(ret.bytecode.size(), VMWordBytes);
			// Advance bytecode by one VM word (default initialized).
			ret.bytecode.resize(ret.bytecode.size() + VMWordBytes);
			break;
		}
		case VerbatimBytecode:
			ret.bytecode += i.verbatimData();
			break;
		case AssignImmutable:
		{
			// Expect 2 elements on stack (source, dest_base)
			u256 const immutableHash = checkedHashReference(i, "Immutable");
			auto immutableReferences = immutableReferencesBySub.find(immutableHash);
			std::vector<size_t> const emptyOffsets;
			auto const& offsets =
				immutableReferences == immutableReferencesBySub.end() ?
				emptyOffsets :
				immutableReferences->second.second;
			for (size_t i = 0; i < offsets.size(); ++i)
			{
				if (i != offsets.size() - 1)
				{
					ret.bytecode.push_back(uint8_t(Instruction::DUP2));
					ret.bytecode.push_back(uint8_t(Instruction::DUP2));
				}
				// TODO: should we make use of the constant optimizer methods for pushing the offsets?
				bytes offsetBytes = toCompactBigEndian(u256(offsets[i]));
				ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(static_cast<unsigned>(offsetBytes.size()))));
				ret.bytecode += offsetBytes;
				ret.bytecode.push_back(uint8_t(Instruction::ADD));
				ret.bytecode.push_back(uint8_t(Instruction::MSTORE));
			}
			if (offsets.empty())
			{
				ret.bytecode.push_back(uint8_t(Instruction::POP));
				ret.bytecode.push_back(uint8_t(Instruction::POP));
			}
			if (immutableReferences != immutableReferencesBySub.end())
				immutableReferencesBySub.erase(immutableReferences);
			break;
		}
		case PushDeployTimeAddress:
			ret.bytecode.push_back(static_cast<uint8_t>(pushInstruction(hyperion::AddressBytes)));
			addCurrentAddressCodeCopyTaintRange(ret.bytecode.size(), hyperion::AddressBytes);
			ret.bytecode.resize(ret.bytecode.size() + hyperion::AddressBytes);
			break;
		case Tag:
		{
			assertThrow(i.data() != 0, AssemblyException, "Invalid tag position.");
			assertThrow(i.splitForeignPushTag().first == std::numeric_limits<size_t>::max(), AssemblyException, "Foreign tag.");
			size_t tagId = static_cast<size_t>(i.data());
			bool inserted = tagPositionsInBytecode.emplace(tagId, ret.bytecode.size()).second;
			assertThrow(inserted, AssemblyException, "Duplicate tag position.");
			ret.bytecode.push_back(static_cast<uint8_t>(Instruction::JUMPDEST));
			break;
		}
		default:
			assertThrow(false, InvalidOpcode, "Unexpected opcode while assembling.");
			}

		// Store a position for the invalid jump destination used by ErrorTag.
		// Zero-length items do not provide a bytecode location, and the next item
		// may be a valid JUMPDEST.
		if (i.type() != Tag && ret.bytecode.size() > itemStart && !tagPositionsInBytecode.count(0))
			tagPositionsInBytecode[0] = itemStart;

		if (ret.bytecode.size() > itemStart)
			literalJumpTargets.emplace(itemStart, static_cast<size_t>(itemIndex));
	}

	if (!immutableReferencesBySub.empty())
		throw
			langutil::Error(
				1284_error,
				langutil::Error::Type::CodeGenerationError,
				"Some immutables were read from but never assigned, possibly because of optimization."
			);

	if (!m_subs.empty() || !m_data.empty() || !m_auxiliaryData.empty())
		// Append an INVALID here to help tests find miscompilation.
		ret.bytecode.push_back(static_cast<uint8_t>(Instruction::INVALID));

	std::map<LinkerObject, size_t> subAssemblyOffsets;
	std::map<std::pair<size_t, std::string>, LinkerObject::FunctionDebugData> mergedSubFunctionDebugData;
	auto mergeSubFunctionDebugData = [&](LinkerObject const& _subObject, size_t _subAssemblyOffset)
	{
		for (auto const& [name, debugData]: _subObject.functionDebugData)
		{
			LinkerObject::FunctionDebugData relocatedDebugData = debugData;
			if (relocatedDebugData.bytecodeOffset)
				relocatedDebugData.bytecodeOffset = checkedAddSize(*relocatedDebugData.bytecodeOffset, _subAssemblyOffset);
			relocatedDebugData.instructionIndex = std::nullopt;

			auto [mergedIt, merged] = mergedSubFunctionDebugData.emplace(
				std::make_pair(_subAssemblyOffset, name),
				relocatedDebugData
			);
			if (!merged)
			{
				if (equalFunctionDebugData(mergedIt->second, relocatedDebugData))
					continue;
			}

			insertFunctionDebugData(ret.functionDebugData, name, relocatedDebugData);
		}
	};
	for (auto const& [subIdPath, bytecodeOffset]: subRef)
	{
		Assembly const* subAssembly = subAssemblyById(subIdPath);
		auto [
			tagsReferencedFromOutside,
			subTagsReferencedFromOutside,
			copiedTagsReferencedFromOutside,
			copiedSubTagsReferencedFromOutside
		] = referencesForSubAssembly(subIdPath);
		LinkerObject const& subObject = subAssembly->assemble(
			std::move(tagsReferencedFromOutside),
			std::move(subTagsReferencedFromOutside),
			allowsDeployTimeAddressInSubAssembly(subIdPath),
			std::move(copiedTagsReferencedFromOutside),
			std::move(copiedSubTagsReferencedFromOutside)
		);
		bytesRef r(ret.bytecode.data() + bytecodeOffset, bytesPerDataRef);

		// In order for de-duplication to kick in, not only must the bytecode be identical, but
		// link and immutables references as well.
		if (size_t* subAssemblyOffset = util::valueOrNullptr(subAssemblyOffsets, subObject))
			writeBigEndianChecked(*subAssemblyOffset, r, bytesPerDataRef, "Sub-assembly offset too large for reserved space.");
		else
		{
			writeBigEndianChecked(ret.bytecode.size(), r, bytesPerDataRef, "Sub-assembly offset too large for reserved space.");
			subAssemblyOffsets[subObject] = ret.bytecode.size();
			ret.bytecode += subObject.bytecode;
		}
		for (auto const& ref: subObject.linkReferences)
			ret.linkReferences[checkedAddSize(ref.first, subAssemblyOffsets[subObject])] = ref.second;
		addSubCodeCopyTaintRanges(*subAssembly, subIdPath, subAssemblyOffsets[subObject]);
		mergeSubFunctionDebugData(subObject, subAssemblyOffsets[subObject]);
	}
	for (auto const& i: tagRef)
	{
		size_t subId;
		size_t tagId;
		std::tie(subId, tagId) = i.second;
		Assembly const& tagAssembly =
			subId == std::numeric_limits<size_t>::max() ?
			*this :
			*subAssemblyById(subId);
		auto const& positions = subId == std::numeric_limits<size_t>::max() ?
			tagPositionsInBytecode :
			tagAssembly.m_tagPositionsInBytecode;
		auto tagPosition = positions.find(tagId);
		assertThrow(tagPosition != positions.end(), AssemblyException, "Reference to tag without position.");
		size_t pos = tagPosition->second;
		if (subId != std::numeric_limits<size_t>::max())
		{
			LinkerObject const& subObject = assembleSubAssembly(subId);
			size_t const* subAssemblyOffset = util::valueOrNullptr(subAssemblyOffsets, subObject);
			if (!subAssemblyOffset)
			{
				subAssemblyOffsets[subObject] = ret.bytecode.size();
				ret.bytecode += subObject.bytecode;
				for (auto const& ref: subObject.linkReferences)
					ret.linkReferences[checkedAddSize(ref.first, subAssemblyOffsets[subObject])] = ref.second;
				addSubCodeCopyTaintRanges(tagAssembly, subId, subAssemblyOffsets[subObject]);
				mergeSubFunctionDebugData(subObject, subAssemblyOffsets[subObject]);
				subAssemblyOffset = &subAssemblyOffsets[subObject];
			}
			pos = checkedAddSize(pos, *subAssemblyOffset);
		}
		bytesRef r(ret.bytecode.data() + i.first, bytesPerTag);
		writeBigEndianChecked(pos, r, bytesPerTag, "Tag too large for reserved space.");
	}
	for (auto const& [name, tagInfo]: m_namedTags)
	{
		auto tagPosition = tagPositionsInBytecode.find(tagInfo.id);
		size_t position = tagPosition == tagPositionsInBytecode.end() ? std::numeric_limits<size_t>::max() : tagPosition->second;
		std::optional<size_t> tagIndex;
		for (auto&& [index, item]: m_items | ranges::views::enumerate)
			if (item.type() == Tag && static_cast<size_t>(item.data()) == tagInfo.id)
			{
				tagIndex = index;
				break;
			}
		LinkerObject::FunctionDebugData debugData{
			position == std::numeric_limits<size_t>::max() ? std::nullopt : std::optional<size_t>{position},
			tagIndex,
			tagInfo.sourceID,
			tagInfo.params,
			tagInfo.returns
		};
		insertFunctionDebugData(ret.functionDebugData, name, debugData);
	}

	for (auto const& ref: dataRef)
		assertThrow(m_data.count(ref.first), AssemblyException, "Reference to non-existing data.");

	for (auto const& dataItem: m_data)
	{
		auto references = dataRef.equal_range(dataItem.first);
		if (references.first == references.second)
			continue;
		for (auto ref = references.first; ref != references.second; ++ref)
		{
			bytesRef r(ret.bytecode.data() + ref->second, bytesPerDataRef);
			writeBigEndianChecked(ret.bytecode.size(), r, bytesPerDataRef, "Data offset too large for reserved space.");
		}
		ret.bytecode += dataItem.second;
	}

	ret.bytecode += m_auxiliaryData;

		for (auto const& [pos, item]: sizeRef)
		{
			item->setPushedValue(u512(ret.bytecode.size()));
			itemsWithPushedValues.push_back(item);
			bytesRef r(ret.bytecode.data() + pos, bytesPerProgramSizeRef);
			writeBigEndianChecked(ret.bytecode.size(), r, bytesPerProgramSizeRef, "Program size too large for reserved space.");
		}

	m_assembledObject = std::move(ret);
	m_tagPositionsInBytecode = std::move(tagPositionsInBytecode);
	m_literalJumpTargetsInBytecode = std::move(literalJumpTargets);
	m_codeCopyTaintRangesInBytecode = std::move(codeCopyTaintRanges);
	m_assembled = true;
	m_assembledInitialStackHeight = _initialStackHeight;
	assemblySucceeded = true;
	return m_assembledObject;
}

bool Assembly::allowsDeployTimeAddressInSubAssembly(size_t _subIdPath) const
{
	if (!isCreation())
		return false;
	return m_deployTimeAddressSubAssemblies.count(_subIdPath) != 0;
}

void Assembly::assertValidDeployTimeAddressSubAssembly(size_t _subIdPath) const
{
	assertThrow(isCreation(), AssemblyException, "Deploy-time address sub-assemblies are only valid in creation assemblies.");
	assertThrow(
		_subIdPath == 0,
		AssemblyException,
		"Deploy-time address sub-assemblies must be the direct runtime sub-assembly."
	);
	assertThrow(
		!m_subs.empty() && m_subs.front(),
		AssemblyException,
		"Reference to non-existing subassembly."
	);
	assertThrow(
		!m_subs.front()->isCreation(),
		AssemblyException,
		"Deploy-time address sub-assemblies must be runtime sub-assemblies."
	);
}

void Assembly::assertValidImmutableValidationSubAssembly(size_t _subIdPath) const
{
	assertThrow(isCreation(), AssemblyException, "Immutable validation sub-assemblies are only valid in creation assemblies.");
	assertThrow(
		_subIdPath == 0,
		AssemblyException,
		"Immutable validation sub-assemblies must be the direct runtime sub-assembly."
	);
	assertThrow(
		!m_subs.empty() && m_subs.front(),
		AssemblyException,
		"Reference to non-existing subassembly."
	);
	assertThrow(
		!m_subs.front()->isCreation(),
		AssemblyException,
		"Immutable validation sub-assemblies must be runtime sub-assemblies."
	);
}

std::vector<size_t> Assembly::decodeSubPath(size_t _subObjectId) const
{
	if (_subObjectId < m_subs.size())
		return {_subObjectId};

	auto subIdPathIt = ranges::find_if(
		m_subPaths,
		[_subObjectId](auto const& subId) { return subId.second == _subObjectId; }
	);
	assertThrow(subIdPathIt != m_subPaths.end(), AssemblyException, "");
	return subIdPathIt->first;
}

size_t Assembly::encodeSubPath(std::vector<size_t> const& _subPath)
{
	assertMutable();
	assertThrow(!_subPath.empty(), AssemblyException, "");

	if (_subPath.size() == 1)
		return _subPath[0];

	auto [encodedPath, inserted] = m_subPaths.emplace(
		_subPath,
		std::numeric_limits<size_t>::max() - m_subPaths.size() - 1
	);
	(void)inserted;
	return encodedPath->second;
}

Assembly const* Assembly::subAssemblyById(size_t _subId) const
{
	std::vector<size_t> subIds = decodeSubPath(_subId);
	Assembly const* currentAssembly = this;
	for (size_t currentSubId: subIds)
	{
		assertThrow(currentSubId < currentAssembly->m_subs.size(), AssemblyException, "Invalid sub-assembly path.");
		currentAssembly = currentAssembly->m_subs[currentSubId].get();
		assertThrow(currentAssembly, AssemblyException, "Invalid sub-assembly path.");
	}

	assertThrow(currentAssembly != this, AssemblyException, "");
	return currentAssembly;
}

std::optional<size_t> Assembly::subPathId(std::vector<size_t> const& _subPath) const
{
	assertThrow(!_subPath.empty(), AssemblyException, "");
	if (_subPath.size() == 1)
	{
		if (_subPath.front() < m_subs.size() && m_subs[_subPath.front()])
			return _subPath.front();
		return std::nullopt;
	}
	if (size_t const* encodedPath = util::valueOrNullptr(m_subPaths, _subPath))
		return *encodedPath;

	size_t const objectId = std::numeric_limits<size_t>::max() - m_subPaths.size() - 1;
	m_subPaths[_subPath] = objectId;
	return objectId;
}

Assembly::OptimiserSettings Assembly::OptimiserSettings::translateSettings(frontend::OptimiserSettings const& _settings, langutil::QRVMVersion const& _qrvmVersion)
{
	// Constructing it this way so that we notice changes in the fields.
	qrvmasm::Assembly::OptimiserSettings asmSettings{false,  false, false, false, false, false, _qrvmVersion, 0};
	asmSettings.runInliner = _settings.runInliner;
	asmSettings.runJumpdestRemover = _settings.runJumpdestRemover;
	asmSettings.runPeephole = _settings.runPeephole;
	asmSettings.runDeduplicate = _settings.runDeduplicate;
	asmSettings.runCSE = _settings.runCSE;
	asmSettings.runConstantOptimiser = _settings.runConstantOptimiser;
	asmSettings.expectedExecutionsPerDeployment = _settings.expectedExecutionsPerDeployment;
	asmSettings.qrvmVersion= _qrvmVersion;
	return asmSettings;
}
