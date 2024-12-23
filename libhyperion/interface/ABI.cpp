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
 * Utilities to handle the Contract ABI (https://docs.soliditylang.org/en/develop/abi-spec.html)
 */

#include <libhyperion/interface/ABI.h>

#include <libhyperion/ast/AST.h>

using namespace hyperion;
using namespace hyperion::frontend;

namespace
{
bool anyDataStoredInStorage(TypePointers const& _pointers)
{
	for (Type const* pointer: _pointers)
		if (pointer->dataStoredIn(DataLocation::Storage))
			return true;

	return false;
}
}

Json::Value ABI::generate(ContractDefinition const& _contractDef)
{
	auto compare = [](Json::Value const& _a, Json::Value const& _b) -> bool {
		return std::make_tuple(_a["type"], _a["name"]) < std::make_tuple(_b["type"], _b["name"]);
	};
	std::multiset<Json::Value, decltype(compare)> abi(compare);

	for (auto it: _contractDef.interfaceFunctions())
	{
		if (_contractDef.isLibrary() && (
			it.second->stateMutability() > StateMutability::View ||
			anyDataStoredInStorage(it.second->parameterTypes() + it.second->returnParameterTypes())
		))
			continue;

		FunctionType const* externalFunctionType = it.second->interfaceFunctionType();
		hypAssert(!!externalFunctionType, "");
		Json::Value method{Json::objectValue};
		method["type"] = "function";
		method["name"] = it.second->declaration().name();
		method["stateMutability"] = stateMutabilityToString(externalFunctionType->stateMutability());
		method["inputs"] = formatTypeList(
			externalFunctionType->parameterNames(),
			externalFunctionType->parameterTypes(),
			it.second->parameterTypes(),
			_contractDef.isLibrary()
		);
		method["outputs"] = formatTypeList(
			externalFunctionType->returnParameterNames(),
			externalFunctionType->returnParameterTypes(),
			it.second->returnParameterTypes(),
			_contractDef.isLibrary()
		);
		abi.emplace(std::move(method));
	}
	FunctionDefinition const* constructor = _contractDef.constructor();
	if (constructor && !_contractDef.abstract())
	{
		FunctionType constrType(*constructor);
		FunctionType const* externalFunctionType = constrType.interfaceFunctionType();
		hypAssert(!!externalFunctionType, "");
		Json::Value method{Json::objectValue};
		method["type"] = "constructor";
		method["stateMutability"] = stateMutabilityToString(externalFunctionType->stateMutability());
		method["inputs"] = formatTypeList(
			externalFunctionType->parameterNames(),
			externalFunctionType->parameterTypes(),
			constrType.parameterTypes(),
			_contractDef.isLibrary()
		);
		abi.emplace(std::move(method));
	}
	for (auto const* fallbackOrReceive: {_contractDef.fallbackFunction(), _contractDef.receiveFunction()})
		if (fallbackOrReceive)
		{
			auto const* externalFunctionType = FunctionType(*fallbackOrReceive).interfaceFunctionType();
			hypAssert(!!externalFunctionType, "");
			Json::Value method{Json::objectValue};
			method["type"] = TokenTraits::toString(fallbackOrReceive->kind());
			method["stateMutability"] = stateMutabilityToString(externalFunctionType->stateMutability());
			abi.emplace(std::move(method));
		}
	for (auto const& it: _contractDef.interfaceEvents())
	{
		Json::Value event{Json::objectValue};
		event["type"] = "event";
		event["name"] = it->name();
		event["anonymous"] = it->isAnonymous();
		Json::Value params{Json::arrayValue};
		for (auto const& p: it->parameters())
		{
			Type const* type = p->annotation().type->interfaceType(false);
			hypAssert(type, "");
			auto param = formatType(p->name(), *type, *p->annotation().type, false);
			param["indexed"] = p->isIndexed();
			params.append(std::move(param));
		}
		event["inputs"] = std::move(params);
		abi.emplace(std::move(event));
	}

	for (ErrorDefinition const* error: _contractDef.interfaceErrors())
	{
		Json::Value errorJson{Json::objectValue};
		errorJson["type"] = "error";
		errorJson["name"] = error->name();
		errorJson["inputs"] = Json::arrayValue;
		for (auto const& p: error->parameters())
		{
			Type const* type = p->annotation().type->interfaceType(false);
			hypAssert(type, "");
			errorJson["inputs"].append(
				formatType(p->name(), *type, *p->annotation().type, false)
			);
		}
		abi.emplace(std::move(errorJson));
	}

	Json::Value abiJson{Json::arrayValue};
	for (auto& f: abi)
		abiJson.append(std::move(f));
	return abiJson;
}

Json::Value ABI::formatTypeList(
	std::vector<std::string> const& _names,
	std::vector<Type const*> const& _encodingTypes,
	std::vector<Type const*> const& _hyperionTypes,
	bool _forLibrary
)
{
	Json::Value params{Json::arrayValue};
	hypAssert(_names.size() == _encodingTypes.size(), "Names and types vector size does not match");
	hypAssert(_names.size() == _hyperionTypes.size(), "");
	for (unsigned i = 0; i < _names.size(); ++i)
	{
		hypAssert(_encodingTypes[i], "");
		params.append(formatType(_names[i], *_encodingTypes[i], *_hyperionTypes[i], _forLibrary));
	}
	return params;
}

Json::Value ABI::formatType(
	std::string const& _name,
	Type const& _encodingType,
	Type const& _hyperionType,
	bool _forLibrary
)
{
	Json::Value ret{Json::objectValue};
	ret["name"] = _name;
	ret["internalType"] = _hyperionType.toString(true);
	std::string suffix = (_forLibrary && _encodingType.dataStoredIn(DataLocation::Storage)) ? " storage" : "";
	if (_encodingType.isValueType() || (_forLibrary && _encodingType.dataStoredIn(DataLocation::Storage)))
		ret["type"] = _encodingType.canonicalName() + suffix;
	else if (ArrayType const* arrayType = dynamic_cast<ArrayType const*>(&_encodingType))
	{
		if (arrayType->isByteArrayOrString())
			ret["type"] = _encodingType.canonicalName() + suffix;
		else
		{
			std::string suffix;
			if (arrayType->isDynamicallySized())
				suffix = "[]";
			else
				suffix = std::string("[") + arrayType->length().str() + "]";
			hypAssert(arrayType->baseType(), "");
			Json::Value subtype = formatType(
				"",
				*arrayType->baseType(),
				*dynamic_cast<ArrayType const&>(_hyperionType).baseType(),
				_forLibrary
			);
			if (subtype.isMember("components"))
			{
				ret["type"] = subtype["type"].asString() + suffix;
				ret["components"] = subtype["components"];
			}
			else
				ret["type"] = subtype["type"].asString() + suffix;
		}
	}
	else if (StructType const* structType = dynamic_cast<StructType const*>(&_encodingType))
	{
		ret["type"] = "tuple";
		ret["components"] = Json::arrayValue;
		for (auto const& member: structType->members(nullptr))
		{
			hypAssert(member.type, "");
			Type const* t = member.type->interfaceType(_forLibrary);
			hypAssert(t, "");
			ret["components"].append(formatType(member.name, *t, *member.type, _forLibrary));
		}
	}
	else
		hypAssert(false, "Invalid type.");
	return ret;
}
