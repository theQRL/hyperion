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
 * Adaptor between AbstractAssembly and libzvmasm.
 */

#include <libyul/backends/zvm/ZondAssemblyAdapter.h>

#include <libyul/backends/zvm/AbstractAssembly.h>
#include <libyul/Exceptions.h>

#include <libzvmasm/Assembly.h>
#include <libzvmasm/AssemblyItem.h>
#include <libzvmasm/Instruction.h>

#include <liblangutil/SourceLocation.h>

#include <memory>
#include <functional>

using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;
using namespace hyperion::langutil;

ZondAssemblyAdapter::ZondAssemblyAdapter(zvmasm::Assembly& _assembly):
	m_assembly(_assembly)
{
}

void ZondAssemblyAdapter::setSourceLocation(SourceLocation const& _location)
{
	m_assembly.setSourceLocation(_location);
}

int ZondAssemblyAdapter::stackHeight() const
{
	return m_assembly.deposit();
}

void ZondAssemblyAdapter::setStackHeight(int height)
{
	m_assembly.setDeposit(height);
}

void ZondAssemblyAdapter::appendInstruction(zvmasm::Instruction _instruction)
{
	m_assembly.append(_instruction);
}

void ZondAssemblyAdapter::appendConstant(u256 const& _constant)
{
	m_assembly.append(_constant);
}

void ZondAssemblyAdapter::appendLabel(LabelID _labelId)
{
	m_assembly.append(zvmasm::AssemblyItem(zvmasm::Tag, _labelId));
}

void ZondAssemblyAdapter::appendLabelReference(LabelID _labelId)
{
	m_assembly.append(zvmasm::AssemblyItem(zvmasm::PushTag, _labelId));
}

size_t ZondAssemblyAdapter::newLabelId()
{
	return assemblyTagToIdentifier(m_assembly.newTag());
}

size_t ZondAssemblyAdapter::namedLabel(std::string const& _name, size_t _params, size_t _returns, std::optional<size_t> _sourceID)
{
	return assemblyTagToIdentifier(m_assembly.namedTag(_name, _params, _returns, _sourceID));
}

void ZondAssemblyAdapter::appendLinkerSymbol(std::string const& _linkerSymbol)
{
	m_assembly.appendLibraryAddress(_linkerSymbol);
}

void ZondAssemblyAdapter::appendVerbatim(bytes _data, size_t _arguments, size_t _returnVariables)
{
	m_assembly.appendVerbatim(std::move(_data), _arguments, _returnVariables);
}

void ZondAssemblyAdapter::appendJump(int _stackDiffAfter, JumpType _jumpType)
{
	appendJumpInstruction(zvmasm::Instruction::JUMP, _jumpType);
	m_assembly.adjustDeposit(_stackDiffAfter);
}

void ZondAssemblyAdapter::appendJumpTo(LabelID _labelId, int _stackDiffAfter, JumpType _jumpType)
{
	appendLabelReference(_labelId);
	appendJump(_stackDiffAfter, _jumpType);
}

void ZondAssemblyAdapter::appendJumpToIf(LabelID _labelId, JumpType _jumpType)
{
	appendLabelReference(_labelId);
	appendJumpInstruction(zvmasm::Instruction::JUMPI, _jumpType);
}

void ZondAssemblyAdapter::appendAssemblySize()
{
	m_assembly.appendProgramSize();
}

std::pair<std::shared_ptr<AbstractAssembly>, AbstractAssembly::SubID> ZondAssemblyAdapter::createSubAssembly(bool _creation, std::string _name)
{
	std::shared_ptr<zvmasm::Assembly> assembly{std::make_shared<zvmasm::Assembly>(m_assembly.zvmVersion(), _creation, std::move(_name))};
	auto sub = m_assembly.newSub(assembly);
	return {std::make_shared<ZondAssemblyAdapter>(*assembly), static_cast<size_t>(sub.data())};
}

void ZondAssemblyAdapter::appendDataOffset(std::vector<AbstractAssembly::SubID> const& _subPath)
{
	if (auto it = m_dataHashBySubId.find(_subPath[0]); it != m_dataHashBySubId.end())
	{
		yulAssert(_subPath.size() == 1, "");
		m_assembly << zvmasm::AssemblyItem(zvmasm::PushData, it->second);
		return;
	}

	m_assembly.pushSubroutineOffset(m_assembly.encodeSubPath(_subPath));
}

void ZondAssemblyAdapter::appendDataSize(std::vector<AbstractAssembly::SubID> const& _subPath)
{
	if (auto it = m_dataHashBySubId.find(_subPath[0]); it != m_dataHashBySubId.end())
	{
		yulAssert(_subPath.size() == 1, "");
		m_assembly << u256(m_assembly.data(h256(it->second)).size());
		return;
	}

	m_assembly.pushSubroutineSize(m_assembly.encodeSubPath(_subPath));
}

AbstractAssembly::SubID ZondAssemblyAdapter::appendData(bytes const& _data)
{
	zvmasm::AssemblyItem pushData = m_assembly.newData(_data);
	SubID subID = m_nextDataCounter++;
	m_dataHashBySubId[subID] = pushData.data();
	return subID;
}

void ZondAssemblyAdapter::appendToAuxiliaryData(bytes const& _data)
{
	m_assembly.appendToAuxiliaryData(_data);
}

void ZondAssemblyAdapter::appendImmutable(std::string const& _identifier)
{
	m_assembly.appendImmutable(_identifier);
}

void ZondAssemblyAdapter::appendImmutableAssignment(std::string const& _identifier)
{
	m_assembly.appendImmutableAssignment(_identifier);
}

void ZondAssemblyAdapter::markAsInvalid()
{
	m_assembly.markAsInvalid();
}

langutil::ZVMVersion ZondAssemblyAdapter::zvmVersion() const
{
	return m_assembly.zvmVersion();
}

ZondAssemblyAdapter::LabelID ZondAssemblyAdapter::assemblyTagToIdentifier(zvmasm::AssemblyItem const& _tag)
{
	u256 id = _tag.data();
	yulAssert(id <= std::numeric_limits<LabelID>::max(), "Tag id too large.");
	return LabelID(id);
}

void ZondAssemblyAdapter::appendJumpInstruction(zvmasm::Instruction _instruction, JumpType _jumpType)
{
	yulAssert(_instruction == zvmasm::Instruction::JUMP || _instruction == zvmasm::Instruction::JUMPI, "");
	zvmasm::AssemblyItem jump(_instruction);
	switch (_jumpType)
	{
	case JumpType::Ordinary:
		yulAssert(jump.getJumpType() == zvmasm::AssemblyItem::JumpType::Ordinary, "");
		break;
	case JumpType::IntoFunction:
		jump.setJumpType(zvmasm::AssemblyItem::JumpType::IntoFunction);
		break;
	case JumpType::OutOfFunction:
		jump.setJumpType(zvmasm::AssemblyItem::JumpType::OutOfFunction);
		break;
	}
	m_assembly.append(std::move(jump));
}
