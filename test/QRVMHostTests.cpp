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

#include <test/libhyperion/HyperionExecutionFramework.h>
#include <test/Common.h>
#include <test/QRVMHost.h>

#include <libhyputil/CommonIO.h>
#include <libhyputil/JSON.h>

#include <limits>

namespace hyperion::test
{
using util::fromHex;
using util::h512;

// Fixed vectors generated with cyyber/go-qrl crypto.CreateAddress/CreateAddress2 at
// d4f0d2effde840d4a2c59637d353ef2d72a1b6aa. The sender contains bytes 0x01 through 0x40.
BOOST_FIXTURE_TEST_SUITE(QRVMHostTest, frontend::test::HyperionExecutionFramework)

BOOST_AUTO_TEST_CASE(create_address_vectors)
{
	qrvmc_message message{};
	message.kind = QRVMC_CREATE;
	message.depth = 1;
	message.gas = 1000000;
	for (size_t i = 0; i < sizeof(message.sender.bytes); ++i)
		message.sender.bytes[i] = static_cast<uint8_t>(i + 1);
	bytes const initcode{0x00};
	message.input_data = initcode.data();
	message.input_size = initcode.size();
	std::vector<std::pair<uint64_t, h512>> const cases{
		{0ULL, h512(
			"362736a70b3e3ac6e7310f70e3dabefd37561d7b8f9d8961b6eddf754fb78929"
			"27300dcd6b16f3c03302732e9e7846576e346c3f182af3bb16a0d44a3e19897e"
		)},
		{1ULL, h512(
			"254934f7f4e6a6ef0abf358f03bab03ed070836d3073fa692e06ef76193525bd"
			"82af11f683a3f2d22564ddc9649fd693d9f59b59cc3c532471633b3cc9e6d146"
		)},
		{2ULL, h512(
			"05c9f684435c19f25a9a88ba5e6329873fcac473b34e4459f97fab4df373cf7d"
			"0a6865bb00696cf44040c08e1c5dc961ee80d01b8a4de30a05ecbf8710420282"
		)},
		{3ULL, h512(
			"b6b7eb796a0942f16e4d46ac172ce807874b9d51bb7d0aef5c24bf2a78071d3b"
			"2fa52ec1279092c903afff832333ea19176cf80bb9db4fc4047217c3b9817ba5"
		)},
		{127ULL, h512(
			"d9aecabb215bf9b27da32e96f98d81013df68848ece40c2c02665a1dac4bcb39"
			"dc38dcbf1735984c1f4ef2fdd3199d1958a48f76583cc5aa2b1ae0e0a9d479f4"
		)},
		{128ULL, h512(
			"f437aded0f0ed8b7e6b11415666d00b765d32dd0fa3da5c4e4827108915af0f9"
			"655c01453f2c0fec4c637aafb030d09be5f5bee34b1e18a6408d60e48883b82b"
		)},
		{255ULL, h512(
			"19cf39fadbd48a5cfdb7b0e65f057f7b55d725bfb397e177ae5670de727335d9"
			"ad9be2522fef6c69f7501b75016a99fea2c124a2f4976d85f01e607fc1aed20b"
		)},
		{256ULL, h512(
			"3deca8a0dabc39dc14b8f2c888b4b26432399be5720fce44b3626ebdc8f48f07"
			"83888b9f1d2925b724b838638c3741f387f3b8d54135af5de6928f2994ed2bbb"
		)},
		{65535ULL, h512(
			"7cd4a479e2e59acd3772370d9ada07a0c0fbba7d6374f38e8dad90202b8ca767"
			"cf6386a37d8b7b73ea6d88f6036f3882b701a8c9a6749fb6955b02884b96d6de"
		)},
		{65536ULL, h512(
			"f23f410432c7979934b4b0268bd401832916194cd1b0ea74c5ab0f93a26194fa"
			"e0ce655a3949341f832f8e7bd30d8741547b351cee79c1d676039752746c270a"
		)},
		{4294967296ULL, h512(
			"2a8200aa431c5ae269fb5a4517dce44d8142346a53f4dbb1222f015fa160779a"
			"a8d599dd5b536f86bf2fea0eda4af1c3d9de5e2ba1ae97292d1600616478153a"
		)},
		{18446744073709551614ULL, h512(
			"5d659d151a4484129569d25b67502b2b68a6380b70af07d2c9b4c7515ada87cc"
			"b0d6feedab9dc5b9bb997d4aea52010bf0c41c5c70da86baeb0372695946af43"
		)},
	};
	for (auto const& [nonce, expected]: cases)
		BOOST_TEST_CONTEXT("nonce " << nonce)
		{
			m_qrvmcHost->reset();
			m_qrvmcHost->accounts[message.sender].nonce = nonce;
			auto const result = m_qrvmcHost->call(message);
			BOOST_REQUIRE_EQUAL(result.status_code, QRVMC_SUCCESS);
			BOOST_CHECK_EQUAL(QRVMHost::convertFromQRVMC(result.create_address), expected);
			BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, nonce + 1);
			BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(result.create_address).nonce, 1);
		}
}

