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
 * @author Christian <c@ethdev.com>
 * @date 2015
 * Code generation utils that handle arrays.
 */

#include <libhyperion/codegen/ArrayUtils.h>

#include <libhyperion/ast/Types.h>
#include <libhyperion/ast/TypeProvider.h>
#include <libhyperion/codegen/CompilerContext.h>
#include <libhyperion/codegen/CompilerUtils.h>
#include <libhyperion/codegen/LValue.h>

#include <libhyputil/FunctionSelector.h>
#include <libhyputil/Whiskers.h>
#include <libhyputil/StackTooDeepString.h>
#include <libhyputil/VMConstants.h>

#include <libqrvmasm/Instruction.h>
#include <liblangutil/Exceptions.h>

using namespace hyperion;
using namespace hyperion::qrvmasm;
using namespace hyperion::frontend;
using namespace hyperion::langutil;

void ArrayUtils::copyArrayToStorage(ArrayType const& _targetType, ArrayType const& _sourceType) const
{
	// this copies source to target and also clears target if it was larger
	// need to leave "target_ref target_byte_off" on the stack at the end

	// stack layout: source_ref [source length] target_ref (top)
	hypAssert(_targetType.location() == DataLocation::Storage, "");

	// TODO unroll loop for small sizes

	bool fromCalldata = _sourceType.location() == DataLocation::CallData;
	bool haveSourceLengthOnStack = fromCalldata && _sourceType.isDynamicallySized();

	for (unsigned i = _sourceType.sizeOnStack(); i > 0; --i)
		m_context << swapInstruction(i);
	// stack: target_ref source_ref [source_length]

	if (_sourceType.baseType()->category() == Type::Category::Array)
	{
		// TODO: This limitation can now be removed since we use Yul utility functions that handle nested arrays correctly.
		// The old inline assembly implementation couldn't handle nested calldata dynamic arrays, but the Yul functions
		// support them through recursive calls. We keep this check temporarily for backward compatibility.
		auto const& sourceBaseArrayType = dynamic_cast<ArrayType const&>(*_sourceType.baseType());
		hypUnimplementedAssert(
			!fromCalldata ||
			!_sourceType.isDynamicallyEncoded() ||
			!sourceBaseArrayType.isDynamicallySized(),
			"Copying nested calldata dynamic arrays to storage is not implemented in the old code generator."
		);
	}
	else
	{
		// TODO: This limitation can now be removed since we use Yul utility functions that handle non-value types correctly.
		// The old inline assembly implementation couldn't handle copying arrays of non-value types from memory or calldata to storage,
		// but the Yul functions support them. We keep this check temporarily for backward compatibility.
		bool fromMemoryOrCalldata = _sourceType.location() == DataLocation::Memory || _sourceType.location() == DataLocation::CallData;
		hypUnimplementedAssert(
			_sourceType.baseType()->isValueType() || !fromMemoryOrCalldata,
			"Copying of type " + _sourceType.toString(false) + " to storage not yet supported."
		);
	}

	// The copy (and the clearing of any leftover target elements) is delegated to the same Yul
	// utility functions the IR pipeline uses. They iterate over element and slot counts rather
	// than comparing absolute slot numbers, so arrays whose slots wrap past the last storage slot
	// (2**512 - 1) are copied and cleaned up correctly.
	if (haveSourceLengthOnStack)
	{
		// stack: target_ref source_ref source_length
		m_context << Instruction::SWAP1;
		// stack: target_ref source_length source_ref
		m_context << Instruction::DUP3;
		// stack: target_ref source_length source_ref target_ref
	}
	else
	{
		// stack: target_ref source_ref
		m_context << Instruction::DUP2;
		// stack: target_ref source_ref target_ref
	}

	m_context.callYulFunction(
		m_context.utilFunctions().copyArrayToStorageFunction(_sourceType, _targetType),
		haveSourceLengthOnStack ? 3 : 2,
		0
	);
	// stack: target_ref
}

