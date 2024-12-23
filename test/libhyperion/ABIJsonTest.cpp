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
 * Unit tests for the hyperion compiler ABI JSON Interface output.
 */

#include <test/libhyperion/ABIJsonTest.h>

#include <test/Common.h>

#include <libhyperion/interface/CompilerStack.h>
#include <libhyputil/JSON.h>
#include <libhyputil/AnsiColorized.h>

#include <fstream>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;

ABIJsonTest::ABIJsonTest(std::string const& _filename):
	TestCase(_filename)
{
	m_source = m_reader.source();
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult ABIJsonTest::run(std::ostream& _stream, std::string const& _linePrefix, bool _formatted)
{
	CompilerStack compiler;

	compiler.setSources({{
		"",
		"pragma hyperion >=0.0;\n// SPDX-License-Identifier: GPL-3.0\n" + m_source
	}});
	compiler.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	compiler.setOptimiserSettings(hyperion::test::CommonOptions::get().optimize);
	if (!compiler.parseAndAnalyze())
		BOOST_THROW_EXCEPTION(std::runtime_error("Parsing contract failed"));

	m_obtainedResult.clear();
	bool first = true;
	for (std::string const& contractName: compiler.contractNames())
	{
		if (!first)
			m_obtainedResult += "\n\n";
		m_obtainedResult += "    " + contractName + "\n";
		m_obtainedResult += jsonPrettyPrint(compiler.contractABI(contractName)) + "\n";
		first = false;
	}

	return checkResult(_stream, _linePrefix, _formatted);
}