BOOST_AUTO_TEST_CASE(create2_address_vectors)
{
	struct TestCase
	{
		bytes salt;
		bytes initcode;
		h512 expected;
	};
	std::vector<TestCase> const cases{
		{fromHex(
			"0000000000000000000000000000000000000000000000000000000000000000"
			"0000000000000000000000000000000000000000000000000000000000000000"
		), fromHex(""), h512(
			"5c070f5ad17d7ca187f144b9cfc547e7390f5677d8b0f266afca2c19e77e45f4"
			"30ecc4bce95f1a209fb9031c6bf775fcce21c5a4a3ecc65e0e3f4a23da03746d"
		)},
		{fromHex(
			"8000000000000000000000000000000000000000000000000000000000000012"
			"34000000000000000000000000000000000000000000000000000000000000ff"
		), fromHex("60016000f3"), h512(
			"63b5fe8fc2057b78a9c7444b87dcc8216053eb05a20d2b5ec7c266efc1e047d2"
			"a43ddfc316fde908b197b4258d2e86f1b48dd8270e5064647cd34db69d774bc7"
		)},
		{fromHex(
			"0000000000000000000000000000000000000000000000000000000000000012"
			"34000000000000000000000000000000000000000000000000000000000000ff"
		), fromHex("60016000f3"), h512(
			"2ce467a682b5a899193fd87fd0f064402d930d878fa0a1b0ad2d0c0f47326471"
			"66ac2ea56c0d3171cde56df4caf8d40c5dee1787d703791f313f22736ee23e5b"
		)},
	};
	for (auto const& test: cases)
	{
		m_qrvmcHost->reset();
		qrvmc_message message{};
		message.kind = QRVMC_CREATE2;
		message.depth = 1;
		message.gas = 1000000;
		for (size_t i = 0; i < sizeof(message.sender.bytes); ++i)
		{
			message.sender.bytes[i] = static_cast<uint8_t>(i + 1);
			message.create2_salt.bytes[i] = test.salt[i];
		}
		uint8_t const emptyInput = 0;
		message.input_data = test.initcode.empty() ? &emptyInput : test.initcode.data();
		message.input_size = test.initcode.size();
		auto const result = m_qrvmcHost->call(message);
		BOOST_REQUIRE_EQUAL(result.status_code, QRVMC_SUCCESS);
		BOOST_CHECK_EQUAL(QRVMHost::convertFromQRVMC(result.create_address), test.expected);
		BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 1);
		BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(result.create_address).nonce, 1);
	}
}

