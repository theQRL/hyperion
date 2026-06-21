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
 * Yul interpreter module that evaluates QRVM instructions.
 */

#include <test/tools/yulInterpreter/QRVMInstructionInterpreter.h>

#include <test/tools/yulInterpreter/Interpreter.h>

#include <libyul/backends/qrvm/QRVMDialect.h>
#include <libyul/AST.h>

#include <libqrvmasm/Instruction.h>
#include <libqrvmasm/SemanticInformation.h>

#include <libhyputil/Keccak256.h>
#include <libhyputil/Numeric.h>
#include <libhyputil/VMConstants.h>

#include <limits>

using namespace std;
using namespace hyperion;
using namespace hyperion::qrvmasm;
using namespace hyperion::yul;
using namespace hyperion::yul::test;

using hyperion::util::h512;
using hyperion::util::h256;
using hyperion::util::keccak256;

namespace
{

/// Reads one VM word from @a _data at position @a _offset bytes while
/// interpreting @a _data to be padded with an infinite number of zero
/// bytes beyond its end.
u512 readZeroExtended(bytes const& _data, u512 const& _offset)
{
	if (_offset > std::numeric_limits<size_t>::max())
		return 0;
	size_t off = _offset.convert_to<size_t>();
	u512 val;
	for (size_t i = 0; i < VMWordBytes; ++i)
	{
		val <<= 8;
		if (_offset < _data.size() && off + i < _data.size())
			val += _data[off + i];
	}
	return val;
}

u512 leftAlignedHash(h256 const& _hash)
{
	return u512(h256::Arith(_hash)) << (VMWordBits - 256);
}

u512 expWord(u512 _base, u512 _exponent)
{
	u512 result = 1;
	while (_exponent != 0)
	{
		if ((_exponent & 1) != 0)
			result *= _base;
		_exponent >>= 1;
		if (_exponent != 0)
			_base *= _base;
	}
	return result;
}
}

namespace hyperion::yul::test
{
/// Copy @a _size bytes of @a _source at offset @a _sourceOffset to
/// @a _target at offset @a _targetOffset. Behaves as if @a _source would
/// continue with an infinite sequence of zero bytes beyond its end.
void copyZeroExtended(
	map<u512, uint8_t>& _target, bytes const& _source,
	size_t _targetOffset, size_t _sourceOffset, size_t _size
)
{
	for (size_t i = 0; i < _size; ++i)
		_target[_targetOffset + i] = _sourceOffset + i < _source.size() ? _source[_sourceOffset + i] : 0;
}

}