void ArrayUtils::copyArrayToMemory(ArrayType const& _sourceType, bool _padToWordBoundaries) const
{
	hypUnimplementedAssert(
		!_sourceType.baseType()->isDynamicallySized(),
		"Nested dynamic arrays not implemented here."
	);
	CompilerUtils utils(m_context);

	if (_sourceType.location() == DataLocation::CallData)
	{
		if (!_sourceType.isDynamicallySized())
			m_context << _sourceType.length();
		if (!_sourceType.isByteArrayOrString() && _sourceType.baseType()->isValueType())
		{
			m_context << u256(0) << Instruction::SWAP3;
			// stack: counter source length target
			qrvmasm::AssemblyItem repeat = m_context.newTag();
			m_context << repeat;
			m_context << Instruction::DUP2 << Instruction::DUP5;
			m_context << Instruction::LT << Instruction::ISZERO;
			qrvmasm::AssemblyItem loopEnd = m_context.appendConditionalJump();

			m_context << Instruction::DUP3;
			utils.loadFromMemoryDynamic(*_sourceType.baseType(), true, true, false);
			utils.storeInMemoryDynamic(*_sourceType.baseType(), _padToWordBoundaries, false);

			m_context << Instruction::SWAP2 << u256(_sourceType.calldataStride()) << Instruction::ADD << Instruction::SWAP2;
			m_context << Instruction::SWAP3 << u256(1) << Instruction::ADD << Instruction::SWAP3;
			m_context.appendJumpTo(repeat);
			m_context << loopEnd;
			m_context << Instruction::SWAP3;
			utils.popStackSlots(3);
			// stack: updated_target_pos
			return;
		}
		if (!_sourceType.isByteArrayOrString())
			convertLengthToSize(_sourceType);

		std::string routine = "calldatacopy(target, source, len)\n";
		if (_padToWordBoundaries)
			routine += util::Whiskers(R"(
				// Set padding suffix to zero
				mstore(add(target, len), 0)
				len := and(add(len, <alignmentMask>), not(<alignmentMask>))
			)")
			("alignmentMask", std::to_string(VMWordAlignmentMask))
			.render();
		routine += "target := add(target, len)\n";
		m_context.appendInlineAssembly("{" + routine + "}", {"target", "source", "len"});
		m_context << Instruction::POP << Instruction::POP;
	}
	else if (_sourceType.location() == DataLocation::Memory)
	{
		retrieveLength(_sourceType);
		// stack: target source length
		if (!_sourceType.baseType()->isValueType())
		{
			// copy using a loop
			m_context << u256(0) << Instruction::SWAP3;
			// stack: counter source length target
			auto repeat = m_context.newTag();
			m_context << repeat;
			m_context << Instruction::DUP2 << Instruction::DUP5;
			m_context << Instruction::LT << Instruction::ISZERO;
			auto loopEnd = m_context.appendConditionalJump();
			m_context << Instruction::DUP3 << Instruction::DUP5;
			accessIndex(_sourceType, false);
			MemoryItem(m_context, *_sourceType.baseType(), true).retrieveValue(SourceLocation(), true);
			if (auto baseArray = dynamic_cast<ArrayType const*>(_sourceType.baseType()))
				copyArrayToMemory(*baseArray, _padToWordBoundaries);
			else
				utils.storeInMemoryDynamic(*_sourceType.baseType());
			m_context << Instruction::SWAP3 << u256(1) << Instruction::ADD;
			m_context << Instruction::SWAP3;
			m_context.appendJumpTo(repeat);
			m_context << loopEnd;
			m_context << Instruction::SWAP3;
			utils.popStackSlots(3);
			// stack: updated_target_pos
			return;
		}

		// memcpy using the built-in contract
		if (_sourceType.isDynamicallySized())
		{
			// change pointer to data part
			m_context << Instruction::SWAP1 << u256(VMWordBytes) << Instruction::ADD;
			m_context << Instruction::SWAP1;
		}
		if (!_sourceType.isByteArrayOrString())
			convertLengthToSize(_sourceType);
		// stack: <target> <source> <size>
		m_context << Instruction::DUP1 << Instruction::DUP4 << Instruction::DUP4;
		// We can resort to copying full VM words only if
		// - the length is known to be a multiple of the VM word size or
		// - we will pad to full VM words later anyway.
		if (!_sourceType.isByteArrayOrString() || _padToWordBoundaries)
			utils.memoryCopyWord();
		else
			utils.memoryCopy();

		m_context << Instruction::SWAP1 << Instruction::POP;
		// stack: <target> <size>

		bool paddingNeeded = _padToWordBoundaries && _sourceType.isByteArrayOrString();

		if (paddingNeeded)
		{
			// stack: <target> <size>
			m_context << Instruction::SWAP1 << Instruction::DUP2 << Instruction::ADD;
			// stack: <length> <target + size>
			m_context << Instruction::SWAP1 << u256(VMWordAlignmentMask) << Instruction::AND;
			// stack: <target + size> <remainder = size % VMWordBytes>
			qrvmasm::AssemblyItem skip = m_context.newTag();
			if (_sourceType.isDynamicallySized())
			{
				m_context << Instruction::DUP1 << Instruction::ISZERO;
				m_context.appendConditionalJumpTo(skip);
			}
			// round off, load from there.
			// stack <target + size> <remainder = size % VMWordBytes>
			m_context << Instruction::DUP1 << Instruction::DUP3;
			m_context << Instruction::SUB;
			// stack: target+size remainder <target + size - remainder>
			m_context << Instruction::DUP1 << Instruction::MLOAD;
			// Now we AND it with ~(2**(8 * (VMWordBytes - remainder)) - 1)
			m_context << u256(1);
			m_context << Instruction::DUP4 << u256(VMWordBytes) << Instruction::SUB;
			// stack: ...<v> 1 <VMWordBytes - remainder>
			m_context << u256(0x100) << Instruction::EXP << Instruction::SUB;
			m_context << Instruction::NOT << Instruction::AND;
			// stack: target+size remainder target+size-remainder <v & ...>
			m_context << Instruction::DUP2 << Instruction::MSTORE;
			// stack: target+size remainder target+size-remainder
			m_context << u256(VMWordBytes) << Instruction::ADD;
			// stack: target+size remainder <new_padded_end>
			m_context << Instruction::SWAP2 << Instruction::POP;

			if (_sourceType.isDynamicallySized())
				m_context << skip.tag();
			// stack <target + "size"> <remainder = size % VMWordBytes>
			m_context << Instruction::POP;
		}
		else
			// stack: <target> <size>
			m_context << Instruction::ADD;
	}
	else
	{
		hypAssert(_sourceType.location() == DataLocation::Storage, "");
		unsigned storageBytes = _sourceType.baseType()->storageBytes();
		u256 storageSize = _sourceType.baseType()->storageSize();
		hypAssert(storageSize > 1 || (storageSize == 1 && storageBytes > 0), "");

		retrieveLength(_sourceType);
		// stack here: memory_offset storage_offset length
		// jump to end if length is zero
		m_context << Instruction::DUP1 << Instruction::ISZERO;
		qrvmasm::AssemblyItem loopEnd = m_context.appendConditionalJump();
		// Special case for tightly-stored byte arrays
		if (_sourceType.isByteArrayOrString())
		{
			// stack here: memory_offset storage_offset length
			m_context << Instruction::DUP1 << u256(VMWordAlignmentMask) << Instruction::LT;
			qrvmasm::AssemblyItem longByteArray = m_context.appendConditionalJump();
			// store the short byte array (discard lower-order byte)
			m_context << u256(0x100) << Instruction::DUP1;
			m_context << Instruction::DUP4 << Instruction::SLOAD;
			m_context << Instruction::DIV << Instruction::MUL;
			m_context << Instruction::DUP4 << Instruction::MSTORE;
			// stack here: memory_offset storage_offset length
			// add VMWordBytes or length to memory offset
			m_context << Instruction::SWAP2;
			if (_padToWordBoundaries)
				m_context << u256(VMWordBytes);
			else
				m_context << Instruction::DUP3;
			m_context << Instruction::ADD;
			m_context << Instruction::SWAP2;
			m_context.appendJumpTo(loopEnd);
			m_context << longByteArray;
		}
		else
			// convert length to memory size
			m_context << _sourceType.baseType()->memoryHeadSize() << Instruction::MUL;

		m_context << Instruction::DUP3 << Instruction::ADD << Instruction::SWAP2;
		if (_sourceType.isDynamicallySized())
		{
			// actual array data is stored at KECCAK256(storage_offset)
			m_context << Instruction::SWAP1;
			utils.computeHashStatic();
			m_context << Instruction::SWAP1;
		}

		// stack here: memory_end_offset storage_data_offset memory_offset
		bool haveByteOffset = !_sourceType.isByteArrayOrString() && storageBytes <= (VMWordBytes / 2);
		if (haveByteOffset)
			m_context << u256(0) << Instruction::SWAP1;
		// stack here: memory_end_offset storage_data_offset [storage_byte_offset] memory_offset
		qrvmasm::AssemblyItem loopStart = m_context.newTag();
		m_context << loopStart;
		// load and store
		if (_sourceType.isByteArrayOrString())
		{
			// Packed both in storage and memory.
			m_context << Instruction::DUP2 << Instruction::SLOAD;
			m_context << Instruction::DUP2 << Instruction::MSTORE;
			// increment storage_data_offset by 1
			m_context << Instruction::SWAP1 << u256(1) << Instruction::ADD;
			// increment memory offset by VMWordBytes
			m_context << Instruction::SWAP1 << u256(VMWordBytes) << Instruction::ADD;
		}
		else
		{
			// stack here: memory_end_offset storage_data_offset [storage_byte_offset] memory_offset
			if (haveByteOffset)
				m_context << Instruction::DUP3 << Instruction::DUP3;
			else
				m_context << Instruction::DUP2 << u256(0);
			StorageItem(m_context, *_sourceType.baseType()).retrieveValue(SourceLocation(), true);
			if (auto baseArray = dynamic_cast<ArrayType const*>(_sourceType.baseType()))
				copyArrayToMemory(*baseArray, _padToWordBoundaries);
			else
				utils.storeInMemoryDynamic(*_sourceType.baseType());
			// increment storage_data_offset and byte offset
			if (haveByteOffset)
				incrementByteOffset(storageBytes, 2, 3);
			else
			{
				m_context << Instruction::SWAP1;
				m_context << storageSize << Instruction::ADD;
				m_context << Instruction::SWAP1;
			}
		}
		// check for loop condition
		m_context << Instruction::DUP1 << dupInstruction(haveByteOffset ? 5 : 4);
		m_context << Instruction::GT;
		m_context.appendConditionalJumpTo(loopStart);
		// stack here: memory_end_offset storage_data_offset [storage_byte_offset] memory_offset
		if (haveByteOffset)
			m_context << Instruction::SWAP1 << Instruction::POP;
		if (!_sourceType.isByteArrayOrString())
		{
			hypAssert(_sourceType.calldataStride() % VMWordBytes == 0, "");
			hypAssert(_sourceType.memoryStride() % VMWordBytes == 0, "");
		}
		if (_padToWordBoundaries && _sourceType.isByteArrayOrString())
		{
			// memory_end_offset - start is the actual length (we want to compute the ceil of).
			// memory_offset - start is its next multiple of VMWordBytes, but it might be off by VMWordBytes.
			// so we compute: memory_end_offset += (memory_offset - memory_end_offest) & VMWordAlignmentMask
			m_context << Instruction::DUP3 << Instruction::SWAP1 << Instruction::SUB;
			m_context << u256(VMWordAlignmentMask) << Instruction::AND;
			m_context << Instruction::DUP3 << Instruction::ADD;
			m_context << Instruction::SWAP2;
		}
		m_context << loopEnd << Instruction::POP << Instruction::POP;
	}
}

