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
/** @file IhypTestOptions.cpp
* @date 2019
*/

#include <test/tools/IhypTestOptions.h>

#include <libhyputil/Assertions.h>

#include <boost/filesystem.hpp>

#include <iostream>
#include <regex>
#include <string>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace hyperion::test
{

namespace
{

auto const description = R"(ihyptest, tool for interactively managing test contracts.
Usage: ihyptest [Options]
Interactively validates test contracts.

Allowed options)";

std::string editorPath()
{
	if (getenv("EDITOR"))
		return getenv("EDITOR");
	else if (fs::exists("/usr/bin/editor"))
		return "/usr/bin/editor";

	return std::string{};
}

}

IhypTestOptions::IhypTestOptions():
	CommonOptions(description)
{
}

void IhypTestOptions::addOptions()
{
	CommonOptions::addOptions();
	options.add_options()
		("editor", po::value<std::string>(&editor)->default_value(editorPath()), "Path to editor for opening test files.")
		("help", po::bool_switch(&showHelp)->default_value(showHelp), "Show this help screen.")
		("no-color", po::bool_switch(&noColor)->default_value(noColor), "Don't use colors.")
		("accept-updates", po::bool_switch(&acceptUpdates)->default_value(acceptUpdates), "Automatically accept expectation updates.")
		("test,t", po::value<std::string>(&testFilter)->default_value("*/*"), "Filters which test units to include.");
}

bool IhypTestOptions::parse(int _argc, char const* const* _argv)
{
	bool const shouldContinue = CommonOptions::parse(_argc, _argv);

	if (showHelp || !shouldContinue)
	{
		std::cout << options << std::endl;
		return false;
	}

	enforceGasTest = enforceGasTest || (zvmVersion() == langutil::ZVMVersion{} && !useABIEncoderV1);

	return shouldContinue;
}

void IhypTestOptions::validate() const
{
	CommonOptions::validate();
	static std::string filterString{"[a-zA-Z0-9_/*]*"};
	static std::regex filterExpression{filterString};
	assertThrow(
		regex_match(testFilter, filterExpression),
		ConfigException,
		"Invalid test unit filter - can only contain '" + filterString + ": " + testFilter
	);
}

}