u512 QRVMInstructionInterpreter::eval(
	qrvmasm::Instruction _instruction,
	vector<u512> const& _arguments
)
{
	using namespace hyperion::qrvmasm;
	using qrvmasm::Instruction;

	auto info = instructionInfo(_instruction);
	yulAssert(static_cast<size_t>(info.args) == _arguments.size(), "");

	auto const& arg = _arguments;
	switch (_instruction)
	{
	case Instruction::STOP:
		logTrace(_instruction);
		BOOST_THROW_EXCEPTION(ExplicitlyTerminated());
	// --------------- arithmetic ---------------
	case Instruction::ADD:
		return arg[0] + arg[1];
	case Instruction::MUL:
		return arg[0] * arg[1];
	case Instruction::SUB:
		return arg[0] - arg[1];
	case Instruction::DIV:
		return arg[1] == 0 ? 0 : arg[0] / arg[1];
	case Instruction::SDIV:
		return arg[1] == 0 ? 0 : s2u(u2s(arg[0]) / u2s(arg[1]));
	case Instruction::MOD:
		return arg[1] == 0 ? 0 : arg[0] % arg[1];
	case Instruction::SMOD:
		return arg[1] == 0 ? 0 : s2u(u2s(arg[0]) % u2s(arg[1]));
	case Instruction::EXP:
		return expWord(arg[0], arg[1]);
	case Instruction::NOT:
		return ~arg[0];
	case Instruction::LT:
		return arg[0] < arg[1] ? 1 : 0;
	case Instruction::GT:
		return arg[0] > arg[1] ? 1 : 0;
	case Instruction::SLT:
		return u2s(arg[0]) < u2s(arg[1]) ? 1 : 0;
	case Instruction::SGT:
		return u2s(arg[0]) > u2s(arg[1]) ? 1 : 0;
	case Instruction::EQ:
		return arg[0] == arg[1] ? 1 : 0;
	case Instruction::ISZERO:
		return arg[0] == 0 ? 1 : 0;
	case Instruction::AND:
		return arg[0] & arg[1];
	case Instruction::OR:
		return arg[0] | arg[1];
	case Instruction::XOR:
		return arg[0] ^ arg[1];
	case Instruction::BYTE:
	{
		if (arg[0] >= VMWordBytes)
			return 0;
		unsigned byteIndex = arg[0].convert_to<unsigned>();
		return (arg[1] >> (8 * (VMWordBytes - 1 - byteIndex))) & 0xff;
	}
	case Instruction::SHL:
		return arg[0] >= VMWordBits ? 0 : (arg[1] << unsigned(arg[0]));
	case Instruction::SHR:
		return arg[0] >= VMWordBits ? 0 : (arg[1] >> unsigned(arg[0]));
	case Instruction::SAR:
	{
		static u512 const hibit = u512(1) << (VMWordBits - 1);
		if (arg[0] >= VMWordBits)
			return arg[1] & hibit ? u512(-1) : 0;
		else
		{
			unsigned amount = unsigned(arg[0]);
			u512 v = arg[1] >> amount;
			if (arg[1] & hibit)
				v |= u512(-1) << (VMWordBits - amount);
			return v;
		}
	}
	case Instruction::ADDMOD:
		return arg[2] == 0 ? 0 : u512((bigint(arg[0]) + bigint(arg[1])) % bigint(arg[2]));
	case Instruction::MULMOD:
		return arg[2] == 0 ? 0 : u512((bigint(arg[0]) * bigint(arg[1])) % bigint(arg[2]));
	case Instruction::SIGNEXTEND:
		if (arg[0] >= VMWordBytes - 1)
			return arg[1];
		else
		{
			unsigned testBit = unsigned(arg[0]) * 8 + 7;
			u512 ret = arg[1];
			u512 mask = ((u512(1) << testBit) - 1);
			if (boost::multiprecision::bit_test(ret, testBit))
				ret |= ~mask;
			else
				ret &= mask;
			return ret;
		}
	// --------------- blockchain stuff ---------------
	case Instruction::KECCAK256:
	{
		if (!accessMemory(arg[0], arg[1]))
			return u512("0x1234cafe1234cafe1234cafe") + arg[0];
		uint64_t offset = uint64_t(arg[0] & uint64_t(-1));
		uint64_t size = uint64_t(arg[1] & uint64_t(-1));
		return leftAlignedHash(keccak256(m_state.readMemory(offset, size)));
	}
	case Instruction::ADDRESS:
		return h512::Arith(m_state.address);
	case Instruction::BALANCE:
		if (arg[0] == h512::Arith(m_state.address))
			return m_state.selfbalance;
		else
			return m_state.balance;
	case Instruction::SELFBALANCE:
		return m_state.selfbalance;
	case Instruction::ORIGIN:
		return h512::Arith(m_state.origin);
	case Instruction::CALLER:
		return h512::Arith(m_state.caller);
	case Instruction::CALLVALUE:
		return m_state.callvalue;
	case Instruction::CALLDATALOAD:
		return readZeroExtended(m_state.calldata, arg[0]);
	case Instruction::CALLDATASIZE:
		return m_state.calldata.size();
	case Instruction::CALLDATACOPY:
		if (accessMemory(arg[0], arg[2]))
			copyZeroExtended(
				m_state.memory, m_state.calldata,
				size_t(arg[0]), size_t(arg[1]), size_t(arg[2])
			);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::CODESIZE:
		return m_state.code.size();
	case Instruction::CODECOPY:
		if (accessMemory(arg[0], arg[2]))
			copyZeroExtended(
				m_state.memory, m_state.code,
				size_t(arg[0]), size_t(arg[1]), size_t(arg[2])
			);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::GASPRICE:
		return m_state.gasprice;
	case Instruction::CHAINID:
		return m_state.chainid;
	case Instruction::BASEFEE:
		return m_state.basefee;
	case Instruction::EXTCODESIZE:
		return u512(h256::Arith(keccak256(h512(arg[0])))) & 0xffffff;
	case Instruction::EXTCODEHASH:
		return leftAlignedHash(keccak256(h512(arg[0] + 1)));
	case Instruction::EXTCODECOPY:
		if (accessMemory(arg[1], arg[3]))
			// TODO this way extcodecopy and codecopy do the same thing.
			copyZeroExtended(
				m_state.memory, m_state.code,
				size_t(arg[1]), size_t(arg[2]), size_t(arg[3])
			);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::RETURNDATASIZE:
		return m_state.returndata.size();
	case Instruction::RETURNDATACOPY:
		if (accessMemory(arg[0], arg[2]))
			copyZeroExtended(
				m_state.memory, m_state.returndata,
				size_t(arg[0]), size_t(arg[1]), size_t(arg[2])
			);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::BLOCKHASH:
		if (arg[0] >= m_state.blockNumber || arg[0] + 256 < m_state.blockNumber)
			return 0;
		else
			return 0xaaaaaaaa + (arg[0] - m_state.blockNumber - 256);
	case Instruction::COINBASE:
		return h512::Arith(m_state.coinbase);
	case Instruction::TIMESTAMP:
		return m_state.timestamp;
	case Instruction::NUMBER:
		return m_state.blockNumber;
	case Instruction::PREVRANDAO:
		return m_state.prevrandao;
	case Instruction::GASLIMIT:
		return m_state.gaslimit;
	// --------------- memory / storage / logs ---------------
	case Instruction::MLOAD:
		accessMemory(arg[0], VMWordBytes);
		return readMemoryWord(arg[0]);
	case Instruction::MSTORE:
		accessMemory(arg[0], VMWordBytes);
		writeMemoryWord(arg[0], arg[1]);
		return 0;
	case Instruction::MSTORE8:
		accessMemory(arg[0], 1);
		m_state.memory[arg[0]] = uint8_t(arg[1] & 0xff);
		return 0;
	case Instruction::SLOAD:
		return h512::Arith(m_state.storage[h512(arg[0])]);
	case Instruction::SSTORE:
		m_state.storage[h512(arg[0])] = h512(arg[1]);
		return 0;
	case Instruction::PC:
		return 0x77;
	case Instruction::MSIZE:
		return m_state.msize;
	case Instruction::GAS:
		return 0x99;
	case Instruction::LOG0:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::LOG1:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::LOG2:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::LOG3:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		return 0;
	case Instruction::LOG4:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		return 0;
	// --------------- calls ---------------
	case Instruction::CREATE:
		accessMemory(arg[1], arg[2]);
		logTrace(_instruction, arg);
		if (arg[2] != 0)
			return 0xcccccc + arg[1];
		else
			return 0xcccccc;
	case Instruction::CREATE2:
		accessMemory(arg[1], arg[2]);
		logTrace(_instruction, arg);
		if (arg[2] != 0)
			return 0xdddddd + arg[1];
		else
			return 0xdddddd;
	case Instruction::CALL:
		accessMemory(arg[3], arg[4]);
		accessMemory(arg[5], arg[6]);
		logTrace(_instruction, arg);
		// Randomly fail based on the called address if it isn't a call to self.
		// Used for fuzzing.
		return (
			(arg[0] > 0) &&
			(arg[1] == util::h512::Arith(m_state.address) || (arg[1] & 1))
		) ? 1 : 0;
	case Instruction::DELEGATECALL:
	case Instruction::STATICCALL:
		accessMemory(arg[2], arg[3]);
		accessMemory(arg[4], arg[5]);
		logTrace(_instruction, arg);
		// Randomly fail based on the called address if it isn't a call to self.
		// Used for fuzzing.
		return (
			(arg[0] > 0) &&
			(arg[1] == util::h512::Arith(m_state.address) || (arg[1] & 1))
		) ? 1 : 0;
	case Instruction::RETURN:
	{
		m_state.returndata = {};
		if (accessMemory(arg[0], arg[1]))
			m_state.returndata = m_state.readMemory(arg[0], arg[1]);
		logTrace(_instruction, arg, m_state.returndata);
		BOOST_THROW_EXCEPTION(ExplicitlyTerminatedWithReturn());
	}
	case Instruction::REVERT:
		accessMemory(arg[0], arg[1]);
		logTrace(_instruction, arg);
		m_state.storage.clear();
		BOOST_THROW_EXCEPTION(ExplicitlyTerminated());
	case Instruction::INVALID:
		logTrace(_instruction);
		m_state.storage.clear();
		m_state.trace.clear();
		BOOST_THROW_EXCEPTION(ExplicitlyTerminated());
	case Instruction::POP:
		break;
	// --------------- invalid in strict assembly ---------------
	case Instruction::JUMP:
	case Instruction::JUMPI:
	case Instruction::JUMPDEST:
	case Instruction::PUSH0:
	case Instruction::PUSH1:
	case Instruction::PUSH2:
	case Instruction::PUSH3:
	case Instruction::PUSH4:
	case Instruction::PUSH5:
	case Instruction::PUSH6:
	case Instruction::PUSH7:
	case Instruction::PUSH8:
	case Instruction::PUSH9:
	case Instruction::PUSH10:
	case Instruction::PUSH11:
	case Instruction::PUSH12:
	case Instruction::PUSH13:
	case Instruction::PUSH14:
	case Instruction::PUSH15:
	case Instruction::PUSH16:
	case Instruction::PUSH17:
	case Instruction::PUSH18:
	case Instruction::PUSH19:
	case Instruction::PUSH20:
	case Instruction::PUSH21:
	case Instruction::PUSH22:
	case Instruction::PUSH23:
	case Instruction::PUSH24:
	case Instruction::PUSH25:
	case Instruction::PUSH26:
	case Instruction::PUSH27:
	case Instruction::PUSH28:
	case Instruction::PUSH29:
	case Instruction::PUSH30:
	case Instruction::PUSH31:
	case Instruction::PUSH32:
	case Instruction::PUSH33:
	case Instruction::PUSH34:
	case Instruction::PUSH35:
	case Instruction::PUSH36:
	case Instruction::PUSH37:
	case Instruction::PUSH38:
	case Instruction::PUSH39:
	case Instruction::PUSH40:
	case Instruction::PUSH41:
	case Instruction::PUSH42:
	case Instruction::PUSH43:
	case Instruction::PUSH44:
	case Instruction::PUSH45:
	case Instruction::PUSH46:
	case Instruction::PUSH47:
	case Instruction::PUSH48:
	case Instruction::PUSH49:
	case Instruction::PUSH50:
	case Instruction::PUSH51:
	case Instruction::PUSH52:
	case Instruction::PUSH53:
	case Instruction::PUSH54:
	case Instruction::PUSH55:
	case Instruction::PUSH56:
	case Instruction::PUSH57:
	case Instruction::PUSH58:
	case Instruction::PUSH59:
	case Instruction::PUSH60:
	case Instruction::PUSH61:
	case Instruction::PUSH62:
	case Instruction::PUSH63:
	case Instruction::PUSH64:
	case Instruction::DUP1:
	case Instruction::DUP2:
	case Instruction::DUP3:
	case Instruction::DUP4:
	case Instruction::DUP5:
	case Instruction::DUP6:
	case Instruction::DUP7:
	case Instruction::DUP8:
	case Instruction::DUP9:
	case Instruction::DUP10:
	case Instruction::DUP11:
	case Instruction::DUP12:
	case Instruction::DUP13:
	case Instruction::DUP14:
	case Instruction::DUP15:
	case Instruction::DUP16:
	case Instruction::SWAP1:
	case Instruction::SWAP2:
	case Instruction::SWAP3:
	case Instruction::SWAP4:
	case Instruction::SWAP5:
	case Instruction::SWAP6:
	case Instruction::SWAP7:
	case Instruction::SWAP8:
	case Instruction::SWAP9:
	case Instruction::SWAP10:
	case Instruction::SWAP11:
	case Instruction::SWAP12:
	case Instruction::SWAP13:
	case Instruction::SWAP14:
	case Instruction::SWAP15:
	case Instruction::SWAP16:
	{
		yulAssert(false, "");
		return 0;
	}
	}

	return 0;
}

