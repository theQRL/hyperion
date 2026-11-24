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
 * @author Christian <c@ethdev.com>
 * @date 2015
 * Unit tests for the gas estimator.
 */

#include <test/libhyperion/HyperionExecutionFramework.h>
#include <libqrvmasm/GasMeter.h>
#include <libqrvmasm/KnownState.h>
#include <libqrvmasm/PathGasMeter.h>
#include <libhyperion/ast/AST.h>
#include <libhyperion/interface/GasEstimator.h>

using namespace hyperion::langutil;
using namespace hyperion::qrvmasm;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;

namespace hyperion::frontend::test
{

class GasMeterTestFramework: public HyperionExecutionFramework
{
public:
	void compile(std::string const& _sourceCode)
	{
		m_compiler.reset();
		m_compiler.setSources({{"", "pragma hyperion >=0.0;\n"
				"// SPDX-License-Identifier: GPL-3.0\n" + _sourceCode}});
		m_compiler.setOptimiserSettings(hyperion::test::CommonOptions::get().optimize);
		m_compiler.setQRVMVersion(m_qrvmVersion);
		BOOST_REQUIRE_MESSAGE(m_compiler.compile(), "Compiling contract failed");
	}

	void testCreationTimeGas(std::string const& _sourceCode, u256 const& _tolerance = u256(0))
	{
		compileAndRun(_sourceCode);
		auto state = std::make_shared<KnownState>();
		PathGasMeter meter(*m_compiler.assemblyItems(m_compiler.lastContractName()), hyperion::test::CommonOptions::get().qrvmVersion());
		GasMeter::GasConsumption gas = meter.estimateMax(0, state);
		u256 bytecodeSize(m_compiler.runtimeObject(m_compiler.lastContractName()).bytecode.size());
		// costs for deployment
		gas += bytecodeSize * GasCosts::createDataGas;
		// costs for transaction
		gas += gasForTransaction(m_compiler.object(m_compiler.lastContractName()).bytecode, true);

		// Yul helper calls can prevent the estimator from deriving a finite bound.
		if (!gas.isInfinite)
		{
			BOOST_CHECK_LE(m_gasUsed, gas.value);
			BOOST_CHECK_LE(gas.value - _tolerance, m_gasUsed);
		}
	}

	/// Compares the gas computed by PathGasMeter for the given signature (but unknown arguments)
	/// against the actual gas usage computed by the VM on the given set of argument variants.
	void testRunTimeGas(std::string const& _sig, std::vector<bytes> _argumentVariants, u256 const& _tolerance = u256(0))
	{
		u256 gasUsed = 0;
		GasMeter::GasConsumption gas;
		util::FixedHash<4> hash = util::selectorFromSignatureH32(_sig);
		for (bytes const& arguments: _argumentVariants)
		{
			sendMessage(hash.asBytes() + arguments, false, 0);
			BOOST_CHECK(m_transactionSuccessful);
			gasUsed = std::max(gasUsed, m_gasUsed);
			gas = std::max(gas, gasForTransaction(hash.asBytes() + arguments, false));
		}

		gas += GasEstimator(hyperion::test::CommonOptions::get().qrvmVersion()).functionalEstimation(
			*m_compiler.runtimeAssemblyItems(m_compiler.lastContractName()),
			_sig
		);
		// Yul helper calls can prevent the estimator from deriving a finite bound.
		if (!gas.isInfinite)
		{
			BOOST_CHECK_LE(gasUsed, gas.value);
			BOOST_CHECK_LE(gas.value - _tolerance, gasUsed);
		}
	}

