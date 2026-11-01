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

#include <test/ExecutionFramework.h>

namespace hyperion::test
{

BOOST_AUTO_TEST_SUITE(ExecutionFrameworkTest)

BOOST_AUTO_TEST_CASE(unsigned_integer_encoding)
{
	u256 const highBit = u256(1) << 255;
	std::vector<std::pair<u256, bytes>> const cases{
		{0, bytes(64, 0)},
		{1, bytes(63, 0) + bytes{1}},
		{highBit - 1, bytes(32, 0) + bytes{0x7f} + bytes(31, 0xff)},
		{highBit, bytes(32, 0) + bytes{0x80} + bytes(31, 0)},
		{highBit + 1, bytes(32, 0) + bytes{0x80} + bytes(30, 0) + bytes{1}},
		{u256(-1), bytes(32, 0) + bytes(32, 0xff)}
	};
	for (auto const& [value, expected]: cases)
	{
		bytes const actual = ExecutionFramework::encode(value);
		BOOST_CHECK_EQUAL_COLLECTIONS(
			actual.begin(),
			actual.end(),
			expected.begin(),
			expected.end()
		);
	}
}

BOOST_AUTO_TEST_CASE(signed_integer_encoding)
{
	s256 const highBit = s256(1) << 255;
	std::vector<std::pair<s256, bytes>> const cases{
		{0, bytes(64, 0)},
		{1, bytes(63, 0) + bytes{1}},
		{-1, bytes(64, 0xff)},
		{-257, bytes(62, 0xff) + bytes{0xfe, 0xff}},
		{-highBit, bytes(32, 0xff) + bytes{0x80} + bytes(31, 0)},
		{highBit - 1, bytes(32, 0) + bytes{0x7f} + bytes(31, 0xff)}
	};
	for (auto const& [value, expected]: cases)
	{
		bytes const actual = ExecutionFramework::encode(value);
		BOOST_CHECK_EQUAL_COLLECTIONS(
			actual.begin(),
			actual.end(),
			expected.begin(),
			expected.end()
		);
	}
	BOOST_CHECK(ExecutionFramework::encode(-1) == bytes(64, 0xff));
	BOOST_CHECK(ExecutionFramework::encode(-257) == bytes(62, 0xff) + bytes({0xfe, 0xff}));
}

BOOST_AUTO_TEST_CASE(fixed_point_encoding)
{
	BOOST_CHECK(
		ExecutionFramework::encode(std::make_pair(rational(3, 2), 8)) ==
		bytes(62, 0) + bytes({1, 0x80})
	);
	BOOST_CHECK(
		ExecutionFramework::encode(std::make_pair(rational(-3, 2), 8)) ==
		bytes(62, 0xff) + bytes({0xfe, 0x80})
	);
	BOOST_CHECK(
		ExecutionFramework::encode(std::make_pair(rational(bigint(1) << 255), 0)) ==
		bytes(32, 0) + bytes{0x80} + bytes(31, 0)
	);
}

BOOST_AUTO_TEST_SUITE_END()

}
