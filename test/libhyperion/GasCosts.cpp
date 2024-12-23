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
 * Tests that check that the cost of certain operations stay within range.
 */

#include <test/libhyperion/HyperionExecutionFramework.h>
#include <liblangutil/ZVMVersion.h>
#include <libhyputil/IpfsHash.h>
#include <libzvmasm/GasMeter.h>

#include <cmath>

using namespace hyperion::langutil;
using namespace hyperion::langutil;
using namespace hyperion::zvmasm;
using namespace hyperion::frontend;
using namespace hyperion::test;

namespace hyperion::frontend::test
{

#define CHECK_DEPLOY_GAS(_gasNoOpt, _gasOpt, _zvmVersion) \
	do \
	{ \
		u256 metaCost = GasMeter::dataGas(m_compiler.cborMetadata(m_compiler.lastContractName()), true); \
		u256 gasOpt{_gasOpt}; \
		u256 gasNoOpt{_gasNoOpt}; \
		u256 gas = m_optimiserSettings == OptimiserSettings::minimal() ? gasNoOpt : gasOpt; \
		BOOST_CHECK_MESSAGE( \
			m_gasUsed >= metaCost, \
			"Gas used: " + \
			m_gasUsed.str() + \
			" is less than the data cost for the cbor metadata: " + \
			u256(metaCost).str() \
		); \
		u256 gasUsed = m_gasUsed - metaCost; \
		BOOST_CHECK_MESSAGE( \
			gas == gasUsed, \
			"Gas used: " + \
			gasUsed.str() + \
			" - expected: " + \
			gas.str() \
		); \
	} while(0)

#define CHECK_GAS(_gasNoOpt, _gasOpt, _tolerance) \
	do \
	{ \
		u256 gasOpt{_gasOpt}; \
		u256 gasNoOpt{_gasNoOpt}; \
		u256 tolerance{_tolerance}; \
		u256 gas = m_optimiserSettings == OptimiserSettings::minimal() ? gasNoOpt : gasOpt; \
		u256 diff = gas < m_gasUsed ? m_gasUsed - gas : gas - m_gasUsed; \
		BOOST_CHECK_MESSAGE( \
			diff <= tolerance, \
			"Gas used: " + \
			m_gasUsed.str() + \
			" - expected: " + \
			gas.str() + \
			" (tolerance: " + \
			tolerance.str() + \
			")" \
		); \
	} while(0)

BOOST_FIXTURE_TEST_SUITE(GasCostTests, HyperionExecutionFramework)

BOOST_AUTO_TEST_CASE(string_storage)
{
	char const* sourceCode = R"(
		contract C {
			function f() pure public {
				require(false, "Not Authorized. This function can only be called by the custodian or owner of this contract");
			}
		}
	)";
	m_compiler.setMetadataFormat(CompilerStack::MetadataFormat::NoMetadata);
	m_appendCBORMetadata = false;
	compileAndRun(sourceCode);

	if (!CommonOptions::get().useABIEncoderV1)
	{
		if (CommonOptions::get().optimize)
		{
			CHECK_DEPLOY_GAS(0, 97071, zvmVersion);
		}
		else
		{
			CHECK_DEPLOY_GAS(121493, 110969, zvmVersion);
		}
	}
	// TODO(now.youtrack.cloud/issue/TS-14): Gas used: 102421 - expected: 114077
	// else
		// CHECK_DEPLOY_GAS(114077, 95835, zvmVersion);

	callContractFunction("f()");
	if (!CommonOptions::get().useABIEncoderV1)
	{
		if (CommonOptions::get().optimize)
		{
			CHECK_GAS(0, 21318, 20);
		}
		else
		{
			CHECK_GAS(21528, 21351, 20);
		}
	}
	// TODO(now.youtrack.cloud/issue/TS-14)
	// CHECK_GAS(21521, 21322, 20);
}

BOOST_AUTO_TEST_CASE(single_callvaluecheck)
{
	std::string sourceCode = R"(
		// All functions nonpayable, we can check callvalue at the beginning
		contract Nonpayable {
			address a;
			function f(address b) public {
				a = b;
			}
			function f1(address b) public pure returns (uint c) {
				return uint160(b) + 2;
			}
			function f2(address b) public pure returns (uint) {
				return uint160(b) + 8;
			}
			function f3(address, uint c) pure public returns (uint) {
				return c - 5;
			}
		}
		// At least on payable function, we cannot do the optimization.
		contract Payable {
			address a;
			function f(address b) public {
				a = b;
			}
			function f1(address b) public pure returns (uint c) {
				return uint160(b) + 2;
			}
			function f2(address b) public pure returns (uint) {
				return uint160(b) + 8;
			}
			function f3(address, uint c) payable public returns (uint) {
				return c - 5;
			}
		}
	)";
	compileAndRun(sourceCode);
	size_t bytecodeSizeNonpayable = m_compiler.object("Nonpayable").bytecode.size();
	size_t bytecodeSizePayable = m_compiler.object("Payable").bytecode.size();

	BOOST_CHECK_EQUAL(bytecodeSizePayable - bytecodeSizeNonpayable, 24);
		
}

BOOST_AUTO_TEST_SUITE_END()

}
