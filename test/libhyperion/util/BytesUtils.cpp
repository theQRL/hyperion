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

#include <test/libhyperion/util/BytesUtils.h>
#include <test/libhyperion/util/ContractABIUtils.h>
#include <test/libhyperion/util/HyptestErrors.h>

#include <libhyputil/CommonData.h>
#include <libhyputil/CommonIO.h>
#include <libhyputil/StringUtils.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <iterator>
#include <iomanip>
#include <memory>
#include <regex>
#include <stdexcept>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;

bytes BytesUtils::alignLeft(bytes _bytes)
{
	hyptestAssert(_bytes.size() <= 32, "");
	size_t size = _bytes.size();
	return std::move(_bytes) + bytes(32 - size, 0);
}

bytes BytesUtils::alignRight(bytes _bytes)
{
	hyptestAssert(_bytes.size() <= 32, "");
	return bytes(32 - _bytes.size(), 0) + std::move(_bytes);
}

bytes BytesUtils::applyAlign(
	Parameter::Alignment _alignment,
	ABIType& _abiType,
	bytes _bytes
)
{
	if (_alignment != Parameter::Alignment::None)
		_abiType.alignDeclared = true;

	switch (_alignment)
	{
	case Parameter::Alignment::Left:
		_abiType.align = ABIType::AlignLeft;
		return alignLeft(std::move(_bytes));
	case Parameter::Alignment::Right:
	default:
		_abiType.align = ABIType::AlignRight;
		return alignRight(std::move(_bytes));
	}
}

bytes BytesUtils::convertBoolean(std::string const& _literal)
{
	if (_literal == "true")
		return bytes{true};
	else if (_literal == "false")
		return bytes{false};
	else
		BOOST_THROW_EXCEPTION(TestParserError("Boolean literal invalid."));
}

bytes BytesUtils::convertNumber(std::string const& _literal)
{
	try
	{
		return toCompactBigEndian(u256{_literal});
	}
	catch (std::exception const&)
	{
		BOOST_THROW_EXCEPTION(TestParserError("Number encoding invalid."));
	}
}

bytes BytesUtils::convertFixedPoint(std::string const& _literal, size_t& o_fractionalDigits)
{
	size_t dotPos = _literal.find('.');
	o_fractionalDigits = dotPos < _literal.size() ? _literal.size() - dotPos : 0;
	bool negative = !_literal.empty() && _literal.at(0) == '-';
	// remove decimal point
	std::string valueInteger = _literal.substr(0, dotPos) + _literal.substr(dotPos + 1);
	// erase leading zeros to avoid parsing as octal.
	while (!valueInteger.empty() && (valueInteger.at(0) == '0' || valueInteger.at(0) == '-'))
		valueInteger.erase(valueInteger.begin());
	if (valueInteger.empty())
		valueInteger = "0";
	try
	{
		u256 value(valueInteger);
		if (negative)
			value = s2u(-u2s(value));
		return toBigEndian(value);
	}
	catch (std::exception const&)
	{
		BOOST_THROW_EXCEPTION(TestParserError("Number encoding invalid."));
	}
}

bytes BytesUtils::convertHexNumber(std::string const& _literal)
{
	try
	{
		return fromHex(_literal);
	}
	catch (std::exception const&)
	{
		BOOST_THROW_EXCEPTION(TestParserError("Hex number encoding invalid."));
	}
}

bytes BytesUtils::convertString(std::string const& _literal)
{
	try
	{
		return asBytes(_literal);
	}
	catch (std::exception const&)
	{
		BOOST_THROW_EXCEPTION(TestParserError("String encoding invalid."));
	}
}

std::string BytesUtils::formatUnsigned(bytes const& _bytes)
{
	std::stringstream os;

	hyptestAssert(!_bytes.empty() && _bytes.size() <= 32, "");

	return fromBigEndian<u256>(_bytes).str();
}

std::string BytesUtils::formatSigned(bytes const& _bytes)
{
	std::stringstream os;

	hyptestAssert(!_bytes.empty() && _bytes.size() <= 32, "");

	if (*_bytes.begin() & 0x80)
		os << u2s(fromBigEndian<u256>(_bytes));
	else
		os << fromBigEndian<u256>(_bytes);

	return os.str();
}