u512 QRVMInstructionInterpreter::evalBuiltin(
	BuiltinFunctionForQRVM const& _fun,
	vector<Expression> const& _arguments,
	vector<u512> const& _evaluatedArguments
)
{
	if (_fun.instruction)
		return eval(*_fun.instruction, _evaluatedArguments);

	string fun = _fun.name.str();
		// Evaluate datasize/offset/copy instructions
		if (fun == "datasize" || fun == "dataoffset")
		{
			string arg = std::get<Literal>(_arguments.at(0)).value.str();
			if (arg.length() < VMWordBytes)
				arg.resize(VMWordBytes, 0);
		if (fun == "datasize")
			return u512(h256::Arith(keccak256(arg))) & 0xfff;
		else
		{
			// Force different value than for datasize
			arg[VMWordBytes - 1]++;
			arg[VMWordBytes - 1]++;
			return u512(h256::Arith(keccak256(arg))) & 0xfff;
		}
	}
	else if (fun == "datacopy")
	{
		// This is identical to codecopy.
		if (
				_evaluatedArguments.at(2) != 0 &&
				accessMemory(_evaluatedArguments.at(0), _evaluatedArguments.at(2))
		)
			copyZeroExtended(
				m_state.memory,
				m_state.code,
				size_t(_evaluatedArguments.at(0)),
				size_t(_evaluatedArguments.at(1) & numeric_limits<size_t>::max()),
				size_t(_evaluatedArguments.at(2))
			);
		return 0;
	}
	else if (fun == "memoryguard")
		return _evaluatedArguments.at(0);
	else
		yulAssert(false, "Unknown builtin: " + fun);
	return 0;
}