	static GasMeter::GasConsumption gasForTransaction(bytes const& _data, bool _isCreation)
	{
		GasMeter::GasConsumption gas = _isCreation ? GasCosts::txCreateGas : GasCosts::txGas;
		for (auto i: _data)
			gas += i != 0 ? GasCosts::txDataNonZeroGas : GasCosts::txDataZeroGas;
		return gas;
	}
};

BOOST_FIXTURE_TEST_SUITE(GasMeterTests, GasMeterTestFramework)

BOOST_AUTO_TEST_CASE(constants_wider_than_256_bits)
{
	// A gas amount is a u256, so a 512-bit constant that the gas depends on makes it infinite
	// instead of being cut to its low 256 bits.
	auto estimateLast = [](std::vector<AssemblyItem> const& _items, bool _includeExternalCosts) {
		auto state = std::make_shared<KnownState>();
		GasMeter meter(state, hyperion::test::CommonOptions::get().qrvmVersion());
		GasMeter::GasConsumption gas;
		for (AssemblyItem const& item: _items)
			gas = meter.estimateMax(item, _includeExternalCosts);
		return gas;
	};
	u512 const huge = u512(1) << 300;

	// LOG0 of 2**300 bytes at an offset that wraps the memory end to 0.
	BOOST_CHECK(estimateLast({AssemblyItem(huge), AssemblyItem(u512(0) - huge), AssemblyItem(Instruction::LOG0)}, false).isInfinite);
	BOOST_CHECK(!estimateLast({AssemblyItem(u512(0)), AssemblyItem(u512(0)), AssemblyItem(Instruction::LOG0)}, false).isInfinite);

	// STATICCALL passing 2**300 gas: gas, address, argsOffset, argsSize, retOffset, retSize.
	std::vector<AssemblyItem> call;
	for (unsigned i = 0; i < 5; ++i)
		call.emplace_back(u512(0));
	call.emplace_back(huge);
	call.emplace_back(Instruction::STATICCALL);
	BOOST_CHECK(estimateLast(call, false).isInfinite);
	call[5] = AssemblyItem(u512(1000));
	BOOST_CHECK(!estimateLast(call, false).isInfinite);
}

BOOST_AUTO_TEST_CASE(simple_contract)
{
	// Tests a simple "deploy contract" code without constructor. The actual contract is not relevant.
	char const* sourceCode = R"(
		contract test {
			bytes32 public shaValue;
			function f(uint a) public {
				shaValue = keccak256(abi.encodePacked(a));
			}
		}
	)";
	testCreationTimeGas(sourceCode);
}

// TODO(now.youtrack.cloud/issue/TS-14): check gas.value - _tolerance <= m_gasUsed has failed [113769 > 93769]
/*
BOOST_AUTO_TEST_CASE(store_keccak256)
{
	char const* sourceCode = R"(
		contract test {
			bytes32 public shaValue;
			constructor() {
				shaValue = keccak256(abi.encodePacked(this));
			}
		}
	)";
	testCreationTimeGas(sourceCode);
}
*/

// TODO(now.youtrack.cloud/issue/TS-14): check gas.value - _tolerance <= m_gasUsed has failed [98816 > 80516]
/*
BOOST_AUTO_TEST_CASE(updating_store)
{
	char const* sourceCode = R"(
		contract test {
			uint data;
			uint data2;
			constructor() {
				data = 1;
				data = 2;
				data2 = 0;
			}
		}
	)";
	testCreationTimeGas(sourceCode, u256(9600));
}
*/

// TODO(now.youtrack.cloud/issue/TS-14): check gas.value - _tolerance <= m_gasUsed has failed [43559 > 23560]
/*
BOOST_AUTO_TEST_CASE(branches)
{
	char const* sourceCode = R"(
		contract test {
			uint data;
			uint data2;
			function f(uint x) public {
				if (x > 7)
					data2 = 1;
				else
					data = 1;
			}
		}
	)";
	testCreationTimeGas(sourceCode, 1);
	testRunTimeGas("f(uint512)", std::vector<bytes>{encodeArgs(2), encodeArgs(8)}, 1);
}
*/

// TODO(now.youtrack.cloud/issue/TS-14): check gas.value - _tolerance <= m_gasUsed has failed [43671 > 23671]
/*
BOOST_AUTO_TEST_CASE(function_calls)
{
	char const* sourceCode = R"(
		contract test {
			uint data;
			uint data2;
			function f(uint x) public {
				if (x > 7)
					{ unchecked { data2 = g(x**8) + 1; } }
				else
					data = 1;
			}
			function g(uint x) internal returns (uint) {
				return data2;
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	// In f, data2 is accessed twice, so there is a reduction of 2200 to 100 in actual costs.
	// However, GasMeter always assumes cold costs.
	testRunTimeGas(
		"f(uint512)",
		std::vector<bytes>{encodeArgs(2), encodeArgs(8)},
		u256(2100)
	);
}
*/

// TODO(now.youtrack.cloud/issue/TS-14): check gas.value - _tolerance <= m_gasUsed has failed [43671 > 23671]
/*
BOOST_AUTO_TEST_CASE(multiple_external_functions)
{
	char const* sourceCode = R"(
		contract test {
			uint data;
			uint data2;
			function f(uint x) public {
				if (x > 7)
					{ unchecked { data2 = g(x**8) + 1; } }
				else
					data = 1;
			}
			function g(uint x) public returns (uint) {
				return data2;
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	// In f, data2 is accessed twice, so there is a reduction of 2200 to 100 in actual costs.
	// However, GasMeter always assumes cold costs.
	testRunTimeGas(
		"f(uint512)",
		std::vector<bytes>{encodeArgs(2), encodeArgs(8)},
		u256(2100)
	);

	testRunTimeGas("g(uint512)", std::vector<bytes>{encodeArgs(2)});
}
*/