std::string BytesUtils::formatBoolean(bytes const& _bytes)
{
	std::stringstream os;
	u256 result = fromBigEndian<u256>(_bytes);

	if (result == 0)
		os << "false";
	else if (result == 1)
		os << "true";
	else
		os << result;

	return os.str();
}

std::string BytesUtils::formatHex(bytes const& _bytes, bool _shorten)
{
	hyptestAssert(!_bytes.empty() && _bytes.size() <= 32, "");
	u256 value = fromBigEndian<u256>(_bytes);
	std::string output = toCompactHexWithPrefix(value);

	if (_shorten)
		return output.substr(0, output.size() - countRightPaddedZeros(_bytes) * 2);
	return output;
}

std::string BytesUtils::formatHexString(bytes const& _bytes)
{
	std::stringstream os;

	os << "hex\"" << util::toHex(_bytes) << "\"";

	return os.str();
}

std::string BytesUtils::formatString(bytes const& _bytes, size_t _cutOff)
{
	std::stringstream os;

	os << "\"";
	for (size_t i = 0; i < std::min(_cutOff, _bytes.size()); ++i)
	{
		auto const v = _bytes[i];
		switch (v)
		{
			case '\0':
				os << "\\0";
				break;
			case '\n':
				os << "\\n";
				break;
			default:
				if (isPrint(static_cast<char>(v)))
					os << v;
				else
					os << "\\x" << util::toHex(v, HexCase::Lower);
		}
	}
	os << "\"";

	return os.str();
}

std::string BytesUtils::formatFixedPoint(bytes const& _bytes, bool _signed, size_t _fractionalDigits)
{
	std::string decimal;
	bool negative = false;
	if (_signed)
	{
		s256 signedValue{u2s(fromBigEndian<u256>(_bytes))};
		negative = (signedValue < 0);
		decimal = signedValue.str();
	}
	else
		decimal = fromBigEndian<u256>(_bytes).str();
	if (_fractionalDigits > 0)
	{
		size_t numDigits = decimal.length() - (negative ? 1 : 0);
		if (_fractionalDigits >= numDigits)
			decimal.insert(negative ? 1 : 0, std::string(_fractionalDigits + 1 - numDigits, '0'));
		decimal.insert(decimal.length() - _fractionalDigits, ".");
	}
	return decimal;
}

std::string BytesUtils::formatRawBytes(
	bytes const& _bytes,
	hyperion::frontend::test::ParameterList const& _parameters,
	std::string _linePrefix
)
{
	std::stringstream os;
	ParameterList parameters;
	auto it = _bytes.begin();

	if (_bytes.size() != ContractABIUtils::encodingSize(_parameters))
	{
		// Interpret all full 32-byte values as integers.
		parameters = ContractABIUtils::defaultParameters(_bytes.size() / 32);

		// We'd introduce trailing zero bytes if we interpreted the final bit as an integer.
		// We want a right-aligned sequence of bytes instead.
		if (_bytes.size() % 32 != 0)
			parameters.push_back({
				bytes(),
				"",
				ABIType{ABIType::HexString, ABIType::AlignRight, _bytes.size() % 32},
				FormatInfo{},
			});
	}
	else
		parameters = _parameters;
	hyptestAssert(ContractABIUtils::encodingSize(parameters) >= _bytes.size());

	for (auto const& parameter: parameters)
	{
		long actualSize = std::min(
			distance(it, _bytes.end()),
			static_cast<ParameterList::difference_type>(parameter.abiType.size)
		);
		bytes byteRange(parameter.abiType.size, 0);
		copy(it, it + actualSize, byteRange.begin());

		os << _linePrefix << byteRange;
		if (&parameter != &parameters.back())
			os << std::endl;

		it += actualSize;
	}

	return os.str();
}

