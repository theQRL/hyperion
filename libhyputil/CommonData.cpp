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
/** @file CommonData.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libhyputil/Assertions.h>
#include <libhyputil/CommonData.h>
#include <libhyputil/Exceptions.h>
#include <libhyputil/FixedHash.h>
#include <libhyputil/Keccak256.h>
#include <libhyputil/StringUtils.h>
#include <libhyputil/VMConstants.h>

#include <boost/algorithm/string.hpp>

using namespace hyperion;
using namespace hyperion::util;

namespace
{

static char const* upperHexChars = "0123456789ABCDEF";
static char const* lowerHexChars = "0123456789abcdef";

bool isAddressHex(std::string const& _body)
{
	return _body.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

bool isUniformHexCase(std::string const& _body)
{
	bool hasLower = false;
	bool hasUpper = false;
	for (char c: _body)
	{
		hasLower = hasLower || (c >= 'a' && c <= 'f');
		hasUpper = hasUpper || (c >= 'A' && c <= 'F');
	}
	return !hasLower || !hasUpper;
}

std::string checksummedAddressBody(std::string const& _body)
{
	std::string lower = boost::algorithm::to_lower_copy(_body, std::locale::classic());
	bytes hash = shake256(lower, AddressBytes);

	for (size_t i = 0; i < lower.size(); ++i)
	{
		char& c = lower[i];
		if (c < 'a' || c > 'f')
			continue;

		uint8_t const hashByte = hash[i / 2];
		uint8_t const nibble = (i % 2 == 0) ? (hashByte >> 4) : (hashByte & 0x0f);
		if (nibble >= 8)
			c = static_cast<char>(c - ('a' - 'A'));
	}
	return lower;
}

}

std::string hyperion::util::toHex(uint8_t _data, HexCase _case)
{
	assertThrow(_case != HexCase::Mixed, BadHexCase, "Mixed case can only be used for byte arrays.");

	char const* chars = _case == HexCase::Upper ? upperHexChars : lowerHexChars;

	return std::string{
		chars[(unsigned(_data) / 16) & 0xf],
		chars[unsigned(_data) & 0xf]
	};
}

std::string hyperion::util::toHex(bytes const& _data, HexPrefix _prefix, HexCase _case)
{
	std::string ret(_data.size() * 2 + (_prefix == HexPrefix::Add ? 2 : 0), 0);

	size_t i = 0;
	if (_prefix == HexPrefix::Add)
	{
		ret[i++] = '0';
		ret[i++] = 'x';
	}

	// Mixed case will be handled inside the loop.
	char const* chars = _case == HexCase::Upper ? upperHexChars : lowerHexChars;
	size_t rix = _data.size() - 1;
	for (uint8_t c: _data)
	{
		// switch hex case every four hexchars
		if (_case == HexCase::Mixed)
			chars = (rix-- & 2) == 0 ? lowerHexChars : upperHexChars;

		ret[i++] = chars[(static_cast<size_t>(c) >> 4ul) & 0xfu];
		ret[i++] = chars[c & 0xfu];
	}
	assertThrow(i == ret.size(), Exception, "");

	return ret;
}

int hyperion::util::fromHex(char _i, WhenError _throw)
{
	if (_i >= '0' && _i <= '9')
		return _i - '0';
	if (_i >= 'a' && _i <= 'f')
		return _i - 'a' + 10;
	if (_i >= 'A' && _i <= 'F')
		return _i - 'A' + 10;
	if (_throw == WhenError::Throw)
		assertThrow(false, BadHexCharacter, std::to_string(_i));
	else
		return -1;
}

bytes hyperion::util::fromHex(std::string const& _s, WhenError _throw)
{
	if (_s.empty())
		return {};

	unsigned s = (_s.size() >= 2 && _s[0] == '0' && _s[1] == 'x') ? 2 : 0;
	std::vector<uint8_t> ret;
	ret.reserve((_s.size() - s + 1) / 2);

	if (_s.size() % 2)
	{
		int h = fromHex(_s[s++], _throw);
		if (h != -1)
			ret.push_back(static_cast<uint8_t>(h));
		else
			return bytes();
	}
	for (unsigned i = s; i < _s.size(); i += 2)
	{
		int h = fromHex(_s[i], _throw);
		int l = fromHex(_s[i + 1], _throw);
		if (h != -1 && l != -1)
			ret.push_back(static_cast<uint8_t>(h * 16 + l));
		else
			return bytes();
	}
	return ret;
}

bool hyperion::util::passesAddressChecksum(std::string const& _str, bool _strict)
{
	if (_str.length() != AddressBytes * 2 + 1 || !boost::starts_with(_str, "Q"))
		return false;

	std::string s = _str.substr(1);
	if (!isAddressHex(s))
		return false;

	std::string checksummed = checksummedAddressBody(s);
	if (_strict)
		return s == checksummed;

	return isUniformHexCase(s) || s == checksummed;
}

std::string hyperion::util::getChecksummedAddress(std::string const& _addr)
{
	assertThrow(_addr.length() == AddressBytes * 2 + 1, InvalidAddress, _addr);
	assertThrow(boost::starts_with(_addr, "Q"), InvalidAddress, "");
	std::string s = _addr.substr(1);
	assertThrow(isAddressHex(s), InvalidAddress, "");

	return "Q" + checksummedAddressBody(s);
}

bool hyperion::util::isValidHex(std::string const& _string)
{
	if (_string.substr(0, 2) != "0x")
		return false;
	if (_string.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos)
		return false;
	return true;
}

bool hyperion::util::isValidDecimal(std::string const& _string)
{
	if (_string.empty())
		return false;
	if (_string == "0")
		return true;
	// No leading zeros
	if (_string.front() == '0')
		return false;
	if (_string.find_first_not_of("0123456789") != std::string::npos)
		return false;
	return true;
}

std::string hyperion::util::formatAsStringOrNumber(std::string const& _value)
{
	assertThrow(_value.length() <= 64, StringTooLong, "String to be formatted longer than 64 bytes.");

	for (auto const& c: _value)
		if (c <= 0x1f || c >= 0x7f || c == '"')
		{
			// Left-align value in hex, padded to word size
			std::string hex;
			for (char ch: _value)
			{
				uint8_t b = static_cast<uint8_t>(ch);
				hex += "0123456789abcdef"[b >> 4];
				hex += "0123456789abcdef"[b & 0xf];
			}
			hex.resize(128, '0'); // pad to 64 bytes = 128 hex chars
			return "0x" + hex;
		}

	return escapeAndQuoteString(_value);
}


std::string hyperion::util::escapeAndQuoteString(std::string const& _input)
{
	std::string out;

	for (char c: _input)
		if (c == '\\')
			out += "\\\\";
		else if (c == '"')
			out += "\\\"";
		else if (c == '\n')
			out += "\\n";
		else if (c == '\r')
			out += "\\r";
		else if (c == '\t')
			out += "\\t";
		else if (!isPrint(c))
		{
			std::ostringstream o;
			o << "\\x" << std::hex << std::setfill('0') << std::setw(2) << (unsigned)(unsigned char)(c);
			out += o.str();
		}
		else
			out += c;

	return "\"" + std::move(out) + "\"";
}
