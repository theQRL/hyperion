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
 * Unit tests for the compiler itself.
 */

#include <test/libhyperion/AnalysisFramework.h>
#include <test/Metadata.h>
#include <test/Common.h>

#include <libhyperion/ast/TypeProvider.h>
#include <libhyperion/codegen/MultiUseYulFunctionCollector.h>
#include <libhyperion/codegen/YulUtilFunctions.h>

#include <boost/test/unit_test.hpp>


namespace hyperion::frontend::test
{

class HyperionCompilerFixture: protected AnalysisFramework
{
	void setupCompiler(CompilerStack& _compiler) override
	{
		AnalysisFramework::setupCompiler(_compiler);

		// FIXME: This test was probably supposed to respect CommonOptions::get().optimize but
		// due to a bug it was always running with optimizer disabled and it does not pass with it.
		_compiler.setOptimiserSettings(false);
	}
};

BOOST_FIXTURE_TEST_SUITE(HyperionCompiler, HyperionCompilerFixture)

BOOST_AUTO_TEST_CASE(does_not_include_creation_time_only_internal_functions)
{
	char const* sourceCode = R"(
		contract C {
			uint x;
			constructor() { f(); }
			function f() internal { unchecked { for (uint i = 0; i < 10; ++i) x += 3 + i; } }
		}
	)";

	runFramework(sourceCode, PipelineStage::Compilation);
	BOOST_REQUIRE_MESSAGE(
		pipelineSuccessful(),
		"Contract compilation failed:\n" + formatErrors(filteredErrors(), true /* _colored */)
	);

	bytes const& creationBytecode = hyperion::test::bytecodeSansMetadata(compiler().object("C").bytecode);
	bytes const& runtimeBytecode = hyperion::test::bytecodeSansMetadata(compiler().runtimeObject("C").bytecode);
	BOOST_CHECK(creationBytecode.size() >= 80);
	BOOST_CHECK(creationBytecode.size() <= 260);
	unsigned threshold = 9;
	BOOST_CHECK(runtimeBytecode.size() >= threshold);
	BOOST_CHECK(runtimeBytecode.size() <= 30);
}

BOOST_AUTO_TEST_CASE(literal_exp_bound_uses_common_type_width)
{
	MultiUseYulFunctionCollector collector;
	YulUtilFunctions utils(langutil::QRVMVersion::zond(), RevertStrings::Default, collector);

	utils.overflowCheckedIntLiteralExpFunction(
		*TypeProvider::rationalNumber(rational(2, 1)),
		*TypeProvider::uint(16),
		*TypeProvider::uint(512)
	);

	std::string const generatedFunctions = collector.requestedFunctions();
	BOOST_CHECK(generatedFunctions.find("if gt(exponent, 511)") != std::string::npos);
	BOOST_CHECK(generatedFunctions.find("if gt(exponent, 255)") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(literal_exp_base_uses_common_type_width)
{
	MultiUseYulFunctionCollector collector;
	YulUtilFunctions utils(langutil::QRVMVersion::zond(), RevertStrings::Default, collector);

	bigint const wideBase = bigint(1) << 256;
	utils.overflowCheckedIntLiteralExpFunction(
		*TypeProvider::rationalNumber(rational(wideBase, 1)),
		*TypeProvider::uint(16),
		*TypeProvider::uint(512)
	);

	std::string const generatedFunctions = collector.requestedFunctions();
	BOOST_CHECK(generatedFunctions.find("power := exp(" + wideBase.str() + ", exponent)") != std::string::npos);
	BOOST_CHECK(generatedFunctions.find("power := exp(0, exponent)") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()

}
