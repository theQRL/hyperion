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
/**
 * Unit tests for the view and pure checker.
 */

#include <test/libhyperion/AnalysisFramework.h>

#include <test/Common.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <tuple>

using namespace hyperion::langutil;

namespace hyperion::frontend::test
{

BOOST_FIXTURE_TEST_SUITE(ViewPureChecker, AnalysisFramework)

BOOST_AUTO_TEST_CASE(environment_access)
{
	std::vector<std::string> view{
		"block.coinbase",
		"block.timestamp",
		"block.prevrandao",
		"block.number",
		"block.gaslimit",
		"blockhash(7)",
		"gasleft()",
		"msg.value",
		"msg.sender",
		"tx.origin",
		"tx.gasprice",
		"this",
		"address(1).balance",
		"address(0x4242).staticcall(\"\")",
	};

	// ``blockhash`` is tested separately below because its usage will
	// produce warnings that can't be handled in a generic way.
	std::vector<std::string> pure{
		"msg.data",
		"msg.data[0]",
		"msg.sig",
		"msg",
		"block",
		"tx"
	};
	for (std::string const& x: view)
	{
		CHECK_ERROR(
			"contract C { function f() pure public { " + x + "; } }",
			TypeError,
			"Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires \"view\""
		);
	}
	for (std::string const& x: pure)
	{
		CHECK_WARNING(
			"contract C { function f() view public { " + x + "; } }",
			"Function state mutability can be restricted to pure"
		);
	}

	CHECK_WARNING_ALLOW_MULTI(
		"contract C { function f() view public { blockhash; } }",
		(std::vector<std::string>{
			"Function state mutability can be restricted to pure",
			"Statement has no effect."
	}));
}

BOOST_AUTO_TEST_CASE(address_staticcall)
{
	std::string text = R"(
		contract C {
			function i() view public returns (bool) {
				(bool success,) = address(0x4242).staticcall("");
				return success;
			}
		}
	)";
	CHECK_SUCCESS_NO_WARNINGS(text);
}


BOOST_AUTO_TEST_CASE(assembly_staticcall)
{
	std::string text = R"(
		contract C {
			function i() view public {
				assembly { pop(staticcall(gas(), 1, 2, 3, 4, 5)) }
			}
		}
	)";
	CHECK_SUCCESS_NO_WARNINGS(text);
}

BOOST_AUTO_TEST_SUITE_END()

}