BOOST_AUTO_TEST_CASE(creation_nonce_sequence)
{
	qrvmc_message message{};
	message.kind = QRVMC_CREATE2;
	message.depth = 1;
	message.gas = 1000000;
	for (size_t i = 0; i < sizeof(message.sender.bytes); ++i)
		message.sender.bytes[i] = static_cast<uint8_t>(i + 1);
	bytes const initcode{0x60, 0x01, 0x60, 0x00, 0xf3};
	message.input_data = initcode.data();
	message.input_size = initcode.size();
	BOOST_REQUIRE_EQUAL(m_qrvmcHost->call(message).status_code, QRVMC_SUCCESS);
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 1);

	// A collision consumes a nonce, as does a constructor that reverts.
	BOOST_CHECK_EQUAL(m_qrvmcHost->call(message).status_code, QRVMC_OUT_OF_GAS);
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 2);
	bytes const revertingInitcode{0x60, 0x00, 0x60, 0x00, 0xfd};
	message.kind = QRVMC_CREATE;
	message.input_data = revertingInitcode.data();
	message.input_size = revertingInitcode.size();
	BOOST_CHECK_EQUAL(m_qrvmcHost->call(message).status_code, QRVMC_REVERT);
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 3);

	message.input_data = initcode.data();
	message.input_size = initcode.size();
	auto const result = m_qrvmcHost->call(message);
	BOOST_REQUIRE_EQUAL(result.status_code, QRVMC_SUCCESS);
	BOOST_CHECK_EQUAL(QRVMHost::convertFromQRVMC(result.create_address), h512(
		"b6b7eb796a0942f16e4d46ac172ce807874b9d51bb7d0aef5c24bf2a78071d3b"
		"2fa52ec1279092c903afff832333ea19176cf80bb9db4fc4047217c3b9817ba5"
	));
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 4);

	// Rejected attempts do not consume a nonce or wrap the uint64 nonce counter.
	message.value = qrvmc::uint512be{1};
	BOOST_CHECK_EQUAL(m_qrvmcHost->call(message).status_code, QRVMC_INSUFFICIENT_BALANCE);
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, 4);
	message.value = {};
	m_qrvmcHost->accounts[message.sender].nonce = std::numeric_limits<uint64_t>::max();
	BOOST_CHECK_EQUAL(m_qrvmcHost->call(message).status_code, QRVMC_FAILURE);
	BOOST_CHECK_EQUAL(m_qrvmcHost->accounts.at(message.sender).nonce, std::numeric_limits<uint64_t>::max());
}

namespace
{

/// Runs every vector in go-qrl's `core/vm/testdata/precompiles/<_file>` format, copied to
/// `test/precompiles/`, through the precompile at @a _address and checks output and gas.
void checkGoQrlPrecompileVectors(QRVMHost& _host, uint8_t _address, std::string const& _file)
{
	boost::filesystem::path const path = CommonOptions::get().testPath / "precompiles" / _file;
	Json::Value vectors;
	std::string errors;
	BOOST_REQUIRE_MESSAGE(util::jsonParseStrict(util::readFileAsString(path), vectors, &errors), errors);
	BOOST_REQUIRE(vectors.isArray() && !vectors.empty());
	for (auto const& vector: vectors)
	{
		std::string const name = vector["Name"].asString();
		bytes const input = fromHex(vector["Input"].asString());
		qrvmc_message message{};
		message.kind = QRVMC_CALL;
		message.recipient.bytes[sizeof(message.recipient.bytes) - 1] = _address;
		message.code_address = message.recipient;
		message.input_data = input.data();
		message.input_size = input.size();
		message.gas = 100000000;
		qrvmc::Result const result = _host.call(message);
		BOOST_CHECK_MESSAGE(result.status_code == QRVMC_SUCCESS, name + ": call failed");
		BOOST_CHECK_MESSAGE(
			util::toHex(bytes(result.output_data, result.output_data + result.output_size)) == vector["Expected"].asString(),
			name + ": wrong output"
		);
		BOOST_CHECK_MESSAGE(
			message.gas - result.gas_left == vector["Gas"].asInt64(),
			name + ": used " + std::to_string(message.gas - result.gas_left) + " gas instead of " + vector["Gas"].asString()
		);
	}
}

}

BOOST_AUTO_TEST_CASE(go_qrl_depositroot_vectors)
{
	checkGoQrlPrecompileVectors(*m_qrvmcHost, 1, "depositroot.json");
}

BOOST_AUTO_TEST_CASE(go_qrl_modexp_vectors)
{
	checkGoQrlPrecompileVectors(*m_qrvmcHost, 5, "modexp.json");
}

BOOST_AUTO_TEST_SUITE_END()

}