void ArrayUtils::clearArray(ArrayType const& _typeIn) const
{
	Type const* type = &_typeIn;
	m_context.callLowLevelFunction(
		"$clearArray_" + _typeIn.identifier(),
		2,
		0,
		[type](CompilerContext& _context)
		{
			ArrayType const& _type = dynamic_cast<ArrayType const&>(*type);
			unsigned stackHeightStart = _context.stackHeight();
			hypAssert(_type.location() == DataLocation::Storage, "");
			if (_type.baseType()->storageBytes() < VMWordBytes)
			{
				hypAssert(_type.baseType()->isValueType(), "Invalid storage size for non-value type.");
				hypAssert(_type.baseType()->storageSize() <= 1, "Invalid storage size for type.");
			}
			_context << Instruction::POP; // remove byte offset
			if (_type.isDynamicallySized())
				ArrayUtils(_context).clearDynamicArray(_type);
			else if (_type.length() == 0 || _type.baseType()->category() == Type::Category::Mapping)
				_context << Instruction::POP;
			else if (_type.baseType()->isValueType() && _type.storageSize() <= 5)
			{
				// unroll loop for small arrays @todo choose a good value
				// Note that we loop over storage slots here, not elements.
				for (unsigned i = 1; i < _type.storageSize(); ++i)
					_context
						<< u256(0) << Instruction::DUP2 << Instruction::SSTORE
						<< u256(1) << Instruction::ADD;
				_context << u256(0) << Instruction::SWAP1 << Instruction::SSTORE;
			}
			else if (!_type.baseType()->isValueType() && _type.length() <= 4)
			{
				// unroll loop for small arrays @todo choose a good value
				hypAssert(_type.baseType()->storageBytes() >= VMWordBytes, "Invalid storage size.");
				for (unsigned i = 1; i < _type.length(); ++i)
				{
					_context << u256(0);
					StorageItem(_context, *_type.baseType()).setToZero(SourceLocation(), false);
					_context
						<< Instruction::POP
						<< u256(_type.baseType()->storageSize()) << Instruction::ADD;
				}
				_context << u256(0);
				StorageItem(_context, *_type.baseType()).setToZero(SourceLocation(), true);
			}
			else
			{
				_context << _type.length();
				ArrayUtils(_context).convertLengthToSize(_type);
				// stack: storage_ref slot_count
				if (_type.baseType()->storageBytes() < VMWordBytes)
					ArrayUtils(_context).clearStorageLoop(TypeProvider::uint256());
				else
					ArrayUtils(_context).clearStorageLoop(_type.baseType());
				_context << Instruction::POP;
			}
			hypAssert(_context.stackHeight() == stackHeightStart - 2, "");
		}
	);
}

