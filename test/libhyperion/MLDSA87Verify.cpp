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

#include <libhyputil/CommonData.h>

#include <mldsa_native.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace hyperion::util;
using namespace hyperion::test;

namespace hyperion::frontend::test
{
namespace
{

// Deliberately substitute return data only in decoder tests. The normal host always
// verifies real signatures, including when running semantic tests via ihyptest.
class MLDSA87ReturnDataHost: public QRVMHost
{
public:
	using QRVMHost::QRVMHost;
	bytes output;
	qrvmc_status_code status = QRVMC_SUCCESS;
	size_t calls = 0;

	/// Answers a call to the mldsa87verify precompile with @a output and @a status, counting it in
	/// @a calls; everything else goes to the real host.
	qrvmc::Result call(qrvmc_message const& _message) noexcept override
	{
		if (_message.recipient == qrvmc::address{3})
		{
			++calls;
			return qrvmc::Result(status, status == QRVMC_SUCCESS ? _message.gas : 0, 0, output.data(), output.size());
		}
		return QRVMHost::call(_message);
	}
};

}

BOOST_FIXTURE_TEST_SUITE(MLDSA87Verify, HyperionExecutionFramework)

BOOST_AUTO_TEST_CASE(return_data_decoding)
{
	auto host = std::make_unique<MLDSA87ReturnDataHost>(m_qrvmVersion, QRVMHost::getVM(m_vmPaths.front().string()));
	auto& mock = *host;
	m_qrvmcHost = std::move(host);
	for (bool viaIR: {false, true})
		for (bool optimize: {false, true})
		{
			m_compileViaYul = viaIR;
			m_optimiserSettings = optimize ? OptimiserSettings::full() : OptimiserSettings::minimal();
			reset();
			compileAndRun(R"(
				contract C {
					function verify() public pure returns (bool) {
						return mldsa87verify(bytes64(0), new bytes(4627), new bytes(2592), "");
					}
				}
			)");
			bytes successWord(64, 0);
			successWord.back() = 1;
			bytes noncanonicalWord(64, 0);
			noncanonicalWord.back() = 2;
			bytes oversizedTrue = successWord;
			oversizedTrue.push_back(0);
			for (auto const& output: std::vector<bytes>{successWord, {}, bytes(64, 0), noncanonicalWord, {1}, oversizedTrue})
			{
				mock.output = output;
				BOOST_CHECK(callContractFunction("verify()") == encode(output == successWord));
				BOOST_CHECK(m_transactionSuccessful);
			}
			mock.status = QRVMC_OUT_OF_GAS;
			mock.output.clear();
			callContractFunction("verify()");
			BOOST_CHECK(!m_transactionSuccessful);
			mock.status = QRVMC_SUCCESS;
		}
}

BOOST_AUTO_TEST_CASE(malformed_frame_not_sent)
{
	// Arguments of the wrong length yield false without a call, since the precompile would
	// charge its full gas for a frame it cannot parse.
	auto host = std::make_unique<MLDSA87ReturnDataHost>(m_qrvmVersion, QRVMHost::getVM(m_vmPaths.front().string()));
	auto& mock = *host;
	m_qrvmcHost = std::move(host);
	mock.output = bytes(64, 0);
	mock.output.back() = 1;
	for (bool viaIR: {false, true})
		for (bool optimize: {false, true})
		{
			m_compileViaYul = viaIR;
			m_optimiserSettings = optimize ? OptimiserSettings::full() : OptimiserSettings::minimal();
			reset();
			compileAndRun(R"(
				contract C {
					function verify(uint signatureLength, uint publicKeyLength, uint contextLength) public pure returns (bool) {
						return mldsa87verify(bytes64(0), new bytes(signatureLength), new bytes(publicKeyLength), new bytes(contextLength));
					}
				}
			)");
			mock.calls = 0;
			BOOST_CHECK(callContractFunction("verify(uint512,uint512,uint512)", u256(4627), u256(2592), u256(255)) == encode(true));
			BOOST_CHECK_EQUAL(mock.calls, 1);
			for (auto const& [signatureLength, publicKeyLength, contextLength]: std::vector<std::tuple<u256, u256, u256>>{
				{4626, 2592, 0}, {4628, 2592, 0}, {4627, 2591, 0}, {4627, 2593, 0}, {4627, 2592, 256}, {0, 0, 0}
			})
			{
				mock.calls = 0;
				BOOST_CHECK(callContractFunction("verify(uint512,uint512,uint512)", signatureLength, publicKeyLength, contextLength) == encode(false));
				BOOST_CHECK(m_transactionSuccessful);
				BOOST_CHECK_EQUAL(mock.calls, 0);
			}
		}
}

BOOST_AUTO_TEST_CASE(malformed_frames_and_gas)
{
	qrvmc_message message{};
	message.recipient = qrvmc::address{3};
	message.gas = 125001;
	for (size_t length: {size_t(0), size_t(64), size_t(7283), size_t(7284), size_t(7285), size_t(7540)})
	{
		bytes input(length, 0);
		message.input_data = input.data();
		message.input_size = input.size();
		auto result = m_qrvmcHost->call(message);
		BOOST_CHECK_EQUAL(result.status_code, QRVMC_SUCCESS);
		BOOST_CHECK_EQUAL(result.output_size, 0);
		BOOST_CHECK_EQUAL(result.gas_left, 1);
	}
	message.input_data = nullptr;
	message.input_size = 0;
	message.gas = 124999;
	auto result = m_qrvmcHost->call(message);
	BOOST_CHECK_EQUAL(result.status_code, QRVMC_OUT_OF_GAS);
	BOOST_CHECK_EQUAL(result.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(rejects_former_synthetic_fixture)
{
	bytes const context = asBytes("QNS-SIGN-v1");
	bytes input(7284 + context.size(), 0);
	input[0] = 0x42;
	for (size_t i = 0; i < 2592; ++i)
		input[64 + i] = static_cast<uint8_t>((i % 251) ^ 0x44);
	for (size_t i = 0; i < 4627; ++i)
		input[64 + 2592 + i] = static_cast<uint8_t>((i % 241) ^ 0x43);
	input[7283] = static_cast<uint8_t>(context.size());
	std::copy(context.begin(), context.end(), input.begin() + 7284);
	qrvmc_message message{};
	message.recipient = qrvmc::address{3};
	message.gas = 125000;
	message.input_data = input.data();
	message.input_size = input.size();
	auto result = m_qrvmcHost->call(message);
	BOOST_CHECK_EQUAL(result.status_code, QRVMC_SUCCESS);
	BOOST_CHECK_EQUAL(result.output_size, 0);
}

BOOST_AUTO_TEST_CASE(rejects_weak_key_forgeries)
{
	// go-qrl's forgeZeroHintSignature construction: z = h = 0 and
	// c = SHAKE256(SHAKE256(SHAKE256(pk, 64) || 0 || ctxlen || ctx || digest, 64)
	//             || w1Encode(0), 64). Digest is 00..3f and context is "QRL".
	struct Vector { uint8_t rho; unsigned t1; char const* challenge; };
	Vector const vectors[] = {
		{0, 0, "f021f59d1d878654c5c5a54814eb3370adedc1182f1e753c068b2054a8061b9bc2963d35129b86f138e863465d3e3bf0fcc89b9ed560a6280e2cf8cc8ec5e351"},
		{171, 0, "d1b6ef2c49c097430c0a7c90637967a6f4ba3c23e192ba154e6253c3fc7ff0d82b2b1adb1f4961d7363806bfe607eb25fbf4e459ca77ab0e4e053ee23ec6f0f7"},
		{171, 1023, "05d80035cbaddef7266722e2486f8cf2868615925f4fa0a18a1119f1fcd19d5586b45e552e41603be666b25b950b8b606bb98d88d1182d9c52d82f468a620d83"},
		{171, 512, "db074f2e4631247e9f9091dc016109da5cf422b2de69c4654e1c36535ed04caf08e7360327f82f0fc7d782d3cedf0cf19a10b0425d9aca23320caeafb49bffca"},
	};
	for (auto const& vector: vectors)
	{
		bytes input(7284 + 3, 0);
		for (size_t i = 0; i < 64; ++i)
			input[i] = static_cast<uint8_t>(i);
		std::fill(input.begin() + 64, input.begin() + 96, vector.rho);
		uint64_t packed = 0;
		for (unsigned j = 0; j < 4; ++j)
			packed |= uint64_t(vector.t1) << (10 * j);
		for (size_t i = 96; i < 2656; i += 5)
			for (unsigned j = 0; j < 5; ++j)
				input[i + j] = static_cast<uint8_t>(packed >> (8 * j));
		bytes const challenge = fromHex(vector.challenge);
		std::copy(challenge.begin(), challenge.end(), input.begin() + 2656);
		// z = 0 packs two coefficients as 00 00 08 00 80.
		for (size_t i = 2656 + 64; i < 2656 + 64 + 7 * 640; i += 5)
		{
			input[i + 2] = 0x08;
			input[i + 4] = 0x80;
		}
		input[7283] = 3;
		input[7284] = 'Q';
		input[7285] = 'R';
		input[7286] = 'L';
		// Prove the fixture reaches the key-policy distinction: FIPS 204 accepts it.
		BOOST_REQUIRE_EQUAL(hyperion_mldsa87_verify(input.data() + 2656, input.data(), 64,
			input.data() + 7284, 3, input.data() + 64), 0);
		qrvmc_message message{};
		message.recipient = qrvmc::address{3};
		message.gas = 125000;
		message.input_data = input.data();
		message.input_size = input.size();
		auto result = m_qrvmcHost->call(message);
		BOOST_CHECK_EQUAL(result.status_code, QRVMC_SUCCESS);
		BOOST_CHECK_EQUAL(result.output_size, 0);
		BOOST_CHECK_EQUAL(result.gas_left, 0);
	}
}

BOOST_AUTO_TEST_SUITE_END()

}
