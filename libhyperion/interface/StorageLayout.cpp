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

#include <libhyperion/interface/StorageLayout.h>

#include <libhyperion/ast/TypeProvider.h>

using namespace hyperion;
using namespace hyperion::frontend;

Json::Value StorageLayout::generate(ContractDefinition const& _contractDef)
{
	hypAssert(!m_contract, "");
	m_contract = &_contractDef;
	m_types.clear();

	auto typeType = dynamic_cast<TypeType const*>(_contractDef.type());
	hypAssert(typeType, "");
	auto contractType = dynamic_cast<ContractType const*>(typeType->actualType());
	hypAssert(contractType, "");

	Json::Value variables(Json::arrayValue);
	for (auto [var, slot, offset]: contractType->stateVariables())
		variables.append(generate(*var, slot, offset));

	Json::Value layout;
	layout["storage"] = std::move(variables);
	layout["types"] = std::move(m_types);
	return layout;
}

Json::Value StorageLayout::generate(VariableDeclaration const& _var, u256 const& _slot, unsigned _offset)
{
	Json::Value varEntry;
	Type const* varType = _var.type();

	varEntry["label"] = _var.name();
	varEntry["astId"] = static_cast<int>(_var.id());
	varEntry["contract"] = m_contract->fullyQualifiedName();
	varEntry["slot"] = _slot.str();
	varEntry["offset"] = _offset;
	varEntry["type"] = typeKeyName(varType);

	generate(varType);

	return varEntry;
}

void StorageLayout::generate(Type const* _type)
{
	if (m_types.isMember(typeKeyName(_type)))
		return;

	// Register it now to cut recursive visits.
	Json::Value& typeInfo = m_types[typeKeyName(_type)];
	typeInfo["label"] = _type->toString(true);
	typeInfo["numberOfBytes"] = u256(_type->storageBytes() * _type->storageSize()).str();

	if (auto structType = dynamic_cast<StructType const*>(_type))
	{
		Json::Value members(Json::arrayValue);
		auto const& structDef = structType->structDefinition();
		for (auto const& member: structDef.members())
		{
			auto const& offsets = structType->storageOffsetsOfMember(member->name());
			members.append(generate(*member, offsets.first, offsets.second));
		}
		typeInfo["members"] = std::move(members);
		typeInfo["encoding"] = "inplace";
	}
	else if (auto mappingType = dynamic_cast<MappingType const*>(_type))
	{
		typeInfo["key"] = typeKeyName(mappingType->keyType());
		typeInfo["value"] = typeKeyName(mappingType->valueType());
		generate(mappingType->keyType());
		generate(mappingType->valueType());
		typeInfo["encoding"] = "mapping";
	}
	else if (auto arrayType = dynamic_cast<ArrayType const*>(_type))
	{
		if (arrayType->isByteArrayOrString())
			typeInfo["encoding"] = "bytes";
		else
		{
			typeInfo["base"] = typeKeyName(arrayType->baseType());
			generate(arrayType->baseType());
			typeInfo["encoding"] = arrayType->isDynamicallySized() ? "dynamic_array" : "inplace";
		}
	}
	else
	{
		hypAssert(_type->isValueType(), "");
		typeInfo["encoding"] = "inplace";
	}

	hypAssert(typeInfo.isMember("encoding"), "");
}

std::string StorageLayout::typeKeyName(Type const* _type)
{
	if (auto refType = dynamic_cast<ReferenceType const*>(_type))
		return TypeProvider::withLocationIfReference(refType->location(), _type)->richIdentifier();
	return _type->richIdentifier();
}