bool QRVMInstructionInterpreter::accessMemory(u512 const& _offset, u512 const& _size)
{
	if (_size == 0)
		return true;
	else if (((_offset + _size) >= _offset) && ((_offset + _size + VMWordAlignmentMask) >= (_offset + _size)))
	{
		u512 newSize = (_offset + _size + VMWordAlignmentMask) & ~u512(VMWordAlignmentMask);
		m_state.msize = max(m_state.msize, newSize);
		// We only record accesses to contiguous memory chunks that are at most s_maxRangeSize bytes
		// in size and at an offset of at most numeric_limits<size_t>::max() - s_maxRangeSize
		return _size <= s_maxRangeSize && _offset <= u512(numeric_limits<size_t>::max() - s_maxRangeSize);
	}
	else
		m_state.msize = u512(-1);

	return false;
}

bytes QRVMInstructionInterpreter::readMemory(u512 const& _offset, u512 const& _size)
{
	yulAssert(_size <= s_maxRangeSize, "Too large read.");
	bytes data(size_t(_size), uint8_t(0));
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = m_state.memory[_offset + i];
	return data;
}

u512 QRVMInstructionInterpreter::readMemoryWord(u512 const& _offset)
{
	return h512::Arith(h512(m_state.readMemory(_offset, VMWordBytes)));
}

