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

#include <boost/test/unit_test.hpp>

#include <memory>
#include <vector>

using namespace hyperion::util;
using namespace hyperion::test;

namespace hyperion::frontend::test
{
namespace
{

// Substitutes the return data of the xmssverify precompile, so that the decoder generated for
// the builtin can be checked against every shape of output.
class XMSSReturnDataHost: public QRVMHost
{
public:
	using QRVMHost::QRVMHost;
	bytes output;
	qrvmc_status_code status = QRVMC_SUCCESS;

	qrvmc::Result call(qrvmc_message const& _message) noexcept override
	{
		if (_message.recipient == qrvmc::address{7})
			return qrvmc::Result(status, status == QRVMC_SUCCESS ? _message.gas : 0, 0, output.data(), output.size());
		return QRVMHost::call(_message);
	}
};

}

BOOST_FIXTURE_TEST_SUITE(XMSSVerify, HyperionExecutionFramework)

BOOST_AUTO_TEST_CASE(return_data_decoding)
{
	auto host = std::make_unique<XMSSReturnDataHost>(m_qrvmVersion, QRVMHost::getVM(m_vmPaths.front().string()));
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
						return xmssverify("", new bytes(2308), new bytes(67));
					}
				}
			)");
			bytes successWord(64, 0);
			successWord.back() = 1;
			bytes noncanonicalWord(64, 0);
			noncanonicalWord.back() = 2;
			bytes oversizedTrue = successWord;
			oversizedTrue.push_back(0);
			// Only exactly one word holding 1 is true, as for mldsa87verify.
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

BOOST_AUTO_TEST_SUITE_END()

}