void ArrayUtils::clearDynamicArray(ArrayType const& _type) const
{
	hypAssert(_type.location() == DataLocation::Storage, "");
	hypAssert(_type.isDynamicallySized(), "");

	// fetch length
	retrieveLength(_type);
	// set length to zero
	m_context << u256(0) << Instruction::DUP3 << Instruction::SSTORE;
	// Special case: short byte arrays are stored togeher with their length
	qrvmasm::AssemblyItem endTag = m_context.newTag();
	if (_type.isByteArrayOrString())
	{
		// stack: ref old_length
		m_context << Instruction::DUP1 << u256(VMWordAlignmentMask) << Instruction::LT;
		qrvmasm::AssemblyItem longByteArray = m_context.appendConditionalJump();
		m_context << Instruction::POP;
		m_context.appendJumpTo(endTag);
		m_context.adjustStackOffset(1); // needed because of jump
		m_context << longByteArray;
	}
	// stack: ref old_length
	convertLengthToSize(_type);
	// stack: ref slot_count
	m_context << Instruction::SWAP1;
	CompilerUtils(m_context).computeHashStatic();
	// stack: slot_count data_pos
	m_context << Instruction::SWAP1;
	// stack: data_pos slot_count
	if (_type.storageStride() < VMWordBytes)
		clearStorageLoop(TypeProvider::uint256());
	else
		clearStorageLoop(_type.baseType());
	// cleanup
	m_context << endTag;
	m_context << Instruction::POP;
}

