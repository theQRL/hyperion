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
 * @date 2017
 * Unit tests for libhypc/libhypc.cpp.
 */

#include <string>
#include <boost/test/unit_test.hpp>
#include <libhyputil/JSON.h>
#include <libhyperion/interface/ReadFile.h>
#include <libhyperion/interface/Version.h>
#include <libhypc/libhypc.h>


namespace hyperion::frontend::test
{

namespace
{

/// TODO: share this between StandardCompiler.cpp
/// Helper to match a specific error type and message
bool containsError(Json::Value const& _compilerResult, std::string const& _type, std::string const& _message)
{
	if (!_compilerResult.isMember("errors"))
		return false;

	for (auto const& error: _compilerResult["errors"])
	{
		BOOST_REQUIRE(error.isObject());
		BOOST_REQUIRE(error["type"].isString());
		BOOST_REQUIRE(error["message"].isString());
		if ((error["type"].asString() == _type) && (error["message"].asString() == _message))
			return true;
	}

	return false;
}

Json::Value compile(std::string const& _input, CStyleReadFileCallback _callback = nullptr)
{
	char* output_ptr = hyperion_compile(_input.c_str(), _callback, nullptr);
	std::string output(output_ptr);
	hyperion_free(output_ptr);
	hyperion_reset();
	Json::Value ret;
	BOOST_REQUIRE(util::jsonParseStrict(output, ret));
	return ret;
}

char* stringToHyperion(std::string const& _input)
{
	char* ptr = hyperion_alloc(_input.length());
	BOOST_REQUIRE(ptr != nullptr);
	std::memcpy(ptr, _input.c_str(), _input.length());
	return ptr;
}

} // end anonymous namespace

BOOST_AUTO_TEST_SUITE(LibHypc)

BOOST_AUTO_TEST_CASE(read_version)
{
	std::string output(hyperion_version());
	BOOST_CHECK(output.find(VersionString) == 0);
}

BOOST_AUTO_TEST_CASE(read_license)
{
	std::string output(hyperion_license());
	BOOST_CHECK(output.find("GNU GENERAL PUBLIC LICENSE") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(standard_compilation)
{
	char const* input = R"(
	{
		"language": "Hyperion",
		"sources": {
			"fileA": {
				"content": "contract A { }"
			}
		}
	}
	)";
	Json::Value result = compile(input);
	BOOST_REQUIRE(result.isObject());

	// Only tests some assumptions. The StandardCompiler is tested properly in another suite.
	BOOST_CHECK(result.isMember("sources"));
	// This used to test that it is a member, but we did not actually request any output,
	// so there should not be a contract member.
	BOOST_CHECK(!result.isMember("contracts"));
}

BOOST_AUTO_TEST_CASE(missing_callback)
{
	char const* input = R"(
	{
		"language": "Hyperion",
		"sources": {
			"fileA": {
				"content": "import \"missing.hyp\"; contract A { }"
			}
		}
	}
	)";
	Json::Value result = compile(input);
	BOOST_REQUIRE(result.isObject());

	BOOST_CHECK(containsError(result, "ParserError", "Source \"missing.hyp\" not found: File not supplied initially."));
}

BOOST_AUTO_TEST_CASE(with_callback)
{
	char const* input = R"(
	{
		"language": "Hyperion",
		"sources": {
			"fileA": {
				"content": "import \"found.hyp\"; import \"notfound.hyp\"; contract A { }"
			}
		}
	}
	)";

	CStyleReadFileCallback callback{
		[](void* _context, char const* _kind, char const* _path, char** o_contents, char** o_error)
		{
			// Passed in a nullptr in the compile() helper above.
			BOOST_REQUIRE(_context == nullptr);
			// Caller frees the pointers.
			BOOST_REQUIRE(std::string(_kind) == ReadCallback::kindString(ReadCallback::Kind::ReadFile));
			if (std::string(_path) == "found.hyp")
			{
				static std::string content{"import \"missing.hyp\"; contract B {}"};
				*o_contents = stringToHyperion(content);
				*o_error = nullptr;
			}
			else if (std::string(_path) == "missing.hyp")
			{
				static std::string errorMsg{"Missing file."};
				*o_error = stringToHyperion(errorMsg);
				*o_contents = nullptr;
			}
			else
			{
				*o_error = nullptr;
				*o_contents = nullptr;
			}
		}
	};

	Json::Value result = compile(input, callback);
	BOOST_REQUIRE(result.isObject());

	// This ensures that "found.hyp" was properly loaded which triggered the second import statement.
	BOOST_CHECK(containsError(result, "ParserError", "Source \"missing.hyp\" not found: Missing file."));

	// This should be placed due to the missing "notfound.hyp" which sets both pointers to null.
	BOOST_CHECK(containsError(result, "ParserError", "Source \"notfound.hyp\" not found: Callback not supported."));
}

BOOST_AUTO_TEST_SUITE_END()

} // end namespaces
