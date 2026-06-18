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
 * Unit tests for address canonicalisation.
 */

#include <libhyputil/CommonData.h>
#include <libhyputil/Exceptions.h>
#include <libhyputil/VMConstants.h>

#include <test/Common.h>

#include <boost/test/unit_test.hpp>


namespace hyperion::util::test
{

BOOST_AUTO_TEST_SUITE(AddressCanonicalisation)

namespace
{

std::string address(std::string const& _prefix, char _padding = '0')
{
	return "Q" + _prefix + std::string(AddressBytes * 2 - _prefix.size(), _padding);
}

}

BOOST_AUTO_TEST_CASE(calculate)
{
	BOOST_CHECK(!getChecksummedAddress(address("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed")).empty());
	BOOST_CHECK(!getChecksummedAddress(address("0123456789abcdefabcdef0123456789abcdefab")).empty());
	// no prefix
	BOOST_CHECK_THROW(getChecksummedAddress(address("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed").substr(1)), InvalidAddress);
	// too short
	BOOST_CHECK_THROW(getChecksummedAddress("Q" + std::string(AddressBytes * 2 - 1, '0')), InvalidAddress);
	// too long
	BOOST_CHECK_THROW(getChecksummedAddress("Q" + std::string(AddressBytes * 2 + 1, '0')), InvalidAddress);
	// non-hex character
	BOOST_CHECK_THROW(getChecksummedAddress(address("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed").substr(0, AddressBytes * 2) + "K"), InvalidAddress);
}

BOOST_AUTO_TEST_CASE(canonical_roundtrip)
{
	std::string addr = address("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed");
	std::string canonical = getChecksummedAddress(addr);
	BOOST_CHECK(passesAddressChecksum(canonical, true));

	std::string addr2 = address("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 'b');
	std::string canonical2 = getChecksummedAddress(addr2);
	BOOST_CHECK(passesAddressChecksum(canonical2, true));
}

BOOST_AUTO_TEST_CASE(all_lowercase_valid)
{
	std::string lower = address("de709f2102306220921060314715629080e2fb77", 'a');
	BOOST_CHECK(passesAddressChecksum(lower, false));
}

BOOST_AUTO_TEST_CASE(all_uppercase_valid)
{
	std::string upper = address("52908400098527886E0F7030069857D2E4169EE7", 'A');
	BOOST_CHECK(passesAddressChecksum(upper, false));
}

BOOST_AUTO_TEST_CASE(invalid_length)
{
	BOOST_CHECK(!passesAddressChecksum("Q" + std::string(AddressBytes * 2 - 1, '0'), true));
	BOOST_CHECK(!passesAddressChecksum("Q" + std::string(AddressBytes * 2 + 1, '0'), true));
	BOOST_CHECK(!passesAddressChecksum("", true));
	BOOST_CHECK(!passesAddressChecksum("Q", true));
}

BOOST_AUTO_TEST_SUITE_END()

}