void ArrayUtils::incrementDynamicArraySize(ArrayType const& _type) const
{
	hypAssert(_type.location() == DataLocation::Storage, "");
	hypAssert(_type.isDynamicallySized(), "");
	if (!_type.isByteArrayOrString() && _type.baseType()->storageBytes() < VMWordBytes)
		hypAssert(_type.baseType()->isValueType(), "Invalid storage size for non-value type.");

	if (_type.isByteArrayOrString())
	{
		// We almost always just add 2 (length of byte arrays is shifted left by one)
		// except for the case where we transition from a short byte array
		// to a long byte array, there we have to copy.
		// This happens if the length is exactly VMWordBytes - 1, which means that the
		// lowest-order byte (we actually use a mask with fewer bits) must
		// be (VMWordBytes - 1) * 2 + 1

		m_context << Instruction::DUP1 << Instruction::SLOAD << Instruction::DUP1;
		m_context.callYulFunction(m_context.utilFunctions().extractByteArrayLengthFunction(), 1, 1);
		m_context.appendInlineAssembly(util::Whiskers(R"({
			// We have to copy if length is exactly VMWordBytes - 1, because that marks
			// the transition between in-place and out-of-place storage.
			switch length
			case <maxShortLength>
			{
				mstore(0, ref)
				let data_area := keccak256(0, <wordSize>)
				sstore(data_area, and(data, not(0xff)))
				// Set old length in new format ((VMWordBytes - 1) * 2 + 1)
				data := <maxShortLengthLongEncoding>
			}
			sstore(ref, add(data, 2))
			// return new length in ref
			ref := add(length, 1)
		})")
		("maxShortLength", std::to_string(VMWordAlignmentMask))
		("wordSize", std::to_string(VMWordBytes))
		("maxShortLengthLongEncoding", std::to_string(VMWordAlignmentMask * 2 + 1))
		.render(), {"ref", "data", "length"});
		m_context << Instruction::POP << Instruction::POP;
	}
	else
		m_context.appendInlineAssembly(R"({
			let new_length := add(sload(ref), 1)
			sstore(ref, new_length)
			ref := new_length
		})", {"ref"});
}

