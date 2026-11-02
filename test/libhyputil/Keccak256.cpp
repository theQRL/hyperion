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
 * Unit tests for keccak256.
 */
#include <libhyputil/Keccak256.h>
#include <libhyputil/Keccak512.h>

#include <boost/test/unit_test.hpp>


namespace hyperion::util::test
{

BOOST_AUTO_TEST_SUITE(Keccak256, *boost::unit_test::label("nooptions"))

BOOST_AUTO_TEST_CASE(empty)
{
	BOOST_CHECK_EQUAL(
		keccak256(bytes()),
		FixedHash<32>("0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470")
	);
}

BOOST_AUTO_TEST_CASE(zeros)
{
	BOOST_CHECK_EQUAL(
		keccak256(bytes(1, '\0')),
		FixedHash<32>("0xbc36789e7a1e281436464229828f817d6612f7b477d66591ff96a9e064bcc98a")
	);
	BOOST_CHECK_EQUAL(
		keccak256(bytes(2, '\0')),
		FixedHash<32>("0x54a8c0ab653c15bfb48b47fd011ba2b9617af01cb45cab344acd57c924d56798")
	);
	BOOST_CHECK_EQUAL(
		keccak256(bytes(5, '\0')),
		FixedHash<32>("0xc41589e7559804ea4a2080dad19d876a024ccb05117835447d72ce08c1d020ec")
	);
	BOOST_CHECK_EQUAL(
		keccak256(bytes(10, '\0')),
		FixedHash<32>("0x6bd2dd6bd408cbee33429358bf24fdc64612fbf8b1b4db604518f40ffd34b607")
	);
}

BOOST_AUTO_TEST_CASE(strings)
{
	BOOST_CHECK_EQUAL(
		keccak256("test"),
		FixedHash<32>("0x9c22ff5f21f0b81b113e63f7db6da94fedef11b2119b4088b89664fb9a3cb658")
	);
	BOOST_CHECK_EQUAL(
		keccak256("longer test string"),
		FixedHash<32>("0x47bed17bfbbc08d6b5a0f603eff1b3e932c37c10b865847a7bc73d55b260f32a")
	);
}

BOOST_AUTO_TEST_CASE(shake256_empty_64)
{
	BOOST_CHECK_EQUAL(
		toHex(shake256("", 64)),
		"46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
		"d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be"
	);
}

BOOST_AUTO_TEST_CASE(shake128_vectors)
{
	BOOST_CHECK_EQUAL(
		toHex(shake128("", 32)),
		"7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26"
	);
	BOOST_CHECK_EQUAL(
		toHex(shake128("abc", 32)),
		"5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8"
	);
	BOOST_CHECK_EQUAL(
		toHex(shake128("The quick brown fox jumps over the lazy dog", 32)),
		"f4202e3c5852f9182a0430fd8144f0a74b95e7417ecae17db0f8cfeed0e3e66e"
	);
	BOOST_CHECK_EQUAL(
		toHex(shake256("abc", 32)),
		"483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
	);
	BOOST_CHECK(shake128("", 0).empty());
}

BOOST_AUTO_TEST_CASE(keccak512_vectors)
{
	// Legacy Keccak-512 vectors from go-qrl, including both sides of its 72-byte rate boundary.
	std::vector<std::pair<size_t, h512>> const cases{
		{0, h512(
			"0eab42de4c3ceb9235fc91acffe746b29c29a8c366b7c60e4e67c466f36a4304"
			"c00fa9caf9d87976ba469bcbe06713b435f091ef2769fb160cdab33d3670680e"
		)},
		{3, h512(
			"ac45c5e002ec6a0f9240cd2a936efa52825aaeb38bd1ef8499637d038770273f"
			"ed7b43521bc3ec96155048ba069fc58af4b07b4cc51fd748807c6ec4567abae3"
		)},
		{71, h512(
			"a57dce7da8ec781665705f3d69310beaaa5b0cae0c9c34c9b1c5b7238bbd2ce3"
			"85bbe2f37694d2b8e9a55eb889eecb80d74ff4f9086067b47fd3f43c16c0b506"
		)},
		{72, h512(
			"4cb1cecbc96415025c7a9d6fb89f82a8482773fd9664c378691a05323ff4700f"
			"a3e60414e6064814f98b36a61a87f62dffa7c56a2371355868dd37b8a654cf50"
		)},
		{73, h512(
			"2bc203eacdc544533b7e9da9c12f502e2e585716b1fa9cf8063df348f4bf20ee"
			"b3deca3383db498dd3055fbe02dda8176087f2deb439774680822dabb26a127f"
		)},
		{145, h512(
			"6bd628854b42ecc4fe84e937ae706ca311158d69e3fb00345c41d8ba266e61ac"
			"6e026004f01064e2eb0f30a38c4d90c5bd85a6f580c2bbf10dcd61715d38fda6"
		)},
	};
	for (auto const& [length, expected]: cases)
		BOOST_TEST_CONTEXT("input length " << length)
			BOOST_CHECK_EQUAL(keccak512(bytes(length, 'a')), expected);
}

BOOST_AUTO_TEST_SUITE_END()

}
