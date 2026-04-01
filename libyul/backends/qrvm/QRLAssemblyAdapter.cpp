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
 * Adaptor between AbstractAssembly and libqrvmasm.
 */

#include <libyul/backends/qrvm/QRLAssemblyAdapter.h>

#include <libyul/backends/qrvm/AbstractAssembly.h>
#include <libyul/Exceptions.h>

#include <libqrvmasm/Assembly.h>
#include <libqrvmasm/AssemblyItem.h>
#include <libqrvmasm/Instruction.h>

#include <liblangutil/SourceLocation.h>

#include <memory>
#include <functional>

using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;
using namespace hyperion::langutil;

QRLAssemblyAdapter::QRLAssemblyAdapter(qrvmasm::Assembly& _assembly):
	m_assembly(_assembly)
{
}

void QRLAssemblyAdapter::setSourceLocation(SourceLocation const& _location)
{
	m_assembly.setSourceLocation(_location);
}

int QRLAssemblyAdapter::stackHeight() const
{
	return m_assembly.deposit();
}

void QRLAssemblyAdapter::setStackHeight(int height)
{
	m_assembly.setDeposit(height);
}

void QRLAssemblyAdapter::appendInstruction(qrvmasm::Instruction _instruction)
{
	m_assembly.append(_instruction);
}

void QRLAssemblyAdapter::appendConstant(u256 const& _constant)
{
	m_assembly.append(_constant);
}

void QRLAssemblyAdapter::appendLabel(LabelID _labelId)
{
	m_assembly.append(qrvmasm::AssemblyItem(qrvmasm::Tag, _labelId));
}

void QRLAssemblyAdapter::appendLabelReference(LabelID _labelId)
{
	m_assembly.append(qrvmasm::AssemblyItem(qrvmasm::PushTag, _labelId));
}

size_t QRLAssemblyAdapter::newLabelId()
{
	return assemblyTagToIdentifier(m_assembly.newTag());
}

size_t QRLAssemblyAdapter::namedLabel(std::string const& _name, size_t _params, size_t _returns, std::optional<size_t> _sourceID)
{
	return assemblyTagToIdentifier(m_assembly.namedTag(_name, _params, _returns, _sourceID));
}

void QRLAssemblyAdapter::appendLinkerSymbol(std::string const& _linkerSymbol)
{
	m_assembly.appendLibraryAddress(_linkerSymbol);
}

void QRLAssemblyAdapter::appendVerbatim(bytes _data, size_t _arguments, size_t _returnVariables)
{
	m_assembly.appendVerbatim(std::move(_data), _arguments, _returnVariables);
}

void QRLAssemblyAdapter::appendJump(int _stackDiffAfter, JumpType _jumpType)
{
	appendJumpInstruction(qrvmasm::Instruction::JUMP, _jumpType);
	m_assembly.adjustDeposit(_stackDiffAfter);
}

void QRLAssemblyAdapter::appendJumpTo(LabelID _labelId, int _stackDiffAfter, JumpType _jumpType)
{
	appendLabelReference(_labelId);
	appendJump(_stackDiffAfter, _jumpType);
}

void QRLAssemblyAdapter::appendJumpToIf(LabelID _labelId, JumpType _jumpType)
{
	appendLabelReference(_labelId);
	appendJumpInstruction(qrvmasm::Instruction::JUMPI, _jumpType);
}

void QRLAssemblyAdapter::appendAssemblySize()
{
	m_assembly.appendProgramSize();
}

std::pair<std::shared_ptr<AbstractAssembly>, AbstractAssembly::SubID> QRLAssemblyAdapter::createSubAssembly(bool _creation, std::string _name)
{
	std::shared_ptr<qrvmasm::Assembly> assembly{std::make_shared<qrvmasm::Assembly>(m_assembly.qrvmVersion(), _creation, std::move(_name))};
	auto sub = m_assembly.newSub(assembly);
	return {std::make_shared<QRLAssemblyAdapter>(*assembly), static_cast<size_t>(sub.data())};
}

void QRLAssemblyAdapter::appendDataOffset(std::vector<AbstractAssembly::SubID> const& _subPath)
{
	if (auto it = m_dataHashBySubId.find(_subPath[0]); it != m_dataHashBySubId.end())
	{
		yulAssert(_subPath.size() == 1, "");
		m_assembly << qrvmasm::AssemblyItem(qrvmasm::PushData, it->second);
		return;
	}

	m_assembly.pushSubroutineOffset(m_assembly.encodeSubPath(_subPath));
}

void QRLAssemblyAdapter::appendDataSize(std::vector<AbstractAssembly::SubID> const& _subPath)
{
	if (auto it = m_dataHashBySubId.find(_subPath[0]); it != m_dataHashBySubId.end())
	{
		yulAssert(_subPath.size() == 1, "");
		m_assembly << u256(m_assembly.data(h256(it->second)).size());
		return;
	}

	m_assembly.pushSubroutineSize(m_assembly.encodeSubPath(_subPath));
}

AbstractAssembly::SubID QRLAssemblyAdapter::appendData(bytes const& _data)
{
	qrvmasm::AssemblyItem pushData = m_assembly.newData(_data);
	SubID subID = m_nextDataCounter++;
	m_dataHashBySubId[subID] = pushData.data();
	return subID;
}

void QRLAssemblyAdapter::appendToAuxiliaryData(bytes const& _data)
{
	m_assembly.appendToAuxiliaryData(_data);
}

void QRLAssemblyAdapter::appendImmutable(std::string const& _identifier)
{
	m_assembly.appendImmutable(_identifier);
}

void QRLAssemblyAdapter::appendImmutableAssignment(std::string const& _identifier)
{
	m_assembly.appendImmutableAssignment(_identifier);
}

void QRLAssemblyAdapter::markAsInvalid()
{
	m_assembly.markAsInvalid();
}

langutil::QRVMVersion QRLAssemblyAdapter::qrvmVersion() const
{
	return m_assembly.qrvmVersion();
}

QRLAssemblyAdapter::LabelID QRLAssemblyAdapter::assemblyTagToIdentifier(qrvmasm::AssemblyItem const& _tag)
{
	u256 id = _tag.data();
	yulAssert(id <= std::numeric_limits<LabelID>::max(), "Tag id too large.");
	return LabelID(id);
}

void QRLAssemblyAdapter::appendJumpInstruction(qrvmasm::Instruction _instruction, JumpType _jumpType)
{
	yulAssert(_instruction == qrvmasm::Instruction::JUMP || _instruction == qrvmasm::Instruction::JUMPI, "");
	qrvmasm::AssemblyItem jump(_instruction);
	switch (_jumpType)
	{
	case JumpType::Ordinary:
		yulAssert(jump.getJumpType() == qrvmasm::AssemblyItem::JumpType::Ordinary, "");
		break;
	case JumpType::IntoFunction:
		jump.setJumpType(qrvmasm::AssemblyItem::JumpType::IntoFunction);
		break;
	case JumpType::OutOfFunction:
		jump.setJumpType(qrvmasm::AssemblyItem::JumpType::OutOfFunction);
		break;
	}
	m_assembly.append(std::move(jump));
}