void ArrayUtils::popStorageArrayElement(ArrayType const& _type) const
{
	hypAssert(_type.location() == DataLocation::Storage, "");
	hypAssert(_type.isDynamicallySized(), "");
	if (!_type.isByteArrayOrString() && _type.baseType()->storageBytes() < VMWordBytes)
		hypAssert(_type.baseType()->isValueType(), "Invalid storage size for non-value type.");

	if (_type.isByteArrayOrString())
	{
		m_context << Instruction::DUP1 << Instruction::SLOAD << Instruction::DUP1;
		m_context.callYulFunction(m_context.utilFunctions().extractByteArrayLengthFunction(), 1, 1);
		util::Whiskers code(R"({
			if iszero(length) {
				mstore(0, <panicSelector>)
				mstore(4, <emptyArrayPop>)
				revert(0, <panicReturndataSize>)
			}
			switch gt(length, <maxShortLength>)
			case 0 {
				// short byte array
				// Zero-out the suffix including the least significant byte.
				let mask := sub(exp(0x100, sub(<shortMaskLength>, length)), 1)
				length := sub(length, 1)
				slot_value := or(and(not(mask), slot_value), mul(length, 2))
			}
			case 1 {
				// long byte array
				mstore(0, ref)
				let slot := keccak256(0, <wordSize>)
				switch length
				case <wordSize>
				{
					let data := sload(slot)
					sstore(slot, 0)
					data := and(data, not(0xff))
					slot_value := or(data, <maxShortLengthEncoded>)
				}
				default
				{
					let offset_inside_slot := and(sub(length, 1), <alignmentMask>)
					slot := add(slot, div(sub(length, 1), <wordSize>))
					let data := sload(slot)

					// Zero-out the suffix of the byte array by masking it.
					// ((1<<(8 * (VMWordBytes - offset))) - 1)
					let mask := sub(exp(0x100, sub(<wordSize>, offset_inside_slot)), 1)
					data := and(not(mask), data)
					sstore(slot, data)

					// Reduce the length by 1
					slot_value := sub(slot_value, 2)
				}
			}
			sstore(ref, slot_value)
		})");
		code("panicSelector", (u512(util::selectorFromSignatureU256("Panic(uint512)")) << (VMWordBits - 256)).str());
		code("emptyArrayPop", std::to_string(unsigned(util::PanicCode::EmptyArrayPop)));
		code("panicReturndataSize", std::to_string(4 + VMWordBytes));
		code("maxShortLength", std::to_string(VMWordAlignmentMask));
		code("shortMaskLength", std::to_string(VMWordBytes + 1));
		code("wordSize", std::to_string(VMWordBytes));
		code("maxShortLengthEncoded", std::to_string(VMWordAlignmentMask * 2));
		code("alignmentMask", std::to_string(VMWordAlignmentMask));
		m_context.appendInlineAssembly(code.render(), {"ref", "slot_value", "length"});
		m_context << Instruction::POP << Instruction::POP << Instruction::POP;
	}
	else
	{
		// stack: ArrayReference
		retrieveLength(_type);
		// stack: ArrayReference oldLength
		m_context << Instruction::DUP1;
		// stack: ArrayReference oldLength oldLength
		m_context << Instruction::ISZERO;
		m_context.appendConditionalPanic(util::PanicCode::EmptyArrayPop);

		// Stack: ArrayReference oldLength
		m_context << u256(1) << Instruction::SWAP1 << Instruction::SUB;
		// Stack ArrayReference newLength

		if (_type.baseType()->category() != Type::Category::Mapping)
		{
			m_context << Instruction::DUP2 << Instruction::DUP2;
			// Stack ArrayReference newLength ArrayReference newLength;
			accessIndex(_type, false);
			// Stack: ArrayReference newLength storage_slot byte_offset
			StorageItem(m_context, *_type.baseType()).setToZero(SourceLocation(), true);
		}

		// Stack: ArrayReference newLength
		m_context << Instruction::SWAP1 << Instruction::SSTORE;
	}
}

void ArrayUtils::clearStorageLoop(Type const* _type) const
{
	hypAssert(_type->storageSize() >= 1, "");
	m_context.callLowLevelFunction(
		"$clearStorageLoop_" + _type->identifier(),
		2,
		1,
		[_type](CompilerContext& _context)
		{
			unsigned stackHeightStart = _context.stackHeight();
			if (_type->category() == Type::Category::Mapping)
			{
				_context << Instruction::POP;
				return;
			}
			// stack: start_pos slot_count
			// Loop over a slot counter instead of comparing absolute slot numbers, so that
			// ranges wrapping past the last storage slot (2**512 - 1) are cleared as well.
			// Initialize loop counter i = 0
			_context << u256(0);
			// stack: start_pos slot_count i
			qrvmasm::AssemblyItem loopStart = _context.appendJumpToNew();
			_context << loopStart;
			// check for loop condition: !(slot_count > i) = (i >= slot_count)
			_context << Instruction::DUP1 << Instruction::DUP3 << Instruction::GT << Instruction::ISZERO;
			qrvmasm::AssemblyItem zeroLoopEnd = _context.newTag();
			_context.appendConditionalJumpTo(zeroLoopEnd);
			// stack: start_pos slot_count i
			// compute storage position: start_pos + i
			_context << Instruction::DUP3 << Instruction::DUP2 << Instruction::ADD;
			// stack: start_pos slot_count i (start_pos+i)
			// delete - always clear a full VM word regardless of type size
			if (_type->storageBytes() < VMWordBytes)
			{
				// For types smaller than a word, clear the entire slot with sstore(start_pos+i, 0)
				_context << u256(0) << Instruction::SWAP1 << Instruction::SSTORE;
			}
			else
			{
				_context << u256(0);
				StorageItem(_context, *_type).setToZero(SourceLocation(), /* _removeReference = */ true);
			}
			// stack: start_pos slot_count i
			// increment counter: i += storageSize
			_context << _type->storageSize() << Instruction::ADD;
			_context.appendJumpTo(loopStart);
			// cleanup
			_context << zeroLoopEnd;
			// stack: start_pos slot_count i
			_context << Instruction::POP << Instruction::POP;
			// stack: start_pos

			hypAssert(_context.stackHeight() == stackHeightStart - 1, "");
		}
	);
}