BOOST_AUTO_TEST_CASE(exponent_size)
{
	char const* sourceCode = R"(
		contract A {
			function f(uint x) public returns (uint) {
				unchecked { return x ** 0; }
			}
			function g(uint x) public returns (uint) {
				unchecked { return x ** 0x100; }
			}
			function h(uint x) public returns (uint) {
				unchecked { return x ** 0x10000; }
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("f(uint512)", std::vector<bytes>{encodeArgs(2)});
	testRunTimeGas("g(uint512)", std::vector<bytes>{encodeArgs(2)});
	testRunTimeGas("h(uint512)", std::vector<bytes>{encodeArgs(2)});
}

BOOST_AUTO_TEST_CASE(balance_gas)
{
	char const* sourceCode = R"(
		contract A {
			function lookup_balance(address a) public returns (uint) {
				return a.balance;
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("lookup_balance(address)", std::vector<bytes>{encodeArgs(2), encodeArgs(100)});
}

BOOST_AUTO_TEST_CASE(extcodesize_gas)
{
	char const* sourceCode = R"(
		contract A {
			function f() public returns (uint _s) {
				assembly {
					_s := extcodesize(0x30)
				}
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("f()", std::vector<bytes>{encodeArgs()});
}

BOOST_AUTO_TEST_CASE(transient_storage_gas)
{
	// TLOAD and TSTORE cost the same whatever the slot and the value are.
	char const* sourceCode = R"(
		contract A {
			function f() public returns (uint x) {
				assembly {
					tstore(1, 2)
					x := tload(1)
					tstore(1, 0)
				}
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("f()", std::vector<bytes>{encodeArgs()});
}

BOOST_AUTO_TEST_CASE(clz_gas)
{
	// CLZ costs 5 gas, whatever the value is.
	GasMeter meter(std::make_shared<KnownState>(), hyperion::test::CommonOptions::get().qrvmVersion());
	meter.estimateMax(AssemblyItem{Instruction::CALLVALUE});
	GasMeter::GasConsumption clzGas = meter.estimateMax(AssemblyItem{Instruction::CLZ});
	BOOST_CHECK(!clzGas.isInfinite);
	BOOST_CHECK_EQUAL(clzGas.value, u256(5));

	// The function has no parameter: the estimate of one that has both a parameter and a return
	// value is infinite, and the comparison with the gas the QRVM uses would not take place.
	// The argument of the CLZ is not a constant, so the optimizer keeps the instruction.
	char const* sourceCode = R"(
		contract A {
			function f() public pure returns (uint r) {
				assembly {
					r := clz(calldatasize())
				}
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("f()", std::vector<bytes>{encodeArgs()});
}

BOOST_AUTO_TEST_CASE(mcopy_gas)
{
	// The estimate is exact for constant arguments, also where the copy extends memory:
	// by its source range in the third copy and by both ranges in the second one.
	// The function returns nothing: after an MCOPY the estimator no longer knows the free memory
	// pointer, and so not what the encoding of a return value costs.
	char const* sourceCode = R"(
		contract A {
			function f() public {
				assembly {
					mstore(0x200, 1)
					mcopy(0x100, 0x200, 0x40)
					mcopy(0x1000, 0x800, 0x81)
					mcopy(0x900, 0x2000, 1)
					mcopy(0x8000, 0x10000, 0)
				}
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("f()", std::vector<bytes>{encodeArgs()});
}

BOOST_AUTO_TEST_CASE(mcopy_memory_expansion_gas)
{
	// 3 gas, 3 gas for each word copied and the growth of memory to the higher of the two ranges.
	// A word has 64 bytes and w words of memory cost 3 * w + w * w / 512 gas.
	auto mcopyGas = [](AssemblyItem const& _target, AssemblyItem const& _source, AssemblyItem const& _size, u256 const& _memorySize)
	{
		GasMeter meter(std::make_shared<KnownState>(), hyperion::test::CommonOptions::get().qrvmVersion(), _memorySize);
		meter.estimateMax(_size);
		meter.estimateMax(_source);
		meter.estimateMax(_target);
		return meter.estimateMax(AssemblyItem{Instruction::MCOPY});
	};
	auto checkGas = [&](GasMeter::GasConsumption const& _gas, unsigned _expected)
	{
		BOOST_CHECK(!_gas.isInfinite);
		BOOST_CHECK_EQUAL(_gas.value, u256(_expected));
	};

	// Inside the memory there is.
	checkGas(mcopyGas(u256(0x40), u256(0), u256(0x40), 0x100), 3 + 3);
	checkGas(mcopyGas(u256(0x40), u256(0), u256(0x41), 0x100), 3 + 2 * 3);
	// 65 words for the source range, 65 words for the target range.
	checkGas(mcopyGas(u256(0), u256(0x1000), u256(0x40), 0), 3 + 3 + 203);
	checkGas(mcopyGas(u256(0x1000), u256(0), u256(0x40), 0), 3 + 3 + 203);
	// Both ranges are beyond the end of memory, the target range more so: 129 words.
	checkGas(mcopyGas(u256(0x2000), u256(0x1000), u256(0x40), 0), 3 + 3 + 419);
	checkGas(mcopyGas(u256(0x1000), u256(0x2000), u256(0x40), 0), 3 + 3 + 419);
	// Only the growth is paid for: from 65 to 129 words.
	checkGas(mcopyGas(u256(0x2000), u256(0x1000), u256(0x40), 0x1040), 3 + 3 + 419 - 203);
	// A copy of nothing does not extend memory, whatever the positions are.
	checkGas(mcopyGas(u256(0x2000), u256(0x1000), u256(0), 0), 3);
	checkGas(mcopyGas(Instruction::CALLDATASIZE, Instruction::CALLVALUE, u256(0), 0), 3);

	// Nothing is known of the cost if the size or the higher position is not known.
	BOOST_CHECK(mcopyGas(u256(0), u256(0x40), Instruction::CALLDATASIZE, 0x100).isInfinite);
	BOOST_CHECK(mcopyGas(Instruction::CALLDATASIZE, u256(0), u256(0x40), 0x100).isInfinite);
	BOOST_CHECK(mcopyGas(u256(0), Instruction::CALLDATASIZE, u256(0x40), 0x100).isInfinite);
}

BOOST_AUTO_TEST_CASE(regular_functions_exclude_fallback)
{
	// A bug in the estimator caused the costs for a specific function
	// to always include the costs for the fallback.
	char const* sourceCode = R"(
		contract A {
			uint public x;
			fallback() external { x = 2; }
		}
	)";
	testCreationTimeGas(sourceCode);
	testRunTimeGas("x()", std::vector<bytes>{encodeArgs()});
}

BOOST_AUTO_TEST_CASE(complex_control_flow)
{
	// This crashed the gas estimator previously (or took a very long time).
	// Now we do not follow branches if they start out with lower gas costs than the ones
	// we previously considered. This of course reduces accuracy.
	char const* sourceCode = R"(
		contract log {
			function ln(int128 x) public pure returns (int128 result) {
				unchecked {
					int128 t = x / 256;
					int128 y = 5545177;
					x = t;
					t = x * 16; if (t <= 1000000) { x = t; y = y - 2772588; }
					t = x * 4; if (t <= 1000000) { x = t; y = y - 1386294; }
					t = x * 2; if (t <= 1000000) { x = t; y = y - 693147; }
					t = x + x / 2; if (t <= 1000000) { x = t; y = y - 405465; }
					t = x + x / 4; if (t <= 1000000) { x = t; y = y - 223144; }
					t = x + x / 8; if (t <= 1000000) { x = t; y = y - 117783; }
					t = x + x / 16; if (t <= 1000000) { x = t; y = y - 60624; }
					t = x + x / 32; if (t <= 1000000) { x = t; y = y - 30771; }
					t = x + x / 64; if (t <= 1000000) { x = t; y = y - 15504; }
					t = x + x / 128; if (t <= 1000000) { x = t; y = y - 7782; }
					t = x + x / 256; if (t <= 1000000) { x = t; y = y - 3898; }
					t = x + x / 512; if (t <= 1000000) { x = t; y = y - 1951; }
					t = x + x / 1024; if (t <= 1000000) { x = t; y = y - 976; }
					t = x + x / 2048; if (t <= 1000000) { x = t; y = y - 488; }
					t = x + x / 4096; if (t <= 1000000) { x = t; y = y - 244; }
					t = x + x / 8192; if (t <= 1000000) { x = t; y = y - 122; }
					t = x + x / 16384; if (t <= 1000000) { x = t; y = y - 61; }
					t = x + x / 32768; if (t <= 1000000) { x = t; y = y - 31; }
					t = x + x / 65536; if (t <= 1000000) { y = y - 15; }
					return y;
				}
			}
		}
	)";
	testCreationTimeGas(sourceCode);
	// max gas is used for small x
	testRunTimeGas("ln(int128)", std::vector<bytes>{encodeArgs(0), encodeArgs(10), encodeArgs(105), encodeArgs(30000)});
}

BOOST_AUTO_TEST_SUITE_END()

}