void QRVMInstructionInterpreter::writeMemoryWord(u512 const& _offset, u512 const& _value)
{
	for (size_t i = 0; i < VMWordBytes; i++)
		m_state.memory[_offset + i] = uint8_t((_value >> (8 * (VMWordBytes - 1 - i))) & 0xff);
}


void QRVMInstructionInterpreter::logTrace(
	qrvmasm::Instruction _instruction,
	std::vector<u512> const& _arguments,
	bytes const& _data
)
{
	logTrace(
		qrvmasm::instructionInfo(_instruction).name,
		SemanticInformation::memory(_instruction) == SemanticInformation::Effect::Write,
		_arguments,
		_data
	);
}

void QRVMInstructionInterpreter::logTrace(
	std::string const& _pseudoInstruction,
	bool _writesToMemory,
	std::vector<u512> const& _arguments,
	bytes const& _data
)
{
	if (!(_writesToMemory && memWriteTracingDisabled()))
	{
		string message = _pseudoInstruction + "(";
		std::pair<bool, size_t> inputMemoryPtrModified = isInputMemoryPtrModified(_pseudoInstruction, _arguments);
		for (size_t i = 0; i < _arguments.size(); ++i)
		{
			bool printZero = inputMemoryPtrModified.first && inputMemoryPtrModified.second == i;
			u512 arg = printZero ? 0 : _arguments[i];
			message += (i > 0 ? ", " : "") + formatNumber(arg);
		}
		message += ")";
		if (!_data.empty())
			message += " [" + util::toHex(_data) + "]";
		m_state.trace.emplace_back(std::move(message));
		if (m_state.maxTraceSize > 0 && m_state.trace.size() >= m_state.maxTraceSize)
		{
			m_state.trace.emplace_back("Trace size limit reached.");
			BOOST_THROW_EXCEPTION(TraceLimitReached());
		}
	}
}