void ArrayUtils::convertLengthToSize(ArrayType const& _arrayType, bool _pad) const
{
	if (_arrayType.location() == DataLocation::Storage)
	{
		if (_arrayType.baseType()->storageSize() <= 1)
		{
			unsigned baseBytes = _arrayType.baseType()->storageBytes();
			if (baseBytes == 0)
				m_context << Instruction::POP << u256(1);
			else if (baseBytes <= (VMWordBytes / 2))
			{
				unsigned itemsPerSlot = VMWordBytes / baseBytes;
				m_context
					<< u256(itemsPerSlot - 1) << Instruction::ADD
					<< u256(itemsPerSlot) << Instruction::SWAP1 << Instruction::DIV;
			}
		}
		else
			m_context << _arrayType.baseType()->storageSize() << Instruction::MUL;
	}
	else
	{
		if (!_arrayType.isByteArrayOrString())
		{
			if (_arrayType.location() == DataLocation::Memory)
				m_context << _arrayType.memoryStride();
			else
				m_context << _arrayType.calldataStride();
			m_context << Instruction::MUL;
		}
		else if (_pad)
			m_context << u256(VMWordAlignmentMask) << Instruction::ADD
				<< u256(VMWordBytes) << Instruction::DUP1
				<< Instruction::SWAP2 << Instruction::DIV << Instruction::MUL;
	}
}

void ArrayUtils::retrieveLength(ArrayType const& _arrayType, unsigned _stackDepth) const
{
	if (!_arrayType.isDynamicallySized())
		m_context << _arrayType.length();
	else
	{
		m_context << dupInstruction(1 + _stackDepth);
		switch (_arrayType.location())
		{
		case DataLocation::CallData:
			// length is stored on the stack
			break;
		case DataLocation::Memory:
			m_context << Instruction::MLOAD;
			break;
		case DataLocation::Storage:
			m_context << Instruction::SLOAD;
			if (_arrayType.isByteArrayOrString())
				m_context.callYulFunction(m_context.utilFunctions().extractByteArrayLengthFunction(), 1, 1);
			break;
		}
	}
}

void ArrayUtils::accessIndex(ArrayType const& _arrayType, bool _doBoundsCheck, bool _keepReference) const
{
	/// Stack: reference [length] index
	DataLocation location = _arrayType.location();

	if (_doBoundsCheck)
	{
		// retrieve length
		ArrayUtils::retrieveLength(_arrayType, 1);
		// Stack: ref [length] index length
		// check out-of-bounds access
		m_context << Instruction::DUP2 << Instruction::LT << Instruction::ISZERO;
		// out-of-bounds access throws exception
		m_context.appendConditionalPanic(util::PanicCode::ArrayOutOfBounds);
	}
	if (location == DataLocation::CallData && _arrayType.isDynamicallySized())
		// remove length if present
		m_context << Instruction::SWAP1 << Instruction::POP;

	// stack: <base_ref> <index>
	switch (location)
	{
	case DataLocation::Memory:
		// stack: <base_ref> <index>
		if (!_arrayType.isByteArrayOrString())
			m_context << u256(_arrayType.memoryStride()) << Instruction::MUL;
		if (_arrayType.isDynamicallySized())
			m_context << u256(VMWordBytes) << Instruction::ADD;
		if (_keepReference)
			m_context << Instruction::DUP2;
		m_context << Instruction::ADD;
		break;
	case DataLocation::CallData:
		if (!_arrayType.isByteArrayOrString())
		{
			m_context << _arrayType.calldataStride();
			m_context << Instruction::MUL;
		}
		// stack: <base_ref> <index * size>
		if (_keepReference)
			m_context << Instruction::DUP2;
		m_context << Instruction::ADD;
		break;
	case DataLocation::Storage:
	{
		if (_keepReference)
			m_context << Instruction::DUP2;
		else
			m_context << Instruction::SWAP1;
		// stack: [<base_ref>] <index> <base_ref>

		qrvmasm::AssemblyItem endTag = m_context.newTag();
		if (_arrayType.isByteArrayOrString())
		{
			// Special case of short byte arrays.
			m_context << Instruction::SWAP1;
			m_context << Instruction::DUP2 << Instruction::SLOAD;
			m_context << u256(1) << Instruction::AND << Instruction::ISZERO;
			// No action needed for short byte arrays.
			m_context.appendConditionalJumpTo(endTag);
			m_context << Instruction::SWAP1;
		}
		if (_arrayType.isDynamicallySized())
			CompilerUtils(m_context).computeHashStatic();
		m_context << Instruction::SWAP1;
		if (_arrayType.baseType()->storageBytes() <= (VMWordBytes / 2))
		{
			// stack: <data_ref> <index>
			// goal:
			// <ref> <byte_number> = <base_ref + index / itemsPerSlot> <(index % itemsPerSlot) * byteSize>
			unsigned byteSize = _arrayType.baseType()->storageBytes();
			hypAssert(byteSize != 0, "");
			unsigned itemsPerSlot = VMWordBytes / byteSize;
			m_context << u256(itemsPerSlot) << Instruction::SWAP2;
			// stack: itemsPerSlot index data_ref
			m_context
				<< Instruction::DUP3 << Instruction::DUP3
				<< Instruction::DIV << Instruction::ADD
			// stack: itemsPerSlot index (data_ref + index / itemsPerSlot)
				<< Instruction::SWAP2 << Instruction::SWAP1
				<< Instruction::MOD;
			if (byteSize != 1)
				m_context << u256(byteSize) << Instruction::MUL;
		}
		else
		{
			if (_arrayType.baseType()->storageSize() != 1)
				m_context << _arrayType.baseType()->storageSize() << Instruction::MUL;
			m_context << Instruction::ADD << u256(0);
		}
		m_context << endTag;
		break;
	}
	}
}