std::string BytesUtils::formatBytes(
	bytes const& _bytes,
	ABIType const& _abiType
)
{
	std::stringstream os;

	switch (_abiType.type)
	{
	case ABIType::UnsignedDec:
		// Check if the detected type was wrong and if this could
		// be signed. If an unsigned was detected in the expectations,
		// but the actual result returned a signed, it would be formatted
		// incorrectly.
		if (*_bytes.begin() & 0x80)
			os << formatSigned(_bytes);
		else
		{
			std::string decimal(formatUnsigned(_bytes));
			std::string hexadecimal(formatHex(_bytes));
			unsigned int value = u256(_bytes).convert_to<unsigned int>();
			if (value < 0x10)
				os << decimal;
			else if (value >= 0x10 && value <= 0xff) {
				os << hexadecimal;
			}
			else
			{
				auto entropy = [](std::string const& str) -> double {
					double result = 0;
					std::map<char, double> frequencies;
					for (char c: str)
						frequencies[c]++;
					for (auto p: frequencies)
					{
						double freq = p.second / double(str.length());
						result -= freq * (log(freq) / log(2.0));
					}
					return result;
				};
				if (entropy(decimal) < entropy(hexadecimal.substr(2, hexadecimal.length())))
					os << decimal;
				else
					os << hexadecimal;
			}
		}
		break;
	case ABIType::SignedDec:
		os << formatSigned(_bytes);
		break;
	case ABIType::Boolean:
		os << formatBoolean(_bytes);
		break;
	case ABIType::Hex:
		os << formatHex(_bytes, _abiType.alignDeclared);
		break;
	case ABIType::Address:
		os << boost::replace_all_copy(formatHex(_bytes, _abiType.alignDeclared), "0x", "Z");
		break;	
	case ABIType::HexString:
		os << formatHexString(_bytes);
		break;
	case ABIType::String:
		os << formatString(_bytes, _bytes.size() - countRightPaddedZeros(_bytes));
		break;
	case ABIType::UnsignedFixedPoint:
	case ABIType::SignedFixedPoint:
		os << formatFixedPoint(_bytes, _abiType.type == ABIType::SignedFixedPoint, _abiType.fractionalDigits);
		break;
	case ABIType::Failure:
	case ABIType::None:
		break;
	}

	if (_abiType.alignDeclared)
		return (_abiType.align == ABIType::AlignLeft ? "left(" : "right(") + os.str() + ")";
	return os.str();
}

std::string BytesUtils::formatBytesRange(
	bytes _bytes,
	hyperion::frontend::test::ParameterList const& _parameters,
	bool _highlight
)
{
	std::stringstream os;
	ParameterList parameters;
	auto it = _bytes.begin();

	if (_bytes.size() != ContractABIUtils::encodingSize(_parameters))
	{
		// Interpret all full 32-byte values as integers.
		parameters = ContractABIUtils::defaultParameters(_bytes.size() / 32);

		// We'd introduce trailing zero bytes if we interpreted the final bit as an integer.
		// We want a right-aligned sequence of bytes instead.
		if (_bytes.size() % 32 != 0)
			parameters.push_back({
				bytes(),
				"",
				ABIType{ABIType::HexString, ABIType::AlignRight, _bytes.size() % 32},
				FormatInfo{},
			});
	}
	else
		parameters = _parameters;
	hyptestAssert(ContractABIUtils::encodingSize(parameters) >= _bytes.size());

	for (auto const& parameter: parameters)
	{
		long actualSize = std::min(
			distance(it, _bytes.end()),
			static_cast<ParameterList::difference_type>(parameter.abiType.size)
		);
		bytes byteRange(parameter.abiType.size, 0);
		copy(it, it + actualSize, byteRange.begin());

		if (!parameter.matchesBytes(byteRange))
			AnsiColorized(
				os,
				_highlight,
				{util::formatting::RED_BACKGROUND}
			) << formatBytes(byteRange, parameter.abiType);
		else
			os << parameter.rawString;

		if (&parameter != &parameters.back())
			os << ", ";

		it += actualSize;
	}

	return os.str();
}

size_t BytesUtils::countRightPaddedZeros(bytes const& _bytes)
{
	return static_cast<size_t>(find_if(
		_bytes.rbegin(),
		_bytes.rend(),
		[](uint8_t b) { return b != '\0'; }
	) - _bytes.rbegin());
}