std::pair<bool, size_t> QRVMInstructionInterpreter::isInputMemoryPtrModified(
	std::string const& _pseudoInstruction,
	std::vector<u512> const& _arguments
)
{
	if (_pseudoInstruction == "RETURN" || _pseudoInstruction == "REVERT")
	{
		if (_arguments[1] == 0)
			return {true, 0};
		else
			return {false, 0};
	}
	else if (
		_pseudoInstruction == "RETURNDATACOPY" || _pseudoInstruction == "CALLDATACOPY"
		|| _pseudoInstruction == "CODECOPY")
	{
		if (_arguments[2] == 0)
			return {true, 0};
		else
			return {false, 0};
	}
	else if (_pseudoInstruction == "EXTCODECOPY")
	{
		if (_arguments[3] == 0)
			return {true, 1};
		else
			return {false, 0};
	}
	else if (
		_pseudoInstruction == "LOG0" || _pseudoInstruction == "LOG1" || _pseudoInstruction == "LOG2"
		|| _pseudoInstruction == "LOG3" || _pseudoInstruction == "LOG4")
	{
		if (_arguments[1] == 0)
			return {true, 0};
		else
			return {false, 0};
	}
	if (_pseudoInstruction == "CREATE" || _pseudoInstruction == "CREATE2")
	{
		if (_arguments[2] == 0)
			return {true, 1};
		else
			return {false, 0};
	}
	if (_pseudoInstruction == "CALL")
	{
		if (_arguments[4] == 0)
			return {true, 3};
		else
			return {false, 0};
	}
	else if (_pseudoInstruction == "DELEGATECALL" || _pseudoInstruction == "STATICCALL")
	{
		if (_arguments[3] == 0)
			return {true, 2};
		else
			return {false, 0};
	}
	else
		return {false, 0};
}