void ArrayUtils::accessCallDataArrayElement(ArrayType const& _arrayType, bool _doBoundsCheck) const
{
	hypAssert(_arrayType.location() == DataLocation::CallData, "");
	if (_arrayType.baseType()->isDynamicallyEncoded())
	{
		// stack layout: <base_ref> <length> <index>
		ArrayUtils(m_context).accessIndex(_arrayType, _doBoundsCheck, true);
		// stack layout: <base_ref> <ptr_to_tail>

		CompilerUtils(m_context).accessCalldataTail(*_arrayType.baseType());
		// stack layout: <tail_ref> [length]
	}
	else
	{
		ArrayUtils(m_context).accessIndex(_arrayType, _doBoundsCheck);
		if (_arrayType.baseType()->isValueType())
		{
			hypAssert(_arrayType.baseType()->storageBytes() <= VMWordBytes, "");
			if (
				!_arrayType.isByteArrayOrString() &&
				_arrayType.baseType()->storageBytes() < VMWordBytes
			)
			{
				m_context << u256(VMWordBytes);
				CompilerUtils(m_context).abiDecodeV2({_arrayType.baseType()}, false);
			}
			else
				CompilerUtils(m_context).loadFromMemoryDynamic(
					*_arrayType.baseType(),
					true,
					!_arrayType.isByteArrayOrString(),
					false
				);
		}
		else
			hypAssert(
				_arrayType.baseType()->category() == Type::Category::Struct ||
				_arrayType.baseType()->category() == Type::Category::Array,
				"Invalid statically sized non-value base type on array access."
			);
	}
}

void ArrayUtils::incrementByteOffset(unsigned _byteSize, unsigned _byteOffsetPosition, unsigned _storageOffsetPosition) const
{
	hypAssert(_byteSize < VMWordBytes, "");
	hypAssert(_byteSize != 0, "");
	// We do the following, but avoiding jumps:
	// byteOffset += byteSize
	// if (byteOffset + byteSize > VMWordBytes)
	// {
	//     storageOffset++;
	//     byteOffset = 0;
	// }
	if (_byteOffsetPosition > 1)
		m_context << swapInstruction(_byteOffsetPosition - 1);
	m_context << u256(_byteSize) << Instruction::ADD;
	if (_byteOffsetPosition > 1)
		m_context << swapInstruction(_byteOffsetPosition - 1);
	// compute, X := (byteOffset + byteSize - 1) / VMWordBytes, should be 1 iff byteOffset + bytesize > VMWordBytes
	m_context
		<< u256(VMWordBytes) << dupInstruction(1 + _byteOffsetPosition) << u256(_byteSize - 1)
		<< Instruction::ADD << Instruction::DIV;
	// increment storage offset if X == 1 (just add X to it)
	// stack: X
	m_context
		<< swapInstruction(_storageOffsetPosition) << dupInstruction(_storageOffsetPosition + 1)
		<< Instruction::ADD << swapInstruction(_storageOffsetPosition);
	// stack: X
	// set source_byte_offset to zero if X == 1 (using source_byte_offset *= 1 - X)
	m_context << u256(1) << Instruction::SUB;
	// stack: 1 - X
	if (_byteOffsetPosition == 1)
		m_context << Instruction::MUL;
	else
		m_context
			<< dupInstruction(_byteOffsetPosition + 1) << Instruction::MUL
			<< swapInstruction(_byteOffsetPosition) << Instruction::POP;
}
