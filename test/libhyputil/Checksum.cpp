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
 * Unit tests for address checksum validation.
 */

#include <libhyputil/CommonData.h>
#include <libhyputil/Exceptions.h>
#include <libhyputil/VMConstants.h>

#include <test/Common.h>

#include <boost/test/unit_test.hpp>


namespace hyperion::util::test
{

BOOST_AUTO_TEST_SUITE(AddressChecksum)

namespace
{

std::string const lowerParity =
	"Qd5812f6cf4a0f645aa620cd57319a0ed649dd8f5519a9dde7770ae5b0e49e547"
	"985f35eb972a2a07041561aa39c65a3991478f9b1e6749e05277dcf58a9a8b72";
std::string const checksummedParity =
	"Qd5812F6Cf4a0f645aa620cd57319a0Ed649dd8f5519A9dde7770ae5b0E49e547"
	"985f35eB972A2a07041561aa39c65A3991478f9B1e6749e05277dcf58A9A8B72";
std::string const invalidMixedParity =
	"QD5812F6Cf4a0f645aa620cd57319a0Ed649dd8f5519A9dde7770ae5b0E49e547"
	"985f35eB972A2a07041561aa39c65A3991478f9B1e6749e05277dcf58A9A8B72";

std::string address(std::string const& _prefix, char _padding = '0')
{
	return "Q" + _prefix + std::string(AddressBytes * 2 - _prefix.size(), _padding);
}

}

BOOST_AUTO_TEST_CASE(calculate)
{
	BOOST_CHECK_EQUAL(getChecksummedAddress(lowerParity), checksummedParity);
	BOOST_CHECK_EQUAL(getChecksummedAddress(checksummedParity), checksummedParity);
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
	std::string addr = lowerParity;
	std::string canonical = getChecksummedAddress(addr);
	BOOST_CHECK(passesAddressChecksum(canonical, true));
	BOOST_CHECK(passesAddressChecksum(canonical, false));
}

BOOST_AUTO_TEST_CASE(uniform_case_valid_for_permissive_check)
{
	std::string lower = lowerParity;
	std::string upper = address("52908400098527886E0F7030069857D2E4169EE7", 'A');
	BOOST_CHECK(passesAddressChecksum(lower, false));
	BOOST_CHECK(passesAddressChecksum(upper, false));
}

BOOST_AUTO_TEST_CASE(strict_check_requires_canonical_checksum)
{
	BOOST_CHECK(!passesAddressChecksum(lowerParity, true));
	BOOST_CHECK(passesAddressChecksum(checksummedParity, true));
	BOOST_CHECK(!passesAddressChecksum(invalidMixedParity, false));
	BOOST_CHECK(!passesAddressChecksum(invalidMixedParity, true));
	BOOST_CHECK(passesAddressChecksum("Q" + std::string(AddressBytes * 2, '0'), true));
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
