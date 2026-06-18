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
/** @file LinkerObject.cpp
 * @author Christian R <c@ethdev.com>
 * @date 2015
 */

#include <libqrvmasm/LinkerObject.h>
#include <libhyputil/CommonData.h>
#include <libhyputil/Keccak256.h>
#include <libhyputil/VMConstants.h>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::qrvmasm;

void LinkerObject::append(LinkerObject const& _other)
{
	for (auto const& ref: _other.linkReferences)
		linkReferences[ref.first + bytecode.size()] = ref.second;
	bytecode += _other.bytecode;
}

void LinkerObject::link(std::map<std::string, h512> const& _libraryAddresses)
{
	std::map<size_t, std::string> remainingRefs;
	for (auto const& linkRef: linkReferences)
		if (h512 const* address = matchLibrary(linkRef.second, _libraryAddresses))
			copy(address->data(), address->data() + hyperion::AddressBytes, bytecode.begin() + std::vector<uint8_t>::difference_type(linkRef.first));
		else
			remainingRefs.insert(linkRef);
	linkReferences.swap(remainingRefs);
}

std::string LinkerObject::toHex() const
{
	// Library address placeholder in bytecode hex is `__$<hash>$__` spanning
	// 2 * AddressBytes hex chars: 2 leading underscores, `$`, (2*AddressBytes - 6)
	// hash chars, `$`, 2 trailing underscores.
	size_t constexpr addressHex = 2 * hyperion::AddressBytes;
	size_t constexpr placeholderInnerLen = addressHex - 4;
	std::string hex = hyperion::util::toHex(bytecode);
	for (auto const& ref: linkReferences)
	{
		size_t pos = ref.first * 2;
		std::string hash = libraryPlaceholder(ref.second);
		hex[pos] = hex[pos + 1] = hex[pos + addressHex - 2] = hex[pos + addressHex - 1] = '_';
		for (size_t i = 0; i < placeholderInnerLen; ++i)
			hex[pos + 2 + i] = hash.at(i);
	}
	return hex;
}

std::string LinkerObject::libraryPlaceholder(std::string const& _libraryName)
{
	// Need (2 * AddressBytes - 6) hex chars between the two `$` signs so the
	// whole placeholder spans 2 * AddressBytes hex chars (with surrounding
	// `__...__`). A single keccak256 yields 64 hex chars; concatenate further
	// hashes until we have enough material.
	size_t constexpr hashHexLen = 2 * hyperion::AddressBytes - 6;
	std::string hash = keccak256(_libraryName).hex();
	while (hash.size() < hashHexLen)
		hash += keccak256(hash).hex();
	return "$" + hash.substr(0, hashHexLen) + "$";
}

h512 const*
LinkerObject::matchLibrary(
	std::string const& _linkRefName,
	std::map<std::string, h512> const& _libraryAddresses
)
{
	auto it = _libraryAddresses.find(_linkRefName);
	if (it != _libraryAddresses.end())
		return &it->second;
	return nullptr;
}

bool LinkerObject::operator<(LinkerObject const& _other) const
{
	return tie(this->bytecode, this->linkReferences, this->immutableReferences) <
			tie(_other.bytecode, _other.linkReferences, _other.immutableReferences);
}
