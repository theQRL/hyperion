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
 * Unit tests for the address checksum.
 */

#include <libhyputil/CommonData.h>
#include <libhyputil/Exceptions.h>

#include <test/Common.h>

#include <boost/test/unit_test.hpp>


namespace hyperion::util::test
{

BOOST_AUTO_TEST_SUITE(Checksum)

BOOST_AUTO_TEST_CASE(calculate)
{
	BOOST_CHECK(!getChecksummedAddress("Z5aaeb6053f3e94c9b9a09f33669435e7ef1beaed").empty());
	BOOST_CHECK(!getChecksummedAddress("Z0123456789abcdefABCDEF0123456789abcdefAB").empty());
	// no prefix
	BOOST_CHECK_THROW(getChecksummedAddress("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed"), InvalidAddress);
	// too short
	BOOST_CHECK_THROW(getChecksummedAddress("Z5aaeb6053f3e94c9b9a09f33669435e7ef1beae"), InvalidAddress);
	// too long
	BOOST_CHECK_THROW(getChecksummedAddress("Z5aaeb6053f3e94c9b9a09f33669435e7ef1beaed1"), InvalidAddress);
	// non-hex character
	BOOST_CHECK_THROW(getChecksummedAddress("Z5aaeb6053f3e94c9b9a09f33669435e7ef1beaeK"), InvalidAddress);

	// the official test suite from EIP-55
	std::vector<std::string> cases {
		// all upper case
		"Z52908400098527886E0F7030069857D2E4169EE7",
		"Z8617E340B3D01FA5F11F306F4090FD50E238070D",
		// all lower case
		"Zde709f2102306220921060314715629080e2fb77",
		"Z27b1fdb04752bbc536007a920d24acb045561c26",
		// regular
		"Z5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed",
		"ZfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359",
		"ZdbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB",
		"ZD1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb"
	};

	for (size_t i = 0; i < cases.size(); i++)
		BOOST_REQUIRE_MESSAGE(getChecksummedAddress(cases[i]) == cases[i], cases[i]);
}

BOOST_AUTO_TEST_CASE(regular)
{
	BOOST_CHECK(passesAddressChecksum("Z5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed", true));
	BOOST_CHECK(passesAddressChecksum("ZfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359", true));
	BOOST_CHECK(passesAddressChecksum("ZdbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB", true));
	BOOST_CHECK(passesAddressChecksum("ZD1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb", true));
}

BOOST_AUTO_TEST_CASE(regular_negative)
{
	BOOST_CHECK(!passesAddressChecksum("Z6aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed", true));
	BOOST_CHECK(!passesAddressChecksum("ZeB6916095ca1df60bB79Ce92cE3Ea74c37c5d359", true));
	BOOST_CHECK(!passesAddressChecksum("ZebF03B407c01E7cD3CBea99509d93f8DDDC8C6FB", true));
	BOOST_CHECK(!passesAddressChecksum("ZE1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb", true));
}

BOOST_AUTO_TEST_CASE(regular_invalid_length)
{
	BOOST_CHECK(passesAddressChecksum("Z9426cbfc57389778d313268E7F85F1CDc2fdad60", true));
	BOOST_CHECK(!passesAddressChecksum("Z9426cbfc57389778d313268E7F85F1CDc2fdad6", true));
	BOOST_CHECK(passesAddressChecksum("Z08A61851FFa4637dE289D630Ae8c5dFb0ff9171F", true));
	BOOST_CHECK(!passesAddressChecksum("Z8A61851FFa4637dE289D630Ae8c5dFb0ff9171F", true));
	BOOST_CHECK(passesAddressChecksum("Z00c40cC30cb4675673c9ee382de805c19734986A", true));
	BOOST_CHECK(!passesAddressChecksum("Zc40cC30cb4675673c9ee382de805c19734986A", true));
	BOOST_CHECK(passesAddressChecksum("ZC40CC30cb4675673C9ee382dE805c19734986a00", true));
	BOOST_CHECK(!passesAddressChecksum("ZC40CC30cb4675673C9ee382dE805c19734986a", true));
}

BOOST_AUTO_TEST_CASE(homocaps_valid)
{
	BOOST_CHECK(passesAddressChecksum("Z52908400098527886E0F7030069857D2E4169EE7", true));
	BOOST_CHECK(passesAddressChecksum("Z8617E340B3D01FA5F11F306F4090FD50E238070D", true));
	BOOST_CHECK(passesAddressChecksum("Zde709f2102306220921060314715629080e2fb77", true));
	BOOST_CHECK(passesAddressChecksum("Z27b1fdb04752bbc536007a920d24acb045561c26", true));
}

BOOST_AUTO_TEST_CASE(homocaps_invalid)
{
	std::string upper = "Z00AA0000000012400000000DDEEFF000000000BB";
	BOOST_CHECK(passesAddressChecksum(upper, false));
	BOOST_CHECK(!passesAddressChecksum(upper, true));
	std::string lower = "Z11aa000000000000000d00cc00000000000000bb";
	BOOST_CHECK(passesAddressChecksum(lower, false));
	BOOST_CHECK(!passesAddressChecksum(lower, true));
}

BOOST_AUTO_TEST_SUITE_END()

}
